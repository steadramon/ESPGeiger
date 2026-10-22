/*
  EGTcp.cpp - the owner's side: ready list, callbacks, client and server.

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
#include "EGTcpPort.h"

#include <string.h>
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#endif

// clients per poll(), the loop keeps going
#ifndef EGTCP_BATCH
#define EGTCP_BATCH 16
#endif

// window updates under this wait for more
#define EGTCP_CREDIT_MIN 512

std::atomic<bool> EGTcp::pending{false};

static uint32_t default_millis() {
#ifdef ARDUINO
  return millis();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

static EGTcp::MillisFn s_clock = default_millis;

void EGTcp::setClock(MillisFn fn) {
  s_clock = fn ? fn : default_millis;
}

uint32_t EGTcp::now() {
  return s_clock();
}

uint32_t EGTcpCore::now() {
  return s_clock();
}

EGTcpLock& EGTcpCore::lock() {
  static EGTcpLock l;
  return l;
}

/*
 * Ready list: the stack adds, the owner takes
 */

static EGTcpClient* s_head = nullptr;
static EGTcpClient* s_tail = nullptr;
static EGTcpClient* s_running = nullptr;   // whose callback, ~EGTcpClient clears it

void EGTcpCore::post(EGTcpClient* c, uint16_t ev) {
  c->_ev |= ev;
  if (!c->_queued) {
    c->_queued = true;
    c->_next = nullptr;
    if (s_tail) s_tail->_next = c;
    else s_head = c;
    s_tail = c;
  }
  EGTcp::pending.store(true, std::memory_order_release);
}

struct Taken {
  uint16_t ev;
  int8_t err;
  uint32_t acked;
  uint32_t dns;
  bool found;
  EGTcpAddr addr;
};

struct EGTcpRun {
  // lock held
  static void unqueue(EGTcpClient* c) {
    c->_ev = 0;
    if (!c->_queued) return;
    EGTcpClient* prev = nullptr;
    for (EGTcpClient* p = s_head; p; prev = p, p = p->_next) {
      if (p != c) continue;
      if (prev) prev->_next = c->_next;
      else s_head = c->_next;
      if (s_tail == c) s_tail = prev;
      break;
    }
    c->_queued = false;
    c->_next = nullptr;
  }

  static EGTcpClient* take(Taken& t) {
    EGTcpHold h(EGTcpCore::lock());
    EGTcpClient* c = s_head;
    if (!c) return nullptr;
    s_head = c->_next;
    if (!s_head) s_tail = nullptr;
    c->_next = nullptr;
    c->_queued = false;
    t.ev = c->_ev;
    t.err = c->_ev_err;
    t.acked = c->_ev_acked;
    t.dns = c->_ev_dns;
    t.found = c->_ev_found;
    t.addr = c->_ev_addr;
    c->_ev = 0;
    c->_ev_acked = 0;
    return c;
  }

  // accepts for s, never handed over, lock held
  static EGTcpClient* unqueue_accepts(EGTcpServer* s) {
    EGTcpClient* gone = nullptr;
    EGTcpClient* p = s_head;
    while (p) {
      EGTcpClient* n = p->_next;
      if (p->_srv == s) {
        unqueue(p);
        p->_accept_next = gone;
        gone = p;
      }
      p = n;
    }
    return gone;
  }

  /*
   * Dispatch, owner's task
   */

  // false: never seen, or deleted in onAccept
  static bool hand_over(EGTcpClient* c, uint16_t ev) {
    EGTcpServer* s = c->_srv;
    c->_srv = nullptr;
    if ((ev & EV_ERROR) || !s) {
      delete c;
      return false;
    }
    if (!s->_on_accept) {
      EGTcpClient** p = &s->_pull;
      while (*p) p = &(*p)->_accept_next;
      *p = c;
      return true;
    }
    s_running = c;
    s->_on_accept(c);
    return s_running == c;
  }

  static void step(EGTcpClient* c, uint16_t bit, const Taken& t) {
    switch (bit) {
      case EV_DNS:       c->resolved(t.found, t.addr, t.dns); break;
      case EV_CONNECTED: c->opened(); break;
      case EV_RX:        c->delivered(); break;
      case EV_SENT:      c->acked(t.acked); break;
      case EV_POLL:      c->timers(); break;
      case EV_FIN:       c->ended(0); break;
      case EV_ERROR:     c->ended(t.err); break;
      default: break;
    }
  }

  static void dispatch(EGTcpClient* c, const Taken& t) {
    if ((t.ev & EV_ACCEPT) && !hand_over(c, t.ev)) return;
    s_running = c;
    uint8_t life = c->_life;
    for (uint16_t bit = EV_DNS; bit <= EV_ERROR && s_running == c && c->_life == life; bit <<= 1) {
      if (t.ev & bit) step(c, bit, t);
    }
    s_running = nullptr;
  }
};

