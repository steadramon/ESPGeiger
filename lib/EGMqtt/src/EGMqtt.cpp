/*
  EGMqtt.cpp - MQTT 3.1.1 client, run from the main loop.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "EGMqtt.h"

#include <Arduino.h>
#include <algorithm>
#include <new>
#include <string.h>

using namespace EGMqttWire;

static const uint8_t PACKETS_A_LOOP = 8;   // then the rest of the loop gets a turn
static const size_t ACK_ROOM = 64;          // held back so acks always fit
static const size_t HEADER_MAX = 5;         // first byte and 4 length bytes

// records sit on 4 byte offsets, so a Rec's fields are aligned
static size_t padded(size_t n) { return (n + 3) & ~(size_t)3; }

EGMqtt::~EGMqtt() {
  _tcp.close();
  delete _m;
  free(_strs);
}

// Settings

bool EGMqtt::mem() {
  if (_m) return true;
  _m = new (std::nothrow) Mem();
  return _m != nullptr;
}

// g set with f, both or neither
bool EGMqtt::set_str(Field f, const char* v, Field g, const char* v2) {
  const char* vals[F_COUNT];
  size_t total = 0;
  bool same = true;
  for (uint8_t i = 0; i < F_COUNT; i++) {
    const char* s = i == f ? v : i == g ? v2 : str((Field)i);
    if (!s) s = "";
    size_t n = strlen(s);
    if (n >= EGMQTT_STR_CAP) return false;
    same = same && strcmp(s, str((Field)i)) == 0;
    vals[i] = s;
    total += n + 1;
  }
  if (same) return true;
  char* b = (char*)malloc(total);
  if (!b) return false;
  uint16_t at[F_COUNT];
  size_t w = 0;
  for (uint8_t i = 0; i < F_COUNT; i++) {
    const char* s = vals[i];
    size_t len = strlen(s) + 1;
    memcpy(b + w, s, len);
    at[i] = (uint16_t)w;
    w += len;
  }
  free(_strs);
  _strs = b;
  memcpy(_at, at, sizeof(_at));
  return true;
}

bool EGMqtt::setServer(const char* host, uint16_t port) {
  if (!host || !set_str(F_HOST, host)) return false;
  _port = port;
  return true;
}

bool EGMqtt::setClientId(const char* id) {
  Connect c = conf();
  c.client_id = id ? id : "";
  if (!fits(c) || !set_str(F_ID, id)) return false;
  sized();
  return true;
}

bool EGMqtt::setCredentials(const char* user, const char* pass) {
  if (pass && !user) return false;   // MQTT 3.1.1 has no password alone
  Connect c = conf();
  c.user = user;
  c.pass = pass;
  if (!fits(c) || !set_str(F_USER, user, F_PASS, pass)) return false;
  _hasUser = user != nullptr;
  _hasPass = pass != nullptr;
  sized();
  return true;
}

// set_str copies it
bool EGMqtt::setWill(const char* topic, const __FlashStringHelper* msg, uint8_t qos, bool retain) {
  char m[EGMQTT_STR_CAP];
  size_t n = msg ? strlen_P((PGM_P)msg) : 0;
  if (n >= sizeof(m)) return false;
  if (msg) memcpy_P(m, msg, n);
  m[n] = '\0';
  return setWill(topic, m, qos, retain);
}

bool EGMqtt::setWill(const char* topic, const char* msg, uint8_t qos, bool retain) {
  if (!topic_ok(topic) || qos > 2) return false;
  Connect c = conf();
  c.will_topic = topic;
  c.will_msg = (const uint8_t*)(msg ? msg : "");
  c.will_len = msg ? strlen(msg) : 0;
  c.will_qos = qos;
  c.will_retain = retain;
  if (!fits(c) || !set_str(F_WILL_TOPIC, topic, F_WILL_MSG, msg)) return false;
  _will = true;
  _willQos = qos;
  _willRetain = retain;
  sized();
  return true;
}

void EGMqtt::clearWill() {
  _will = false;
  sized();
}

// CONNECT room kept free
size_t EGMqtt::needs(const Connect& c) {
  return sizeof(Rec) + padded(connect_size(c)) + ACK_ROOM;
}

void EGMqtt::sized() { _reserve = needs(conf()); }

// CONNECT still fits beside the queue
bool EGMqtt::fits(const Connect& c) const { return _outLen + needs(c) <= OUT_CAP; }

EGMqttWire::Connect EGMqtt::conf() const {
  Connect c;
  c.client_id = str(F_ID);
  c.user = _hasUser ? str(F_USER) : nullptr;
  c.pass = _hasPass ? str(F_PASS) : nullptr;
  if (_will) {
    c.will_topic = str(F_WILL_TOPIC);
    c.will_msg = (const uint8_t*)str(F_WILL_MSG);
    c.will_len = strlen(str(F_WILL_MSG));
    c.will_qos = _willQos;
    c.will_retain = _willRetain;
  }
  c.keepalive = _keepalive;
  c.clean = _clean;
  return c;
}

// Up and down

bool EGMqtt::connect() {
  if (_state != DISCONNECTED || !str(F_HOST)[0]) return false;
  if (!connect_size(conf())) {
    _err = PROTOCOL;   // settings no broker takes
    return false;
  }
  sized();
  if (!mem() || _reserve > OUT_CAP) {
    _err = NO_MEMORY;
    return false;
  }
  if (!_tcp.connect(str(F_HOST), _port)) {
    _err = TCP_FAILED;
    return false;
  }
  _err = OK;
  _state = CONNECTING;
  _phase = DIAL;
  _phaseMs = millis();
  return true;
}

void EGMqtt::disconnect() {
  if (_state == DISCONNECTED) return;
  if (_phase == UP) {
    size_t cap;
    uint8_t* p = tail(&cap, false);
    size_t n = p ? bare(p, cap, DISCONNECT) : 0;
    if (n) {
      commit(K_ONCE, 0, n);
      flush();
    }
  }
  end(OK);
  _downNew = false;   // asked for, no callback
}

void EGMqtt::stop() {
  disconnect();
  delete _m;
  _m = nullptr;
  _outLen = 0;
  _txOff = 0;
  _pend = nullptr;
  free(_strs);
  _strs = nullptr;
  memset(_at, 0, sizeof(_at));
  _will = _hasUser = _hasPass = false;
  sized();
}

// QoS 1 and 2 wait for the next connection
void EGMqtt::end(int8_t err) {
  if (_state == DISCONNECTED) return;
  _tcp.close();
  _state = DISCONNECTED;
  _phase = IDLE;
  _err = err;
  _upNew = false;
  _downNew = true;
  _rx = R_HEAD;
  _hdrLen = 0;
  _txOff = 0;
  _pingOut = _pingSent = false;
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    if (r->sent == S_DONE) continue;
    if (r->kind == K_SUB || r->kind == K_UNSUB) {
      r->sent = S_LOST;
      _lostNew = true;
    } else if (r->kind < K_PUB1) {
      r->sent = S_DONE;
    }
  }
  compact();
}

// callbacks last
void EGMqtt::loop() {
  if (_state != DISCONNECTED) {
    uint32_t now = millis();
    if (_phase == DIAL) dial(now);
    if (_phase == HELLO || _phase == UP) receive();
    // closed, nothing left it can read
    bool gone = _tcp.state() == EGTcpConn::CLOSED && (!_tcp.available() || !acks_fit());
    if (_phase != IDLE && _phase != DIAL && gone) end(CONNECTION_LOST);
    if (_state != DISCONNECTED) timers(now);
    if (_state != DISCONNECTED) flush();
  }
  events();
}

// up and down last, a callback may reconnect
void EGMqtt::events() {
  if (_lostNew) lost();
  if (_upNew) {
    _upNew = false;
    if (_onUp) _onUp(_onUpArg, _session);
  }
  if (_downNew) {
    _downNew = false;
    if (_onDown) _onDown(_onDownArg, _err);
  }
}

// 0x80 as if refused, callback may move the outbox
void EGMqtt::lost() {
  _lostNew = false;
  for (;;) {
    Rec* r = nullptr;
    for (size_t off = 0; off < _outLen && !r; off = next(off)) {
      if (rec(off)->sent == S_LOST) r = rec(off);
    }
    if (!r) return;
    r->sent = S_DONE;
    note(r->id, r->kind == K_SUB ? ACK_SUBSCRIBE : ACK_UNSUBSCRIBE, 0x80);
  }
}

void EGMqtt::dial(uint32_t now) {
  EGTcpConn::State s = _tcp.state();
  if (s == EGTcpConn::CLOSED) {
    // no answer, lwIP gave up
    end(_tcp.error() == EGTcpConn::E_ABRT ? TIMEOUT : TCP_FAILED);
    return;
  }
  if (s != EGTcpConn::OPEN) return;
  // CONNECT first, alone till the CONNACK
  size_t cap;
  uint8_t* p = tail(&cap, false);   // compacts, so _outLen after this
  size_t old = _outLen;
  size_t n = p ? EGMqttWire::connect(p, cap, conf()) : 0;
  if (!n) {
    end(NO_MEMORY);
    return;
  }
  commit(K_CONNECT, 0, n);
  std::rotate(_m->out, _m->out + old, _m->out + _outLen);
  _phase = HELLO;
  _phaseMs = now;
}

// ping when either way goes quiet
void EGMqtt::timers(uint32_t now) {
  uint32_t limit = _timeoutS * 1000UL;
  if (_phase != UP) {
    if (now - _phaseMs >= limit) end(TIMEOUT);
    return;
  }
  if (stuck(now)) return;
  if (_pingOut) {
    if (_pingSent && now - _pingMs >= limit) end(TIMEOUT);
    return;
  }
  uint32_t ka = _keepalive * 1000UL;
  if (ka && (now - _txMs >= ka || now - _rxMs >= ka)) {
    queue(PINGREQ, 0);
    _pingOut = true;
  }
}

// no resends in 3.1.1, a stuck ack or write ends the connection
bool EGMqtt::stuck(uint32_t now) {
  uint32_t limit = 2 * _timeoutS * 1000UL;
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    bool waits = r->sent == S_SENT || (r->sent == S_NEW && r->at);
    if (waits && now - r->at >= limit) {
      end(TIMEOUT);
      return true;
    }
  }
  return false;
}

// Outbox

EGMqtt::Rec* EGMqtt::rec(size_t off) const { return (Rec*)(_m->out + off); }

size_t EGMqtt::next(size_t off) const { return off + sizeof(Rec) + padded(rec(off)->len); }

// room for one more packet, built in place then commit()ed
uint8_t* EGMqtt::tail(size_t* cap, bool keep_reserve) {
  if (!_m) return nullptr;
  compact();
  size_t limit = OUT_CAP - (keep_reserve ? _reserve : 0);
  size_t at = _outLen + sizeof(Rec);
  if (at >= limit) return nullptr;
  *cap = (limit - at) & ~(size_t)3;   // so the padding fits too
  return _m->out + at;
}

// full outbox stops reading, an ack must fit
bool EGMqtt::acks_fit() const {
  return OUT_CAP - _outLen >= sizeof(Rec) + padded(4);
}

void EGMqtt::commit(Kind k, uint16_t id, size_t len) {
  Rec* r = rec(_outLen);
  r->at = 0;
  r->len = (uint16_t)len;
  r->id = id;
  r->kind = k;
  r->sent = S_NEW;
  _outLen += sizeof(Rec) + padded(len);
}

// an ack or ping, ACK_ROOM keeps space for it unless the broker floods
void EGMqtt::queue(Type t, uint16_t id) {
  if (_phase != UP) return;
  size_t cap;
  uint8_t* p = tail(&cap, false);
  size_t n = !p ? 0 : t == PINGREQ ? bare(p, cap, t) : ack(p, cap, t, id);
  if (!n) {
    end(NO_MEMORY);
    return;
  }
  commit(t == PUBREL ? K_REL : t == PINGREQ ? K_PING : K_ONCE, id, n);
}

EGMqtt::Rec* EGMqtt::find(Kind k, uint16_t id) {
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    if (r->kind == k && r->id == id && r->sent != S_DONE) return r;
  }
  return nullptr;
}

EGMqtt::Rec* EGMqtt::find(Kind k) {
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    if (r->kind == k && r->sent != S_DONE) return r;
  }
  return nullptr;
}

// not 0, not one still waiting for its ack
uint16_t EGMqtt::next_id() {
  for (;;) {
    if (++_lastId == 0) _lastId = 1;
    bool used = false;
    for (size_t off = 0; off < _outLen && !used; off = next(off)) {
      used = rec(off)->id == _lastId && rec(off)->sent != S_DONE;
    }
    if (!used) return _lastId;
  }
}

// in order, as far as the TCP window takes; only CONNECT before the CONNACK
void EGMqtt::flush() {
  if (!_m || _tcp.state() != EGTcpConn::OPEN) return;
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    if (r->sent != S_NEW) continue;
    if (_phase != UP && r->kind != K_CONNECT) break;
    const uint8_t* p = _m->out + off + sizeof(Rec);
    size_t n = _tcp.write(p + _txOff, r->len - _txOff);
    _txOff += n;
    if (_txOff < r->len) {
      if (n || !r->at) r->at = millis();   // stuck() times the wait
      break;
    }
    _txOff = 0;
    _txMs = r->at = millis();
    if (r->kind == K_PING) {
      _pingSent = true;   // the reply's deadline runs from here
      _pingMs = _txMs;
    }
    r->sent = r->kind >= K_PUB1 ? S_SENT : S_DONE;
  }
  compact();
}

void EGMqtt::compact() {
  _pend = nullptr;
  size_t w = 0;
  for (size_t off = 0; off < _outLen;) {
    size_t n = next(off) - off;
    if (rec(off)->sent != S_DONE) {
      if (w != off) memmove(_m->out + w, _m->out + off, n);
      w += n;
    }
    off += n;
  }
  _outLen = w;
}

// DUP only if the broker kept the session
void EGMqtt::rewind() {
  for (size_t off = 0; off < _outLen; off = next(off)) {
    Rec* r = rec(off);
    r->at = 0;
    if (r->sent != S_SENT) continue;
    r->sent = S_NEW;
    if (r->kind == K_PUB1 || r->kind == K_PUB2) {
      uint8_t* first = _m->out + off + sizeof(Rec);
      if (_session) *first |= DUP;
      else *first &= (uint8_t)~DUP;
    }
  }
}

// fresh session, nothing to release
void EGMqtt::settle() {
  for (Rec* r; (r = find(K_REL));) {
    uint16_t id = r->id;
    r->sent = S_DONE;   // before the callback, which may move the outbox
    note(id, ACK_PUBLISH, 2);
  }
}

size_t EGMqtt::queued() const {
  size_t n = 0;
  if (!_m) return 0;
  for (size_t off = 0; off < _outLen; off = next(off)) {
    uint8_t k = rec(off)->kind;
    if (rec(off)->sent != S_DONE && k >= K_PUB1 && k <= K_REL) n++;
  }
  return n;
}

size_t EGMqtt::room() const {
  size_t used = _outLen + sizeof(Rec) + _reserve;
  return used < OUT_CAP ? (OUT_CAP - used) & ~(size_t)3 : 0;
}

// Sending

uint16_t EGMqtt::publish(const char* topic, const uint8_t* payload, size_t len,
                         uint8_t qos, bool retain) {
  if (!topic || qos > 2 || (!qos && _phase != UP) || !mem()) return 0;
  size_t tn = strlen(topic);
  if (tn > 0xFFFF) return 0;
  Publish m;
  m.topic = topic;
  m.topic_len = (uint16_t)tn;
  m.payload = payload;
  m.len = len;
  m.qos = qos;
  m.retain = retain;
  m.id = qos ? next_id() : 0;
  size_t cap;
  uint8_t* p = tail(&cap, true);
  size_t n = p ? EGMqttWire::publish(p, cap, m) : 0;
  if (!n) return 0;
  commit(qos == 0 ? K_ONCE : qos == 1 ? K_PUB1 : K_PUB2, m.id, n);
  flush();
  return qos ? m.id : 1;
}

uint16_t EGMqtt::publish(const char* topic, const char* payload, uint8_t qos, bool retain) {
  size_t len = payload ? strlen(payload) : 0;
  return publish(topic, (const uint8_t*)payload, len, qos, retain);
}

uint16_t EGMqtt::publish(const char* topic, const __FlashStringHelper* payload,
                         uint8_t qos, bool retain) {
  PGM_P p = (PGM_P)payload;
  size_t len = p ? strlen_P(p) : 0;
  size_t cap;
  uint8_t* out = publish_begin(topic, qos, retain, &cap);
  if (!out) return 0;
  if (len <= cap) memcpy_P(out, p, len);
  return publish_end(len);   // refuses one that didn't fit
}

// room for the longest header, the topic and id; the payload goes after
static size_t ahead_of(size_t topic_len, uint8_t qos) {
  return HEADER_MAX + 2 + topic_len + (qos ? 2 : 0);
}

uint8_t* EGMqtt::publish_begin(const char* topic, uint8_t qos, bool retain, size_t* cap) {
  if (!topic || qos > 2 || (!qos && _phase != UP) || !mem()) return nullptr;
  size_t room;
  uint8_t* p = tail(&room, true);   // compacts, clears _pend
  size_t ahead = ahead_of(strlen(topic), qos);
  if (!p || room <= ahead) return nullptr;
  _pend = p + ahead;
  _pendCap = room - ahead;
  _pendTopic = topic;
  _pendQos = qos;
  _pendRetain = retain;
  *cap = _pendCap;
  return _pend;
}

// header in front, payload slides down
uint16_t EGMqtt::publish_end(size_t len) {
  uint8_t* body = _pend;
  _pend = nullptr;
  if (!body || len > _pendCap) return 0;
  size_t tn = strlen(_pendTopic);
  if (tn > 0xFFFF) return 0;
  Publish m;
  m.topic = _pendTopic;
  m.topic_len = (uint16_t)tn;
  m.payload = body;
  m.len = len;
  m.qos = _pendQos;
  m.retain = _pendRetain;
  m.id = m.qos ? next_id() : 0;
  uint8_t* p = body - ahead_of(tn, m.qos);
  size_t n = EGMqttWire::publish(p, (size_t)(body - p) + len, m);
  if (!n) return 0;
  commit(m.qos == 0 ? K_ONCE : m.qos == 1 ? K_PUB1 : K_PUB2, m.id, n);
  flush();
  return m.qos ? m.id : 1;
}

uint16_t EGMqtt::subscribe(const char* filter, uint8_t qos) {
  if (_phase != UP) return 0;
  uint16_t id = next_id();
  size_t cap;
  uint8_t* p = tail(&cap, true);
  size_t n = p ? EGMqttWire::subscribe(p, cap, id, filter, qos) : 0;
  if (!n) return 0;
  commit(K_SUB, id, n);
  flush();
  return id;
}

uint16_t EGMqtt::unsubscribe(const char* filter) {
  if (_phase != UP) return 0;
  uint16_t id = next_id();
  size_t cap;
  uint8_t* p = tail(&cap, true);
  size_t n = p ? EGMqttWire::unsubscribe(p, cap, id, filter) : 0;
  if (!n) return 0;
  commit(K_UNSUB, id, n);
  flush();
  return id;
}

// Receiving

void EGMqtt::receive() {
  for (uint8_t i = 0; i < PACKETS_A_LOOP && (_phase == HELLO || _phase == UP); i++) {
    if (_rx == R_HEAD && (!acks_fit() || !head())) return;
    if (_rx == R_BODY) body();
    else if (_rx == R_SKIP) skip();
    if (_rx != R_HEAD) return;   // waits for more bytes
  }
}

// a byte at a time, it's 2-5 of them
bool EGMqtt::head() {
  while (_tcp.available()) {
    _tcp.read(_hdr + _hdrLen++, 1);
    int h = header(_hdr, _hdrLen, &_first, &_rem);
    if (h < 0) {
      end(PROTOCOL);
      return false;
    }
    if (h > 0) {
      _hdrLen = 0;
      _got = 0;
      _bigTopic = _bigId = 0;
      _rx = _rem <= IN_CAP ? R_BODY : R_SKIP;
      return true;
    }
  }
  return false;
}

void EGMqtt::body() {
  if (_got < _rem) _got += (uint32_t)_tcp.read(_m->in + _got, _rem - _got);
  if (_got < _rem) return;
  _rx = R_HEAD;
  handle();
}

// too big: thrown away, its topic length and id picked out on the way
void EGMqtt::skip() {
  while (_got < _rem) {
    size_t want = _rem - _got < IN_CAP ? _rem - _got : IN_CAP;
    size_t n = _tcp.read(_m->in, want);
    if (!n) return;
    for (size_t i = 0; i < n; i++) pick(_got + (uint32_t)i, _m->in[i]);
    _got += (uint32_t)n;
  }
  _rx = R_HEAD;
  handle();
}

// bytes 0-1 the topic length, the id the 2 after the topic
void EGMqtt::pick(uint32_t at, uint8_t b) {
  if (at < 2) {
    _bigTopic = (uint16_t)((_bigTopic << 8) | b);
    return;
  }
  uint32_t id_at = 2 + (uint32_t)_bigTopic;
  if (at == id_at || at == id_at + 1) _bigId = (uint16_t)((_bigId << 8) | b);
}

void EGMqtt::handle() {
  _rxMs = millis();
  Type t = type_of(_first);
  if (_phase == HELLO) {
    if (t == CONNACK) on_connack();
    else end(PROTOCOL);
    return;
  }
  switch (t) {
    case PUBLISH:
      on_publish();
      break;
    case PINGRESP:
      if (parse_bare(_first, _rem)) _pingOut = false;
      else end(PROTOCOL);
      break;
    case PUBACK: case PUBREC: case PUBREL: case PUBCOMP: case SUBACK: case UNSUBACK:
      on_ack();
      break;
    default:
      end(PROTOCOL);   // a second CONNACK, or one only a broker gets
  }
}

void EGMqtt::on_connack() {
  bool session;
  uint8_t code;
  if (!parse_connack(_first, _m->in, _rem, &session, &code) || code > NOT_AUTHORIZED) {
    end(PROTOCOL);
    return;
  }
  if (code) {
    end((int8_t)code);
    return;
  }
  _session = session;
  _phase = UP;
  _state = CONNECTED;
  _upNew = true;
  _pingOut = _pingSent = false;
  _txMs = _rxMs = millis();
  if (!session) {
    memset(_q2, 0, sizeof(_q2));   // a new session, any id is a new message
    settle();
  }
  rewind();
}

// too big: acked unread and lost, dropped() counts
void EGMqtt::on_publish() {
  Publish p;
  bool big = _rem > IN_CAP;
  bool ok = big ? publish_head(_first, _bigTopic, _bigId, _rem)
                : parse_publish(_first, _m->in, _rem, &p);
  if (!ok) {
    end(PROTOCOL);
    return;
  }
  if (big) {
    p.qos = (_first >> 1) & 3;
    p.id = _bigId;
  }
  // QoS 2 once only: a resend of a known id is acked, not delivered
  if (p.qos == 2 && q2_known(p.id)) {
    queue(PUBREC, p.id);
    return;
  }
  if (big) _dropped++;
  else deliver(p);
  if (p.qos == 1) queue(PUBACK, p.id);
  else if (p.qos == 2) queue(PUBREC, p.id);
}

void EGMqtt::deliver(const Publish& p) {
  // the topic moves over its length field, so it can end in a NUL
  memmove(_m->in, p.topic, p.topic_len);
  _m->in[p.topic_len] = '\0';
  _m->in[_rem] = '\0';
  Message m = { (const char*)_m->in, p.payload, p.len, p.qos, p.retain, p.dup };
  if (_onMsg) _onMsg(_onMsgArg, m);
}

void EGMqtt::on_ack() {
  Type t = type_of(_first);
  uint16_t id;
  uint8_t granted = 0;
  bool ok = t == SUBACK ? parse_suback(_first, _m->in, _rem, &id, &granted)
                        : parse_id(_first, _m->in, _rem, &id);
  if (!ok) {
    end(PROTOCOL);
    return;
  }
  switch (t) {
    case PUBACK:   done(K_PUB1, id, ACK_PUBLISH, 1); break;
    case PUBCOMP:  done(K_REL, id, ACK_PUBLISH, 2); break;
    case SUBACK:   done(K_SUB, id, ACK_SUBSCRIBE, granted); break;
    case UNSUBACK: done(K_UNSUB, id, ACK_UNSUBSCRIBE, 0); break;
    case PUBREC: {
      Rec* r = find(K_PUB2, id);
      if (r) r->sent = S_DONE;
      if (!find(K_REL, id)) queue(PUBREL, id);   // a repeat gets the queued one
      break;
    }
    default:   // PUBREL
      q2_drop(id);
      queue(PUBCOMP, id);
  }
}

// done before the callback, which may publish and move the outbox
void EGMqtt::done(Kind k, uint16_t id, AckOf of, uint8_t granted) {
  Rec* r = find(k, id);
  if (!r) return;   // unknown id, or a repeat
  r->sent = S_DONE;
  note(id, of, granted);
}

// true = already had it; full = forgotten, a resend would be delivered again
bool EGMqtt::q2_known(uint16_t id) {
  uint16_t* free_slot = nullptr;
  for (uint16_t& s : _q2) {
    if (s == id) return true;
    if (!s && !free_slot) free_slot = &s;
  }
  if (free_slot) *free_slot = id;
  return false;
}

void EGMqtt::q2_drop(uint16_t id) {
  for (uint16_t& s : _q2) {
    if (s == id) s = 0;
  }
}

void EGMqtt::note(uint16_t id, AckOf of, uint8_t granted) {
  if (!_onAck) return;
  Ack a = { id, of, granted };
  _onAck(_onAckArg, a);
}
