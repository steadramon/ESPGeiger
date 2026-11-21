/*
  EGHttpServer.cpp - Main-loop-dispatched HTTP/1.1 server on EGTcp.

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
#include "EGHttpServer.h"
#include <EGClock.h>
#include "EGHttpForm.h"
#include "EGHttpHeaders.h"
#include "RespParse.h"
#include <EGStr.h>
#include <string.h>
#include <stdlib.h>
#include <new>

static const char CL0_CRLF[]      PROGMEM = "Content-Length: 0\r\n";
static const char CLOSE_NOSNIFF[] PROGMEM = "Connection: close\r\nX-Content-Type-Options: nosniff\r\n";
static const char HEAD_OK[]       PROGMEM =
  "HTTP/1.1 %u \r\nContent-Type: %s\r\n%sContent-Length: %u\r\n%s%s\r\n";
static const char GZIP_CRLF[]     PROGMEM = "Content-Encoding: gzip\r\n";
static const char CHUNK_HEAD[]    PROGMEM = "%X\r\n";
static const char CRLF[]          PROGMEM = "\r\n";
static const char M_GET[]         PROGMEM = "GET";
static const char M_POST[]        PROGMEM = "POST";

static const char R_HTTP[]  PROGMEM = "HTTP/1.1 ";
static const char R_CLOSE[] PROGMEM = "\r\nContent-Length: 0\r\nConnection: close\r\n";
static const char R_RETRY[] PROGMEM = "Retry-After: ";
static const char S400[] PROGMEM = "400 Bad Request";
static const char S403[] PROGMEM = "403 Forbidden";
static const char S413[] PROGMEM = "413 Payload Too Large";
static const char S501[] PROGMEM = "501 Not Implemented";
static const char S503[] PROGMEM = "503 Service Unavailable";

// Canned replies never yield, they go in one write or are dropped
static const size_t STATUS_MAX = 96;
static_assert(sizeof(R_HTTP) + sizeof(S503) + sizeof(R_CLOSE) + sizeof(R_RETRY) + 2 * sizeof(CRLF) <= STATUS_MAX,
              "canned reply over STATUS_MAX");

static size_t put_P(char* buf, size_t n, PGM_P p) {
  size_t l = strlen_P(p);
  memcpy_P(buf + n, p, l);
  return n + l;
}

// retry_s is one digit, 0 = no Retry-After
static size_t canned(char* buf, PGM_P status, uint8_t retry_s) {
  size_t n = put_P(buf, 0, R_HTTP);
  n = put_P(buf, n, status);
  n = put_P(buf, n, R_CLOSE);
  if (retry_s) {
    n = put_P(buf, n, R_RETRY);
    buf[n++] = '0' + retry_s;
    n = put_P(buf, n, CRLF);
  }
  return put_P(buf, n, CRLF);
}

static void write_status(EGTcpClient* c, PGM_P status, uint8_t retry_s = 0) {
  char buf[STATUS_MAX];
  if (!c || !status) return;
  size_t len = canned(buf, status, retry_s);
  if (c->space() < len) return;
  c->write(buf, len);
}

// Main loop only, it runs under the 3.2 s wdt
#ifdef ESP32
  // not vTaskDelay, that sleeps a tick
  #define EGHTTP_BACKPRESSURE_PAUSE() do { taskYIELD(); feedLoopWDT(); } while (0)
#else
  #define EGHTTP_BACKPRESSURE_PAUSE() do { yield(); delay(0); } while (0)
#endif

#ifdef EGHTTP_DEBUG
  #define EGHTTP_TRACE(...) Serial.printf_P(__VA_ARGS__)
#else
  #define EGHTTP_TRACE(...) do {} while (0)
#endif

EGHttpServer::LogFn EGHttpServer::s_log = nullptr;

static void emit(EGHttpServer::LogFn log, PGM_P fmt, va_list ap) {
  if (log) {
    log(fmt, ap);
    return;
  }
  char line[96];
  vsnprintf_P(line, sizeof(line), fmt, ap);
  Serial.printf_P(PSTR("%s\n"), line);
}

static void emitf(EGHttpServer::LogFn log, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(log, fmt, ap);
  va_end(ap);
}

// At most a line a second, the rest are counted
static bool s_said = false;
static uint32_t s_said_ms = 0;
static uint16_t s_unsaid = 0;

void EGHttpServer::say(PGM_P fmt, ...) {
  uint32_t now = EGClock::now();
  if (s_said && now - s_said_ms < EGHTTP_SAY_GAP_MS) {
    if (s_unsaid < UINT16_MAX) s_unsaid++;
    return;
  }
  s_said = true;
  s_said_ms = now;
  if (s_unsaid) emitf(s_log, PSTR("[EGHttp] %u more not shown"), (unsigned)s_unsaid);
  s_unsaid = 0;
  va_list ap;
  va_start(ap, fmt);
  emit(s_log, fmt, ap);
  va_end(ap);
}

// true once past s->due, wrap safe
static bool eghttp_late(const EGHttpServer::Slot* s) {
  return (int32_t)(EGClock::now() - s->due) >= 0;
}

// The whole request is in. It's waiting on the server now, not the peer, so no idle timeout
static void set_ready(EGHttpServer::Slot* s) {
  s->state = EGHttpServer::READY;
  if (s->client) s->client->setIdleTimeout(0);
}

__attribute__((warn_unused_result))
static bool send_fail(EGHttpServer::Slot* s, PGM_P why, size_t sent, size_t len) {
  EGHTTP_TRACE(PSTR("[EGHttp] send aborted, %s at %u/%u B\n"), why, (unsigned)sent, (unsigned)len);
  (void)why; (void)sent; (void)len;
  s->send_aborted = true;
  return false;
}

static bool yield_write_sram(EGHttpServer::Slot* s, const char* data, size_t len) {
  if (!s || s->send_aborted) return false;
  size_t sent = 0;
  uint32_t last_ms = EGClock::now();
  while (sent < len) {
    if (s->send_aborted) return false;
    if (!s->client || !s->client->connected()) return send_fail(s, PSTR("connection lost"), sent, len);
    // Acks that trickle in a little at a time would still hold the loop
    if (eghttp_late(s)) return send_fail(s, PSTR("past deadline"), sent, len);
    size_t can = s->client->space();
    size_t take = (len - sent) < can ? (len - sent) : can;
    // write() already sends
    size_t wrote = take ? s->client->write(data + sent, take) : 0;
    if (wrote == 0) {
      if (EGClock::now() - last_ms > EGHTTP_ACK_MS) return send_fail(s, PSTR("stalled"), sent, len);
      EGHTTP_BACKPRESSURE_PAUSE();
      continue;
    }
    sent += wrote;
    last_ms = EGClock::now();
    yield();
  }
  return true;
}

static bool yield_write_pgm(EGHttpServer::Slot* s, PGM_P data, size_t len) {
  char chunk[512];
  size_t off = 0;
  while (off < len) {
    size_t take = (len - off) > sizeof(chunk) ? sizeof(chunk) : (len - off);
    memcpy_P(chunk, data + off, take);
    if (!yield_write_sram(s, chunk, take)) return false;
    off += take;
  }
  return true;
}

static const size_t TAIL_PART = 512;
#if EGHTTP_PULL
static const size_t PULL_STEP = 1024;   // about one segment, a power of 2 so no divide
#endif

// Cuts n to what goes out. nullptr if the puller came up short
static const char* tail_part(EGHttpServer::Slot* s, char* b, size_t& n) {
#if EGHTTP_PULL
  if (s->pull) {
    if (n > TAIL_PART) n = TAIL_PART;
    n = s->pull(s->pullCtx, s->pullAt, b, n);
    return n ? b : nullptr;
  }
#endif
#ifdef ESP8266
  // flash isn't byte readable here
  if (n > TAIL_PART) n = TAIL_PART;
  memcpy_P(b, s->tail, n);
  return b;
#else
  (void)b;
  return s->tail;
#endif
}

static void tail_sent(EGHttpServer::Slot* s, size_t w) {
  s->tailLen -= w;
#if EGHTTP_PULL
  if (s->pull) {
    // Each KB sent buys more time, so a big file gets through but a trickle is cut
    if (s->pullAt / PULL_STEP != (s->pullAt + w) / PULL_STEP) s->due = EGClock::now() + EGHTTP_SEND_MS;
    s->pullAt += w;
    return;
  }
#endif
  s->tail += w;
}

// sends what fits now, onSent sends the rest
static void send_tail(EGHttpServer::Slot* s) {
  while (s->tailLen && s->client) {
    size_t can = s->client->space();
    size_t n = s->tailLen < can ? s->tailLen : can;
    if (!n) return;
    char b[TAIL_PART];
    const char* from = tail_part(s, b, n);
    if (!from) {
      s->send_aborted = true;   // shorter than its Content-Length
      return;
    }
    uint8_t f = EGTcpClient::COPY;
    if (n < s->tailLen && n < can) f |= EGTcpClient::MORE;
    size_t w = s->client->write(from, n, f);
    if (!w) return;
    tail_sent(s, w);
  }
}

// A cut head would have no blank line, so send nothing
static inline bool eghttp_head_fits(int n, size_t cap) {
  return n > 0 && (size_t)n < cap;
}

static bool eghttp_parse_method(const char* name, EGHttpRequest::Method* method) {
  if      (strcmp_P(name, M_GET)  == 0) *method = EGHttpRequest::GET;
  else if (strcmp_P(name, M_POST) == 0) *method = EGHttpRequest::POST;
  else return false;
  return true;
}

static char* eghttp_find_crlf(char* buf, size_t end) {
  for (size_t i = 0; i + 1 < end; i++) {
    if (buf[i] == '\r' && buf[i+1] == '\n') return buf + i;
  }
  return nullptr;
}

// 0 = incomplete, from = bytes already looked at
static size_t eghttp_find_header_end(const char* buf, size_t len, size_t from) {
  for (size_t i = from > 3 ? from : 3; i < len; i++) {
    if (buf[i-3] == '\r' && buf[i-2] == '\n' &&
        buf[i-1] == '\r' && buf[i]   == '\n') {
      return i + 1;
    }
  }
  return 0;
}

// EGHttpServer

static const uint8_t ROUTE_STEP = 16;   // grow by this, the cap stays below NO_ROUTE

bool EGHttpServer::growRoutes() {
  if (_routeCap > NO_ROUTE - ROUTE_STEP) return false;
  uint8_t cap = _routeCap + ROUTE_STEP;
  Route* r = (Route*)realloc(_routes, cap * sizeof(Route));
  if (!r) return false;
  _routes = r;
  _routeCap = cap;
  return true;
}

// before begin() only
bool EGHttpServer::on(const __FlashStringHelper* path, EGHttpRequest::Method method, Handler h, void* user) {
  if (_server || (_routeCount == _routeCap && !growRoutes())) {
    say(PSTR("[EGHttp] route not added: %s"), (PGM_P)path);
    return false;
  }
  _routes[_routeCount++] = { (PGM_P)path, method, h, user };
  return true;
}

// before begin(), after on()
bool EGHttpServer::onUpload(const __FlashStringHelper* path, BodyHandler bh, void* user) {
  if (_server || _uploadCount == EGHTTP_MAX_UPLOADS) {
    say(PSTR("[EGHttp] upload not added: %s"), (PGM_P)path);
    return false;
  }
  for (uint8_t i = 0; i < _routeCount; i++) {
    Route& r = _routes[i];
    if (r.method == EGHttpRequest::POST && streq_PP(r.path, (PGM_P)path)) {
      _uploads[_uploadCount++] = { i, bh, user };
      return true;
    }
  }
  say(PSTR("[EGHttp] onUpload: no POST route for %s"), (PGM_P)path);
  return false;
}

const EGHttpServer::Upload* EGHttpServer::uploadFor(uint8_t route) const {
  for (uint8_t i = 0; i < _uploadCount; i++) {
    if (_uploads[i].route == route) return &_uploads[i];
  }
  return nullptr;
}

void EGHttpServer::onNotFound(Handler h, void* user) {
  _notFound = h;
  _notFoundUser = user;
}

void EGHttpServer::setBasicAuth(const __FlashStringHelper* user, const char* password,
                                  const __FlashStringHelper* realm) {
  _authEnabled = false;
  _authValue[0] = '\0';
  if (!user || !password || !*password) return;
  char raw[EGHTTP_AUTH_MAX + 1];
  int rlen = snprintf_P(raw, sizeof(raw), PSTR("%s:%s"), (PGM_P)user, password);
  if (rlen <= 0 || (size_t)rlen >= sizeof(raw)) return;
  // rlen <= EGHTTP_AUTH_MAX, it fits with the NUL
  encode_base64((const unsigned char*)raw, (unsigned int)rlen, (unsigned char*)_authValue);
  if (realm && pgm_read_byte((PGM_P)realm)) {
    strncpy_P(_authRealm, (PGM_P)realm, sizeof(_authRealm) - 1);
    _authRealm[sizeof(_authRealm) - 1] = '\0';
  }
  _authEnabled = true;
}

void EGHttpServer::begin(uint16_t port) {
  if (_server) return;
  // Give back the spare route room
  if (_routeCount && _routeCount < _routeCap) {
    Route* r = (Route*)realloc(_routes, _routeCount * sizeof(Route));
    if (r) {
      _routes = r;
      _routeCap = _routeCount;
    }
  }
  _server = new (std::nothrow) EGTcpServer();
  if (!_server) {
    say(PSTR("[EGHttp] no memory for the server"));
    return;
  }
#ifdef ESP32
  _server->setMaxClients(EGHTTP_MAX_INFLIGHT);
#endif
  _server->onAccept([](void* p, EGTcpClient* c) { ((EGHttpServer*)p)->onClient(c); }, this);
  if (_server->begin(port)) return;
  say(PSTR("[EGHttp] listen on %u failed, retrying"), (unsigned)port);
  _tickWanted = true;
}

// If there was no memory to listen at begin(), try once a second until it does
void EGHttpServer::relisten() {
  if (!_server || _server->listening()) return;
  _tickWanted = true;
  uint32_t now = EGClock::now();
  if ((int32_t)(now - _relistenAt) < 0) return;
  _relistenAt = now + 1000;
  (void)_server->begin(_server->port());   // listening() tells on the next tick
}

bool EGHttpServer::hasActiveClients() const {
  for (auto& s : _slots) {
    if (!s.client) continue;
    if (s.state == RECEIVING || s.state == READY || s.state == RESPONDING) {
      return true;
    }
  }
  return false;
}

void EGHttpServer::end() {
  if (_server) { _server->end(); delete _server; _server = nullptr; }
  for (auto& s : _slots) {
    abortStream(&s);
    resetSlot(&s);
  }
  releaseStream();
#ifdef ESP32
  if (_dispatchChunkAcc) { free(_dispatchChunkAcc); _dispatchChunkAcc = nullptr; }
#endif
}

// nullptr = none, cap 0
static void set_acc(EGHttpServer::Slot* s, char* buf, size_t cap) {
  s->chunkAcc    = buf;
  s->chunkAccLen = 0;
  s->chunkAccCap = buf ? cap : 0;
}

// query is an offset into buf, so it's cleared with it
static void free_buf(EGHttpServer::Slot* s) {
  free(s->buf);
  s->buf   = nullptr;
  s->query = 0;
}

// Fresh slot, keeps buf
static void clear_slot(EGHttpServer::Slot* s) {
  s->len            = 0;
  s->contentLength  = 0;
  s->headerEnd      = 0;
  s->query          = 0;
  s->stream         = EGHttpServer::NO_STREAM;
  s->ack_owed       = 0;
  s->bodyAcked      = 0;
  s->send_aborted   = false;
  s->responded      = false;
  s->routeIdx       = EGHttpServer::NO_ROUTE;
  set_acc(s, nullptr, 0);
  s->tail           = nullptr;
  s->tailLen        = 0;
#if EGHTTP_PULL
  s->pull           = nullptr;
  s->pulled         = nullptr;
  s->pullCtx        = nullptr;
  s->pullAt         = 0;
#endif
}

EGHttpServer::Slot* EGHttpServer::allocSlot(EGTcpClient* c) {
  for (uint8_t i = 0; i < _maxClients; i++) {
    Slot* s = &_slots[i];
    if (s->state != IDLE || s->client) continue;
    clear_slot(s);
    s->client = c;
    s->state  = RECEIVING;
    s->due    = EGClock::now() + EGHTTP_REQ_MS;
    return s;
  }
  return nullptr;
}

// slots the peer at a holds
static uint8_t held_by(const EGHttpServer::Slot* slots, const EGTcpAddr* at, uint8_t n, const EGTcpAddr& a) {
  uint8_t k = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (slots[i].client && at[i] == a) k++;
  }
  return k;
}

// waiting EGHTTP_SHARE_MS or more for its request
static bool waiting_long(const EGHttpServer::Slot* s, uint32_t now) {
  if (!s->client || s->state != EGHttpServer::RECEIVING || s->stream != EGHttpServer::NO_STREAM) return false;
  // due is its accept + EGHTTP_REQ_MS until the request is in
  return (int32_t)(now - (s->due - EGHTTP_REQ_MS)) >= (int32_t)EGHTTP_SHARE_MS;
}

// Full: the oldest slot still waiting for a request, from a peer holding more
// than the newcomer, goes to it. One peer can't keep the rest out
EGHttpServer::Slot* EGHttpServer::shareSlot(EGTcpClient* c) {
  EGTcpAddr at[EGHTTP_MAX_CLIENTS];
  for (uint8_t i = 0; i < _maxClients; i++) {
    if (_slots[i].client) at[i] = _slots[i].client->remoteAddr();
  }
  const uint8_t mine = held_by(_slots, at, _maxClients, c->remoteAddr());
  const uint32_t now = EGClock::now();
  Slot* old = nullptr;
  for (uint8_t i = 0; i < _maxClients; i++) {
    Slot* s = &_slots[i];
    if (!waiting_long(s, now) || held_by(_slots, at, _maxClients, at[i]) <= mine) continue;
    if (!old || (int32_t)(s->due - old->due) < 0) old = s;
  }
  if (!old) return nullptr;
  drop(old);
  reclaimSlot(old);
  return allocSlot(c);
}

void EGHttpServer::markDone(Slot* s) {
  free_buf(s);
  set_acc(s, nullptr, 0);
  s->state    = DONE;
  _tickWanted = true;
}

// The slot keeps the client until tick() reclaims it
void EGHttpServer::sendStatusAndClose(Slot* s, const char* status, uint8_t retry_s) {
  markDone(s);
  if (!s->client) return;
  write_status(s->client, (PGM_P)status, retry_s);
  s->client->close();
}

void EGHttpServer::resetSlot(Slot* s) {
  free_buf(s);
#if EGHTTP_PULL
  // Called once, whether it finished, was cut, the client went or the server stopped
  if (s->pulled) s->pulled(s->pullCtx);
#endif
  clear_slot(s);
  EGTcpClient* c = s->client;
  s->client = nullptr;
  s->state  = IDLE;
  delete c;   // closes it if still open
}

// the handler only hears the abort once
void EGHttpServer::abortStream(Slot* s) {
  if (s->stream != STREAM_OPEN) return;
  s->stream = STREAM_ABORTED;
  deliverBodyEvent(s, BODY_ABORT, nullptr, 0);
}

void EGHttpServer::wireSlotCallbacks(Slot* s, EGTcpClient* c) {
  c->setIdleTimeout(EGHTTP_REQ_MS);
  c->setAckTimeout(EGHTTP_ACK_MS);
  s->srv = this;
  c->onSent([](void* p, size_t) { Slot* s = (Slot*)p; s->srv->sentMore(s); }, s);
  c->onData([](void* p, const uint8_t* data, size_t len) {
    Slot* s = (Slot*)p;
    EGHttpServer* srv = s->srv;
    if (s->state != RECEIVING) return;
    srv->takeData(s, data, len);
    // Still not all in by the deadline, it's a trickle so drop it
    if (s->state == RECEIVING && s->stream == NO_STREAM && eghttp_late(s)) srv->drop(s);
    srv->_tickWanted = true;
  }, s);
  c->onClose([](void* p, int8_t err) {
    Slot* s = (Slot*)p;
#ifdef EGHTTP_DEBUG
    Serial.printf_P(PSTR("[EGHttp] closed err=%d streaming=%d acked=%u/%u\n"),
                    (int)err, (int)(s->stream != NO_STREAM), (unsigned)s->bodyAcked,
                    (unsigned)s->contentLength);
#else
    (void)err;
#endif
    s->srv->onClosed(s);
  }, s);
  // a whole request then FIN (shutdown(SHUT_WR)): still answered, it closes after.
  // Mid answer a FIN is a peer gone
  c->onFin([](void* p) { return ((Slot*)p)->state == READY; }, s);
}

void EGHttpServer::onClient(EGTcpClient* c) {
  _tickWanted = true;
  Slot* s = allocSlot(c);
  if (!s) {
    // one closed this poll, tick() hasn't got to it yet
    for (auto& d : _slots) {
      if (d.state == DONE) reclaimSlot(&d);
    }
    s = allocSlot(c);
  }
  if (!s) s = shareSlot(c);
  if (s) { wireSlotCallbacks(s, c); return; }

  // No free slot. Send a 503, queueing it would drop pbufs
  write_status(c, S503, 1);
  c->close();
  delete c;
}

// Closed by the peer, an error or a timeout. tick() reclaims the slot
void EGHttpServer::onClosed(Slot* s) {
  s->state    = DONE;
  _tickWanted = true;
}

void EGHttpServer::takeStream(Slot* s, const uint8_t* data, size_t len) {
  if (_streamOwner != s || !_streamBuf) return;
  // anything past Content-Length is dropped
  size_t have = s->bodyAcked + _streamLen;
  size_t room = s->contentLength > have ? s->contentLength - have : 0;
  if (len > room) len = room;
  if (!len) return;
  if (_streamLen + len > STREAM_BUF_SIZE) {
    say(PSTR("[EGHttp] stream buf overflow: %u+%u>%u, aborting"),
        (unsigned)_streamLen, (unsigned)len, (unsigned)STREAM_BUF_SIZE);
    sendStatusAndClose(s, nullptr);
    return;
  }
  memcpy(_streamBuf + _streamLen, data, len);
  _streamLen += len;
  s->client->ackLater();
}

static_assert(EGHTTP_REQ_BUF <= 0xFFFF, "Slot::query is 16 bit");

static bool append_req(EGHttpServer::Slot* s, const uint8_t* data, size_t len) {
  if (s->len + len > EGHTTP_REQ_BUF) return false;
  memcpy(s->buf + s->len, data, len);
  s->len += len;
  return true;
}

void EGHttpServer::takeData(Slot* s, const uint8_t* data, size_t len) {
  if (!s->headerEnd) {
    receiveHeaders(s, data, len);
    return;
  }

  if (s->stream != NO_STREAM) {
    takeStream(s, data, len);
    return;
  }

  if (!append_req(s, data, len)) {
    sendStatusAndClose(s, S413);
    return;
  }
  if (s->len < s->headerEnd + s->contentLength) return;
  set_ready(s);
}

void EGHttpServer::receiveHeaders(Slot* s, const uint8_t* data, size_t len) {
  // Allocated on the first byte, not on accept
  if (!s->buf && ESP.getFreeHeap() >= EGHTTP_HEAP_MIN) s->buf = (char*)malloc(EGHTTP_REQ_BUF);
  if (!s->buf) {
    sendStatusAndClose(s, S503, 1);
    return;
  }
  size_t seen = s->len;
  if (!append_req(s, data, len)) {
    say(PSTR("[EGHttp] headers exceed REQ_BUF (%u+%u>%u) - 413"),
        (unsigned)s->len, (unsigned)len, (unsigned)EGHTTP_REQ_BUF);
    sendStatusAndClose(s, S413);
    return;
  }

  size_t headerEnd = eghttp_find_header_end(s->buf, s->len, seen);
  if (!headerEnd) return;
  s->headerEnd = headerEnd;

  if (eghttp_content_length(s->buf, s->headerEnd, &s->contentLength) == EGHTTP_CL_BAD) {
    sendStatusAndClose(s, S400);
    return;
  }

  identifyRoute(s);
  if (uploadFor(s->routeIdx)) {
    beginStream(s, len);
    return;
  }

  // room for the NUL
  if (s->contentLength >= EGHTTP_REQ_BUF - s->headerEnd) {
    sendStatusAndClose(s, S413);
    return;
  }
  if (s->len >= s->headerEnd + s->contentLength) {
    set_ready(s);
  }
}

// Checked once, before any body comes in. Nothing refuses it after this
void EGHttpServer::beginStream(Slot* s, size_t len) {
  EGHttpRequest req;
  readRequest(s, req);   // a route matched, so the line parses
  uint16_t no = refusal(s, req);
  if (no == 401) sendUnauthorized(s);
  if (no) {
    sendStatusAndClose(s, no == 400 ? S400 : no == 403 ? S403 : nullptr);
    return;
  }
  if (_streamOwner) {
    sendStatusAndClose(s, S503, 5);
    return;
  }
  // A small form doesn't need a whole window, takeStream stops at Content-Length
  size_t cap = s->contentLength < STREAM_BUF_SIZE ? s->contentLength : STREAM_BUF_SIZE;
  uint8_t* buf = (uint8_t*)malloc(cap ? cap : 1);
  if (!buf) {
    say(PSTR("[EGHttp] stream buf alloc failed (need %u)"),
        (unsigned)cap);
    sendStatusAndClose(s, S503);
    return;
  }
  size_t inBuf = s->len - s->headerEnd;
  if (inBuf > s->contentLength) inBuf = s->contentLength;
  if (inBuf) memcpy(buf, s->buf + s->headerEnd, inBuf);
  s->len = s->headerEnd;
  // an ack inside onData is clamped to 0, so it's owed until the drain
  s->ack_owed = (uint16_t)(len - inBuf);
  _streamBuf    = buf;
  _streamLen    = inBuf;
  _streamOwner  = s;
  s->stream     = STREAM_WAIT;
  s->due        = EGClock::now() + EGHTTP_REQ_MS;
  s->client->setIdleTimeout(EGHTTP_STREAM_IDLE_MS);
  s->client->ackLater();
}

void EGHttpServer::releaseStream() {
  free(_streamBuf);
  _streamBuf   = nullptr;
  _streamLen   = 0;
  _streamOwner = nullptr;
}

// Writes NULs into s->buf
void EGHttpServer::identifyRoute(Slot* s) {
  char* lineEnd = eghttp_find_crlf(s->buf, s->headerEnd);
  if (!lineEnd) return;
  *lineEnd = '\0';

  char* sp1 = strchr(s->buf, ' ');
  if (!sp1) return;
  *sp1 = '\0';
  EGHttpRequest::Method method;
  if (!eghttp_parse_method(s->buf, &method)) return;

  char* path = sp1 + 1;
  char* sp2 = strchr(path, ' ');
  if (sp2) *sp2 = '\0';
  char* q = strchr(path, '?');
  if (q) {
    *q = '\0';
    s->query = (uint16_t)(q + 1 - s->buf);
  }
  s->routeIdx = findRoute(path, method);
}

// first match wins
uint8_t EGHttpServer::findRoute(const char* path, EGHttpRequest::Method method) const {
  for (uint8_t i = 0; i < _routeCount; i++) {
    const Route& r = _routes[i];
    if ((r.method == EGHttpRequest::ANY || r.method == method) &&
        strcmp_P(path, r.path) == 0) {
      return i;
    }
  }
  return NO_ROUTE;
}

bool EGHttpServer::deliverBodyEvent(Slot* s, BodyEvent ev,
                                     const char* chunk, size_t len) {
  const Upload* u = uploadFor(s->routeIdx);
  if (!u) return true;
  const Route& r = _routes[u->route];
  EGHttpRequest req;
  req._method  = r.method;
  req._path    = r.path;
  req._body    = nullptr;
  req._bodyLen = s->contentLength;
  if (s->query) {   // uploads get the query too, for their flags
    req._query    = s->buf + s->query;
    req._queryLen = strlen(req._query);
  }
  return u->handler(req, ev, chunk, len, u->user);
}

// Reply now, the reply closes the connection
void EGHttpServer::refuseStream(Slot* s) {
  s->stream = STREAM_ENDED;
  set_ready(s);
  releaseStream();
}

// n bytes were handed over, ack those and keep the rest
void EGHttpServer::streamTaken(Slot* s, size_t n) {
  // a second ackLater() loses bytes
  if (s->client) s->client->ack(n);
  size_t was = s->bodyAcked;
  s->bodyAcked += n;
  // A whole window moved, so give it more time
  if (was / STREAM_BUF_SIZE != s->bodyAcked / STREAM_BUF_SIZE) {
    s->due = EGClock::now() + EGHTTP_REQ_MS;
  }
  size_t leftover = (_streamLen > n) ? _streamLen - n : 0;
  if (leftover > 0) {
    memmove(_streamBuf, _streamBuf + n, leftover);
  }
  _streamLen = leftover;
}

void EGHttpServer::drainStream(Slot* s) {
  // Take a snapshot, a handler that polls can add more
  bool mine = s->stream != NO_STREAM && _streamOwner == s;
  size_t toDeliver = _streamLen;
  if (!mine || s->stream == STREAM_ENDED) return;

  if (s->stream == STREAM_WAIT) {
    s->stream = STREAM_OPEN;
    if (!deliverBodyEvent(s, BODY_BEGIN, nullptr, 0)) {
      refuseStream(s);
      return;
    }
  }
  if (s->ack_owed && s->client) {
    s->client->ack(s->ack_owed);
    s->ack_owed = 0;
  }

  if (toDeliver > 0) {
    if (!deliverBodyEvent(s, BODY_DATA, (const char*)_streamBuf, toDeliver)) {
      refuseStream(s);
      return;
    }
    streamTaken(s, toDeliver);
  }

  if (s->bodyAcked >= s->contentLength) {
    s->stream = STREAM_ENDED;
    deliverBodyEvent(s, BODY_END, nullptr, 0);
    set_ready(s);
    releaseStream();
  } else if (eghttp_late(s)) {
    drop(s);
  }
}

void EGHttpServer::claimChunkAcc(Slot* s) {
#ifdef ESP32
  if (!_dispatchChunkAcc) {
    _dispatchChunkAcc = (char*)malloc(EGHTTP_CHUNK_ACC);
  }
  set_acc(s, _dispatchChunkAcc, EGHTTP_CHUNK_ACC);
#else
  // only the unused tail of buf, +1 keeps the body's NUL
  size_t reqUsed = s->headerEnd + (s->stream != NO_STREAM ? 0 : s->contentLength) + 1;
  if (s->buf && reqUsed < EGHTTP_REQ_BUF) set_acc(s, s->buf + reqUsed, EGHTTP_REQ_BUF - reqUsed);
  else                                    set_acc(s, nullptr, 0);
#endif
}

bool EGHttpServer::readRequest(Slot* s, EGHttpRequest& req) {
  char* sp1 = s->buf + strlen(s->buf);
  if (sp1 >= s->buf + s->headerEnd) return false;

  char* path = sp1 + 1;
  if (!memchr(path, '\0', s->buf + s->headerEnd - path)) return false;
  if (s->query) {
    req._query    = s->buf + s->query;
    req._queryLen = strlen(req._query);
  }
  req._path = path;

  req._body    = s->buf + s->headerEnd;
  // also called before beginStream, so ask about the upload here
  bool streamed = uploadFor(s->routeIdx) != nullptr;
  req._bodyLen = streamed ? 0 : s->contentLength;
  if (!streamed && s->headerEnd + req._bodyLen < EGHTTP_REQ_BUF) {
    s->buf[s->headerEnd + req._bodyLen] = '\0';
  }
  return true;
}

// Compares every byte, no early out on the first wrong one
static bool same_bytes(const char* a, const char* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

// p points just past "Authorization:"
bool EGHttpServer::authorized(const Slot* s, const char* p) const {
  const char* hend = s->buf + s->headerEnd;
  while (p < hend && (*p == ' ' || *p == '\t')) p++;
  size_t vlen = strlen(_authValue);
  // Scheme in any case, the token exact, then the CR. Nothing allowed past the padding
  return (size_t)(hend - p) > 6 + vlen &&
         strncasecmp_P(p, PSTR("Basic "), 6) == 0 &&
         same_bytes(p + 6, _authValue, vlen) &&
         p[6 + vlen] == '\r';
}

// Brake for a run of wrong logins. It's shared by every server, and while it's on
// even the right password is refused
static uint8_t s_wrong = 0;
static bool s_braked = false;
static uint32_t s_braked_ms = 0;

void EGHttpServer::setBrake(bool on) {
  s_brake = on;
  s_wrong = 0;
  s_braked = false;
}

// no header is just the browser's first try, not a guess
bool EGHttpServer::loginOk(const Slot* s) const {
  const char* p = eghttp_find_header(s->buf, s->headerEnd, PSTR("Authorization:"));
  if (!p) return false;
  uint32_t now = EGClock::now();
  if (s_braked && now - s_braked_ms < EGHTTP_BRAKE_MS) return false;
  s_braked = false;
  if (authorized(s, p)) {
    s_wrong = 0;
    return true;
  }
  if (s_brake && ++s_wrong >= EGHTTP_BRAKE_TRIES) {
    s_wrong = 0;
    s_braked = true;
    s_braked_ms = now;
    say(PSTR("[EGHttp] %u wrong logins, none taken for %u s"), (unsigned)EGHTTP_BRAKE_TRIES,
        (unsigned)(EGHTTP_BRAKE_MS / 1000));
  }
  return false;
}

// Skips the spaces, n is the value's length up to the CR
static const char* header_value(const char* p, const char* end, size_t* n) {
  while (p < end && *p == ' ') p++;
  size_t k = 0;
  while (p + k < end && p[k] != '\r') k++;
  *n = k;
  return p;
}

// Compares the page's site with the one asked for. Scheme stripped, then host[:port] in any case
bool EGHttpServer::crossSite(const Slot* s) const {
  if (strcmp_P(s->buf, M_POST) != 0) return false;   // the request line was cut after the method
  const char* o = eghttp_find_header(s->buf, s->headerEnd, PSTR("Origin:"));
  if (!o) return false;
  const char* h = eghttp_find_header(s->buf, s->headerEnd, PSTR("Host:"));
  if (!h) return true;
  const char* end = s->buf + s->headerEnd;
  size_t on, hn;
  o = header_value(o, end, &on);
  h = header_value(h, end, &hn);
  size_t skip = 0;
  if (on > 7 && strncasecmp_P(o, PSTR("http://"), 7) == 0) skip = 7;
  else if (on > 8 && strncasecmp_P(o, PSTR("https://"), 8) == 0) skip = 8;
  if (!skip) return true;   // "null" or no scheme
  return on - skip != hn || strncasecmp(o + skip, h, hn) != 0;
}

// A value arg() can't hold would read as absent, so refuse any of those
bool EGHttpServer::argsFit(const Slot* s, const EGHttpRequest& req) const {
  if (!eghttp_form_fits(req._query, req._queryLen, EGHTTP_ARG_MAX + 1)) return false;
  // console text, not a form
  const char* ct = eghttp_find_header(s->buf, s->headerEnd, PSTR("Content-Type:"));
  while (ct && (*ct == ' ' || *ct == '\t')) ct++;
  if (ct && strncasecmp_P(ct, HTTP_CT_PLAIN, 10) == 0) return true;
  return eghttp_form_fits(req._body, req._bodyLen, EGHTTP_ARG_MAX + 1);
}

// Checks 403, then 401, then 400. Used for normal requests and uploads
uint16_t EGHttpServer::refusal(const Slot* s, const EGHttpRequest& req) const {
  if (s_sameOrigin && crossSite(s)) {
    say(PSTR("[EGHttp] POST from another site refused"));
    return 403;
  }
  if (_authEnabled && !loginOk(s)) return 401;
  return argsFit(s, req) ? 0 : 400;
}

static const char HEAD_401[] PROGMEM =
  "HTTP/1.1 401 Unauthorized\r\n"
  "WWW-Authenticate: Basic realm=\"%s\"\r\n"
  "Content-Type: text/plain\r\n"
  "Content-Length: %u\r\n"
  "%s\r\n";

bool EGHttpServer::s_sameOrigin = false;
bool EGHttpServer::s_brake = true;

void EGHttpServer::setAuthHelp(PGM_P text) { _authHelp = text; }

void EGHttpServer::sendUnauthorized(Slot* s) {
  size_t bn = _authHelp ? strlen_P(_authHelp) : 0;
  char head[sizeof(HEAD_401) + sizeof(_authRealm) + 5 + sizeof(CLOSE_NOSNIFF)];   // 5 for the length digits
  int hn = snprintf_P(head, sizeof(head), HEAD_401, _authRealm, (unsigned)bn, CLOSE_NOSNIFF);
  if (!eghttp_head_fits(hn, sizeof(head)) || !s->client) return;
  s->client->write(head, (size_t)hn);
  // Plain writes like the head, an upload gets refused from a place where nothing may yield
  for (size_t at = 0; at < bn; at += sizeof(head)) {
    size_t n = bn - at < sizeof(head) ? bn - at : sizeof(head);
    memcpy_P(head, _authHelp + at, n);
    s->client->write(head, n);
  }
}

static void reply_status(EGHttpServer::Slot* s, PGM_P status) {
  char buf[STATUS_MAX];
  yield_write_sram(s, buf, canned(buf, status, 0));
}

void EGHttpServer::dispatch(Slot* s) {
  EGHttpRequest req;
  EGHttpResponse res;
  res._slot = s;

  if (!eghttp_parse_method(s->buf, &req._method)) {
    reply_status(s, S501);
    return;
  }
  if (!readRequest(s, req)) return;

  // an upload was already checked in beginStream
  uint16_t no = s->stream == NO_STREAM ? refusal(s, req) : 0;
  if (no == 401) {
    sendUnauthorized(s);
    return;
  }
  if (no) {
    reply_status(s, no == 403 ? S403 : S400);
    return;
  }

  claimChunkAcc(s);
  Route* r = matchedRoute(s);
  if (r) {
    r->handler(req, res, r->user);
  } else {
    if (_notFound) _notFound(req, res, _notFoundUser);
    else           res.send(404, FPSTR(HTTP_CT_PLAIN), FPSTR(HTTP_NOT_FOUND));
  }

  if (!res._started && !res._done) {
    res.send(500, FPSTR(HTTP_CT_PLAIN), F(""));
  }
  dropChunkAcc(s);
}

void EGHttpServer::dropChunkAcc(Slot* s) {
  set_acc(s, nullptr, 0);
#ifdef ESP32
  // Freed after each response, not held while idle
  free(_dispatchChunkAcc);
  _dispatchChunkAcc = nullptr;
#endif
}

void EGHttpServer::tick() {
  // Runs while an upload is open, so a silent one still hits its deadline
  if (!_tickWanted && !_streamOwner) return;
  _tickWanted = false;
  relisten();

  uint8_t replies = 0;
  for (auto& s : _slots) {
    if (s.state == DONE) {
      reclaimSlot(&s);
      continue;
    }
    if (s.state == RECEIVING && s.stream != NO_STREAM) drainStream(&s);
    if (s.state != READY) continue;
    respond(&s);
    // only a few replies a tick, each slow reader holds the loop once
    if (++replies >= EGHTTP_REPLIES_A_TICK) { _tickWanted = true; break; }
  }
}

void EGHttpServer::reclaimSlot(Slot* s) {
  if (s->stream == STREAM_OPEN && _streamOwner == s && _streamLen > 0) {
    drainStream(s);
  }
  // The whole body came, reply anyway so the handler frees it
  if (s->stream == STREAM_ENDED && !s->responded) respond(s);
  abortStream(s);
  if (_streamOwner == s) releaseStream();
  resetSlot(s);
}

void EGHttpServer::respond(Slot* s) {
  s->responded = true;
  s->state = RESPONDING;
  s->due = EGClock::now() + EGHTTP_SEND_MS;
  dispatch(s);
  if (!s->tailLen || !s->client || s->send_aborted) {
    finish(s);
    return;
  }
  // The body goes out as acks come in, the request isn't needed any more
  free_buf(s);
  s->client->setIdleTimeout(EGHTTP_REQ_MS);   // a reader that stops reading still gets dropped
}

void EGHttpServer::finish(Slot* s) {
  // A cut reply has nothing worth lingering for
  if (s->send_aborted) {
    drop(s);
    return;
  }
  if (s->client) s->client->close();
  markDone(s);
}

// RST frees the pcb now
void EGHttpServer::drop(Slot* s) {
  if (s->client) s->client->abort();
  markDone(s);
}

void EGHttpServer::sentMore(Slot* s) {
  if (!s->tailLen) return;
  if (eghttp_late(s)) s->send_aborted = true;
  else send_tail(s);
  if ((!s->tailLen || s->send_aborted) && s->state == RESPONDING) finish(s);
}

// EGHttpRequest

static char s_arg_decoded[EGHTTP_ARG_MAX + 1];

const char* EGHttpRequest::arg(const __FlashStringHelper* name) const {
  PGM_P n = (PGM_P)name;
  if (eghttp_form_find(_query, _queryLen, n, s_arg_decoded, sizeof(s_arg_decoded)) ||
      eghttp_form_find(_body,  _bodyLen,  n, s_arg_decoded, sizeof(s_arg_decoded)))
    return s_arg_decoded;
  return nullptr;
}


const char* EGHttpRequest::decodeArg(const char* body, size_t bodyLen,
                                       const char* name, char* out, size_t cap) {
  return eghttp_form_find(body, bodyLen, name, out, cap) ? out : nullptr;
}

static bool eghttp_write(EGHttpServer::Slot* s, const char* data, size_t len, bool pgm) {
  return pgm ? yield_write_pgm(s, data, len) : yield_write_sram(s, data, len);
}

// pgm = flash data
static bool eghttp_acc_add(EGHttpServer::Slot* s, const char* data, size_t len, bool pgm) {
  if (!s->chunkAcc || s->chunkAccCap == 0) return eghttp_write(s, data, len, pgm);
  if (s->chunkAccLen + len > s->chunkAccCap) {
    if (s->chunkAccLen > 0) {
      if (!yield_write_sram(s, s->chunkAcc, s->chunkAccLen)) return false;
      s->chunkAccLen = 0;
    }
    if (len >= s->chunkAccCap) return eghttp_write(s, data, len, pgm);
  }
  if (pgm) memcpy_P(s->chunkAcc + s->chunkAccLen, data, len);
  else     memcpy(s->chunkAcc + s->chunkAccLen, data, len);
  s->chunkAccLen += len;
  return true;
}

static bool eghttp_acc_append(EGHttpServer::Slot* s, const char* data, size_t len) {
  return eghttp_acc_add(s, data, len, false);
}

static bool eghttp_acc_append_pgm(EGHttpServer::Slot* s, PGM_P data, size_t len) {
  return eghttp_acc_add(s, data, len, true);
}

static bool eghttp_chunk_head(EGHttpServer::Slot* s, size_t len) {
  char hdr[16];
  int hn = snprintf_P(hdr, sizeof(hdr), CHUNK_HEAD, (unsigned)len);
  return eghttp_acc_append(s, hdr, (size_t)hn);
}

static bool eghttp_acc_flush(EGHttpServer::Slot* s) {
  if (!s->chunkAcc || s->chunkAccLen == 0) return true;
  bool ok = yield_write_sram(s, s->chunkAcc, s->chunkAccLen);
  s->chunkAccLen = 0;
  return ok;
}

// EGHttpResponse

// no CR or LF in a value
static bool eghttp_text(const char* s) {
  return s && eg_http_text(s, strlen(s));
}

bool EGHttpResponse::addHeaderP(const __FlashStringHelper* name, PGM_P value) {
  if (_started) return false;
  int n = snprintf_P(_extraHeaders + _extraLen,
                     sizeof(_extraHeaders) - _extraLen,
                     PSTR("%s: %s\r\n"), (PGM_P)name, value);
  if (n > 0 && (size_t)n < sizeof(_extraHeaders) - _extraLen) {
    _extraLen += n;
    return true;
  }
  _extraHeaders[_extraLen] = '\0';
  return false;
}

bool EGHttpResponse::addHeader(const __FlashStringHelper* name, const char* value) {
  return eghttp_text(value) && addHeaderP(name, value);
}

bool EGHttpResponse::addHeader(const __FlashStringHelper* name, const __FlashStringHelper* value) {
  return addHeaderP(name, (PGM_P)value);
}

bool EGHttpResponse::redirect(const __FlashStringHelper* location) {
  return redirectP((PGM_P)location);
}

bool EGHttpResponse::redirect(const char* location) {
  return eghttp_text(location) && redirectP(location);
}

static const size_t HEAD_MAX = 256;

// 0 = too long
static int eghttp_head(char (&head)[HEAD_MAX], uint16_t status, PGM_P ct, PGM_P enc,
                       size_t len, const char* extra) {
  int n = snprintf_P(head, sizeof(head), HEAD_OK,
    status, ct, enc, (unsigned)len, CLOSE_NOSNIFF, extra);
  return eghttp_head_fits(n, sizeof(head)) ? n : 0;
}

// RAM body only, F() has its own overload
void EGHttpResponse::send(uint16_t status, const __FlashStringHelper* contentType, const char* body) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t bodyLen = body ? strlen(body) : 0;
  char head[HEAD_MAX];
  int n = eghttp_head(head, status, (PGM_P)contentType, PSTR(""), bodyLen, _extraHeaders);
  if (!n) return;
  if (s->chunkAcc && s->chunkAccCap > 0) {
    s->chunkAccLen = 0;
    bool ok = eghttp_acc_append(s, head, (size_t)n);
    if (ok && bodyLen > 0) ok = eghttp_acc_append(s, body, bodyLen);
    if (ok) eghttp_acc_flush(s);
    _started = true;
    _done    = true;
    return;
  }
  if (!yield_write_sram(s, head, (size_t)n)) { _done = true; return; }
  _started = true;
  if (bodyLen > 0) yield_write_sram(s, body, bodyLen);
  _done = true;
}

void EGHttpResponse::send(uint16_t status, const __FlashStringHelper* contentType, const __FlashStringHelper* body) {
  if (!_slot || _started || _done) return;
  size_t bodyLen = body ? strlen_P((PGM_P)body) : 0;
  char head[HEAD_MAX];
  int n = eghttp_head(head, status, (PGM_P)contentType, PSTR(""), bodyLen, _extraHeaders);
  if (!n) return;
  writeHeadAndBodyP(head, (size_t)n, (PGM_P)body, bodyLen);
}

void EGHttpResponse::sendGzipP(uint16_t status, const __FlashStringHelper* contentType,
                                 const uint8_t* body, size_t len) {
  if (!_slot || _started || _done) return;
  char head[HEAD_MAX];
  int n = eghttp_head(head, status, (PGM_P)contentType, GZIP_CRLF, len, _extraHeaders);
  if (!n) return;
  writeHeadAndBodyP(head, (size_t)n, (PGM_P)body, len);
}

#if EGHTTP_PULL
void EGHttpResponse::sendPull(uint16_t status, const __FlashStringHelper* contentType, size_t len,
                              Fill fill, Done done, void* ctx) {
  if (!_slot || _started || _done || !fill) {
    if (done) done(ctx);
    return;
  }
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  s->pull    = fill;
  s->pulled  = done;
  s->pullCtx = ctx;
  s->pullAt  = 0;
  char head[HEAD_MAX];
  int n = eghttp_head(head, status, (PGM_P)contentType, PSTR(""), len, _extraHeaders);
  if (!n) return;
  writeHeadAndBodyP(head, (size_t)n, nullptr, len);
}
#endif

void EGHttpResponse::writeHeadAndBodyP(const char* head, size_t headLen,
                                       PGM_P body, size_t bodyLen) {
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  _started = true;
  _done    = true;
  // First write so the head fits, MORE keeps it with the body
  uint8_t f = bodyLen ? EGTcpClient::COPY | EGTcpClient::MORE : EGTcpClient::COPY;
  if (!s->client || s->client->write(head, headLen, f) < headLen) {
    s->send_aborted = true;
    return;
  }
  s->tail    = body;
  s->tailLen = bodyLen;
  send_tail(s);
}

bool EGHttpResponse::redirectP(PGM_P location) {
  if (!_slot || _started || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[HEAD_MAX];
  int n = snprintf_P(head, sizeof(head),
    PSTR("HTTP/1.1 302 Found\r\nLocation: %s\r\n%s%s\r\n"), location, CL0_CRLF, CLOSE_NOSNIFF);
  if (!eghttp_head_fits(n, sizeof(head))) return false;
  bool ok = yield_write_sram(s, head, (size_t)n);
  _started = true;
  _done    = true;
  return ok;
}

bool EGHttpResponse::beginChunked(uint16_t status, const __FlashStringHelper* contentType) {
  if (!_slot || _started || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[HEAD_MAX];
  int n = snprintf_P(head, sizeof(head),
    PSTR("HTTP/1.1 %u \r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\n%s%s\r\n"),
    status, (PGM_P)contentType, CLOSE_NOSNIFF, _extraHeaders);
  if (!eghttp_head_fits(n, sizeof(head))) return false;
  if (!yield_write_sram(s, head, (size_t)n)) { _done = true; return false; }
  _started = true;
  _chunked = true;
  return true;
}

bool EGHttpResponse::sendChunk(const char* data, size_t len) {
  if (!chunkOpen() || len == 0) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  if (!eghttp_chunk_head(s, len))               return false;
  if (!eghttp_acc_append(s, data, len))         return false;
  return eghttp_acc_append_pgm(s, CRLF, 2);
}

bool EGHttpResponse::sendChunk(const char* data) {
  if (!data) return false;
  return sendChunk(data, strlen(data));
}

bool EGHttpResponse::sendChunk(const __FlashStringHelper* data) {
  if (!data || !pgm_read_byte((PGM_P)data)) return false;
  return sendKV(data, nullptr, nullptr);
}

bool EGHttpResponse::sendKV(const __FlashStringHelper* prefix, const char* value) {
  return sendKV(prefix, value, nullptr);
}

static size_t eghttp_flash_len(const __FlashStringHelper* str) {
  return str ? strlen_P((PGM_P)str) : 0;
}

static bool eghttp_acc_append_part_P(EGHttpServer::Slot* s, const __FlashStringHelper* part,
                                     size_t len) {
  if (len == 0) return true;
  return eghttp_acc_append_pgm(s, (PGM_P)part, len);
}

bool EGHttpResponse::sendKV(const __FlashStringHelper* prefix, const char* value,
                            const __FlashStringHelper* suffix) {
  if (!chunkOpen()) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t pLen = eghttp_flash_len(prefix);
  size_t vLen = (value && *value) ? strlen(value) : 0;
  size_t sLen = eghttp_flash_len(suffix);
  size_t total = pLen + vLen + sLen;
  if (total == 0) return true;
  if (!eghttp_chunk_head(s, total))                      return false;
  if (!eghttp_acc_append_part_P(s, prefix, pLen))        return false;
  if (vLen && !eghttp_acc_append(s, value, vLen))        return false;
  if (!eghttp_acc_append_part_P(s, suffix, sLen))        return false;
  return eghttp_acc_append_pgm(s, CRLF, 2);
}

bool EGHttpResponse::endChunked() {
  if (!chunkOpen()) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  bool ok = eghttp_acc_append_pgm(s, PSTR("0\r\n\r\n"), 5) && eghttp_acc_flush(s);
  _done = true;
  return ok;
}