void EGTcp::run() {
  static bool in_run = false;
  if (in_run) return;   // a callback polled
  in_run = true;
  EGTcpCore::owner();
  // before the drain, a post after sets it again
  pending.store(false, std::memory_order_relaxed);
  Taken t;
  for (uint8_t n = 0; n < EGTCP_BATCH; n++) {
    EGTcpClient* c = EGTcpRun::take(t);
    if (!c) {
      in_run = false;
      return;
    }
    EGTcpRun::dispatch(c, t);
  }
  pending.store(true, std::memory_order_relaxed);   // more next pass
  in_run = false;
}

/*
 * EGTcpAddr
 */

bool EGTcpAddr::any() const {
  for (uint8_t i = 0; i < 16; i++) {
    if (b[i]) return false;
  }
  return true;
}

bool EGTcpAddr::operator==(const EGTcpAddr& o) const {
  return v6 == o.v6 && memcmp(b, o.b, 16) == 0;
}

#ifdef ARDUINO
EGTcpAddr::EGTcpAddr(const IPAddress& ip) {
  b[0] = ip[0]; b[1] = ip[1]; b[2] = ip[2]; b[3] = ip[3];
}

IPAddress EGTcpAddr::ip() const {
  if (v6) return IPAddress();
  return IPAddress(b[0], b[1], b[2], b[3]);
}
#endif

/*
 * EGTcpClient
 */

EGTcpClient::EGTcpClient() : _ops(egtcp_ops) {}

EGTcpClient::~EGTcpClient() {
  if (s_running == this) s_running = nullptr;   // its callback stops here
  forget(false);
}

// no stack link, no events, no data
void EGTcpClient::forget(bool rst) {
  if (_h.load()) {
    if (rst) _ops->abort(this);
    else _ops->close(this);
  }
  {
    EGTcpHold h(EGTcpCore::lock());
    EGTcpRun::unqueue(this);
    if (_ask) _ask->c = nullptr;   // answer goes nowhere
    _ask = nullptr;
    _ops->rx_clear(this);
  }
  _owed = _credit = _unacked = 0;
  _ack_later = false;
  _life++;
}

bool EGTcpClient::dial(const EGTcpAddr& a, uint16_t port) {
  int8_t e = _ops->connect(this, a, port);
  if (e == 0) return true;
  _state = CLOSED;
  _err = e;
  return false;
}

bool EGTcpClient::connect(const EGTcpAddr& a, uint16_t port) {
  if (_state == CONNECTING || _state == OPEN) return false;
  EGTcpCore::check();
  forget(false);
  _err = 0;
  _t_start = EGTcpCore::now();
  _state = CONNECTING;
  return dial(a, port);
}

bool EGTcpClient::connect(const char* host, uint16_t port) {
  if (!host || _state == CONNECTING || _state == OPEN) return false;
  EGTcpCore::check();
  forget(false);
  _err = 0;
  _port = port;
  _dns_id++;
  _t_start = EGTcpCore::now();
  _state = CONNECTING;
  EGTcpAddr a;
  int8_t e = _ops->resolve(this, host, &a);
  if (e == 0) return dial(a, port);   // cached
  if (e == EGTCP_LATER) return true;
  _state = CLOSED;
  _err = E_DNS;
  return false;
}

void EGTcpClient::close() {
  // else lwIP sends RST over unread data
  if (_h.load()) credit(_owed + _rx_len.load(), true);
  forget(false);
  _state = IDLE;
  _err = 0;
}

void EGTcpClient::abort() {
  forget(true);
  _state = IDLE;
  _err = 0;
}

void EGTcpClient::shutdown() {
  if (_state == OPEN) _ops->shutdown(this);
}

size_t EGTcpClient::space() const {
  return connected() ? _snd.load(std::memory_order_relaxed) : 0;
}

size_t EGTcpClient::write(const void* data, size_t len, uint8_t flags) {
  if (!data || !len || !space()) return 0;   // full costs no trip
  EGTcpCore::check();
  size_t n = _ops->write(this, data, len, flags);
  if (n) {
    _unacked += n;
    _t_tx = EGTcpCore::now();
  }
  return n;
}

size_t EGTcpClient::write(const char* s) {
  return s ? write(s, strlen(s)) : 0;
}

size_t EGTcpClient::available() const {
  return _rx_len.load(std::memory_order_acquire);
}

size_t EGTcpClient::read(void* out, size_t len) {
  uint8_t* dst = (uint8_t*)out;
  size_t got = 0;
  while (got < len) {
    EGTcpHold h(EGTcpCore::lock());
    size_t n = 0;
    const uint8_t* d = _ops->rx_peek(this, &n);
    if (!d) break;
    if (n > len - got) n = len - got;
    memcpy(dst + got, d, n);
    _ops->rx_drop(this, n);
    got += n;
  }
  credit(got, available() == 0);
  return got;
}

int EGTcpClient::read() {
  uint8_t b;
  return read(&b, 1) ? b : -1;
}

int EGTcpClient::peek() {
  EGTcpHold h(EGTcpCore::lock());
  size_t n = 0;
  const uint8_t* d = _ops->rx_peek(this, &n);
  return d ? *d : -1;
}

void EGTcpClient::ack(size_t len) {
  if (len > _owed) len = _owed;
  _owed -= len;
  credit(len, true);
}

// batched, lwIP only updates the window past a threshold anyway
void EGTcpClient::credit(size_t n, bool flush) {
  _credit += n;
  if (!_h.load()) {
    _credit = 0;
    return;
  }
  if (!_credit || (!flush && _credit < EGTCP_CREDIT_MIN)) return;
  _ops->recved(this, _credit);
  _credit = 0;
}

void EGTcpClient::setNoDelay(bool on) {
  if (_h.load()) _ops->nodelay(this, on);
}

void EGTcpClient::setKeepAlive(uint32_t idle_ms, uint8_t count) {
  if (_h.load()) _ops->keepalive(this, idle_ms, count);
}

EGTcpAddr EGTcpClient::remoteAddr() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i.remote;
}

uint16_t EGTcpClient::remotePort() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i.remote_port;
}

EGTcpAddr EGTcpClient::localAddr() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i.local;
}

uint16_t EGTcpClient::localPort() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i.local_port;
}

uint16_t EGTcpClient::mss() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i.mss;
}

// what happened, owner's task

void EGTcpClient::resolved(bool ok, const EGTcpAddr& a, uint32_t id) {
  if (id != _dns_id || _state != CONNECTING || _h.load()) return;
  if (!ok) {
    ended(E_DNS);
    return;
  }
  int8_t e = _ops->connect(this, a, _port);
  if (e) ended(e);
}

void EGTcpClient::opened() {
  if (_state != CONNECTING || !_h.load()) return;   // an error took it
  _state = OPEN;
  _t_rx = _t_tx = EGTcpCore::now();
  if (_on_connect) _on_connect();
}

void EGTcpClient::delivered() {
  _t_rx = EGTcpCore::now();
  if (!_on_data) return;   // pull, it waits
  for (;;) {
    size_t n = 0;
    const uint8_t* d;
    {
      EGTcpHold h(EGTcpCore::lock());
      d = _ops->rx_peek(this, &n);
    }
    if (!d) return;
    // only the owner drops, d holds
    _ack_later = false;
    _on_data(d, n);
    if (s_running != this || _state == IDLE) return;   // deleted or closed
    {
      EGTcpHold h(EGTcpCore::lock());
      _ops->rx_drop(this, n);
    }
    if (_ack_later) _owed += n;
    else credit(n, available() == 0);
  }
}

void EGTcpClient::acked(uint32_t n) {
  _unacked = n > _unacked ? 0 : _unacked - n;
  _t_rx = EGTcpCore::now();
  if (_on_sent) _on_sent(n);
}

static bool late(uint32_t now, uint32_t since, uint32_t limit) {
  return limit && now - since >= limit;
}

bool EGTcpClient::due(uint32_t now) const {
  uint32_t heard = (int32_t)(_t_rx - _t_tx) > 0 ? _t_rx : _t_tx;
  return (_state == CONNECTING && late(now, _t_start, _to_connect)) ||
         (_state == OPEN && late(now, heard, _to_idle)) ||
         (_unacked && late(now, _t_tx, _to_ack));
}

void EGTcpClient::timers() {
  if (!due(EGTcpCore::now())) return;
  if (_h.load()) _ops->abort(this);
  ended(E_TIMEOUT);
}

// once: the peer, the stack or a timer ended it
void EGTcpClient::ended(int8_t err) {
  if (_state != CONNECTING && _state != OPEN) return;
  if (_h.load()) {
    credit(_owed + available(), true);   // FIN, not RST; pull keeps the data
    _ops->close(this);
  }
  _state = CLOSED;
  if (!_err) _err = err;
  if (_on_close) _on_close(err);
}

/*
 * EGTcpServer
 */

EGTcpServer::~EGTcpServer() {
  end();
}

bool EGTcpServer::begin(uint16_t port, const EGTcpAddr& addr) {
  if (_h.load()) return false;
  EGTcpCore::check();
  _port = port;
  return egtcp_ops->listen(this, addr, port, _max);
}

EGTcpClient* EGTcpServer::accept() {
  EGTcpClient* c = _pull;
  if (c) {
    _pull = c->_accept_next;
    c->_accept_next = nullptr;
  }
  return c;
}

// accepts not handed over go
void EGTcpServer::end() {
  if (_h.load()) egtcp_ops->unlisten(this);
  EGTcpClient* gone;
  {
    EGTcpHold h(EGTcpCore::lock());
    gone = EGTcpRun::unqueue_accepts(this);
  }
  while (gone) {
    EGTcpClient* n = gone->_accept_next;
    delete gone;
    gone = n;
  }
  while (EGTcpClient* c = accept()) delete c;
}
