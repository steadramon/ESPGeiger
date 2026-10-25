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

static const char CL0_CRLF[]      PROGMEM = "Content-Length: 0\r\n";
static const char CLOSE_NOSNIFF[] PROGMEM = "Connection: close\r\nX-Content-Type-Options: nosniff\r\n";
static const char HEAD_OK[]       PROGMEM =
  "HTTP/1.1 %u \r\nContent-Type: %s\r\nContent-Length: %u\r\n%s%s\r\n";
static const char CHUNK_HEAD[]    PROGMEM = "%X\r\n";
static const char CRLF[]          PROGMEM = "\r\n";
static const char M_GET[]         PROGMEM = "GET";
static const char M_POST[]        PROGMEM = "POST";

static const char R400[] PROGMEM =
  "HTTP/1.1 400 Bad Request\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n\r\n";
static const char R413[] PROGMEM =
  "HTTP/1.1 413 Payload Too Large\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n\r\n";
static const char R501[] PROGMEM =
  "HTTP/1.1 501 Not Implemented\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n\r\n";
static const char R503[] PROGMEM =
  "HTTP/1.1 503 Service Unavailable\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n\r\n";
static const char R503_RETRY1[] PROGMEM =
  "HTTP/1.1 503 Service Unavailable\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n"
  "Retry-After: 1\r\n\r\n";
static const char R503_RETRY5[] PROGMEM =
  "HTTP/1.1 503 Service Unavailable\r\n"
  "Content-Length: 0\r\n"
  "Connection: close\r\n"
  "Retry-After: 5\r\n\r\n";

// no yield, one write or dropped
static const size_t STATUS_MAX = 96;
static_assert(sizeof(R503_RETRY5) <= STATUS_MAX, "canned reply over STATUS_MAX");
static_assert(sizeof(R413) <= STATUS_MAX, "canned reply over STATUS_MAX");

static void write_status(EGTcpClient* c, PGM_P r, size_t len) {
  char buf[STATUS_MAX];
  if (!c || !r || !len || len > sizeof(buf) || c->space() < len) return;
  memcpy_P(buf, r, len);
  c->write(buf, len);
}

// main loop only, under the 3.2 s wdt
#ifdef ESP32
  // not vTaskDelay, that sleeps a tick
  #define EGHTTP_BACKPRESSURE_PAUSE() do { taskYIELD(); feedLoopWDT(); } while (0)
#else
  #define EGHTTP_BACKPRESSURE_PAUSE() do { yield(); delay(0); } while (0)
#endif
static const uint32_t EGHTTP_SEND_BUDGET_MS = 2500;

static bool yield_write_sram(EGHttpServer::Slot* s, const char* data, size_t len) {
  if (!s || s->send_aborted) return false;
  size_t sent = 0;
  uint32_t last_progress = EGClock::now();
  while (sent < len) {
    if (s->send_aborted) return false;
    if (!s->client || !s->client->connected()) {
#ifdef EGHTTP_DEBUG
      Serial.printf_P(PSTR("[EGHttp] send aborted, client gone after %u/%u B\n"),
                      (unsigned)sent, (unsigned)len);
#endif
      s->send_aborted = true;
      return false;
    }
    size_t can = s->client->space();
    if (can == 0) {
      if (EGClock::now() - last_progress > EGHTTP_SEND_BUDGET_MS) {
#ifdef EGHTTP_DEBUG
        Serial.printf_P(PSTR("[EGHttp] send aborted, %ums no-progress at %u/%u B\n"),
                        (unsigned)EGHTTP_SEND_BUDGET_MS, (unsigned)sent, (unsigned)len);
#endif
        s->send_aborted = true;
        return false;
      }
      EGHTTP_BACKPRESSURE_PAUSE();
      continue;
    }
    size_t take = (len - sent) < can ? (len - sent) : can;
    // write() already sends
    size_t wrote = s->client->write(data + sent, take);
    if (wrote == 0) {
      if (EGClock::now() - last_progress > EGHTTP_SEND_BUDGET_MS) {
#ifdef EGHTTP_DEBUG
        Serial.printf_P(PSTR("[EGHttp] send aborted, write stuck at %u/%u B\n"),
                        (unsigned)sent, (unsigned)len);
#endif
        s->send_aborted = true;
        return false;
      }
      EGHTTP_BACKPRESSURE_PAUSE();
      continue;
    }
    sent += wrote;
    last_progress = EGClock::now();
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

// what fits now, onSent sends the rest
static void send_tail(EGHttpServer::Slot* s) {
  while (s->tailLen && s->client) {
    size_t can = s->client->space();
    size_t n = s->tailLen < can ? s->tailLen : can;
    if (!n) return;
#ifdef ESP8266
    // flash isn't byte readable here
    char b[512];
    if (n > sizeof(b)) n = sizeof(b);
    memcpy_P(b, s->tail, n);
    const char* from = b;
#else
    const char* from = s->tail;
#endif
    uint8_t f = EGTcpClient::COPY;
    if (n < s->tailLen && n < can) f |= EGTcpClient::MORE;
    size_t w = s->client->write(from, n, f);
    if (!w) return;
    s->tail += w;
    s->tailLen -= w;
  }
}

// a cut head has no blank line, send none
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

// 0 = incomplete
static size_t eghttp_find_header_end(const char* buf, size_t len) {
  for (size_t i = 3; i < len; i++) {
    if (buf[i-3] == '\r' && buf[i-2] == '\n' &&
        buf[i-1] == '\r' && buf[i]   == '\n') {
      return i + 1;
    }
  }
  return 0;
}

// ---------- EGHttpServer ----------

// 0xFF = no route
bool EGHttpServer::growRoutes() {
  if (_routeCap >= 0xF0) return false;
  uint8_t cap = _routeCap + 16;
  Route* r = (Route*)realloc(_routes, cap * sizeof(Route));
  if (!r) return false;
  _routes = r;
  _routeCap = cap;
  return true;
}

// before begin() only
bool EGHttpServer::on(const __FlashStringHelper* path, EGHttpRequest::Method method, Handler h, void* user) {
  if (_server || (_routeCount == _routeCap && !growRoutes())) {
    Serial.printf_P(PSTR("[EGHttp] route not added: %s\n"), (PGM_P)path);
    return false;
  }
  _routes[_routeCount++] = { (PGM_P)path, method, h, user };
  return true;
}

// before begin(), after on()
bool EGHttpServer::onUpload(const __FlashStringHelper* path, BodyHandler bh, void* user) {
  if (_server || _uploadCount == EGHTTP_MAX_UPLOADS) {
    Serial.printf_P(PSTR("[EGHttp] upload not added: %s\n"), (PGM_P)path);
    return false;
  }
  for (uint8_t i = 0; i < _routeCount; i++) {
    Route& r = _routes[i];
    if (r.method == EGHttpRequest::POST && streq_PP(r.path, (PGM_P)path)) {
      _uploads[_uploadCount++] = { i, bh, user };
      return true;
    }
  }
  Serial.printf_P(PSTR("[EGHttp] onUpload: no POST route for %s\n"), (PGM_P)path);
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
  if (!user || !password || !*password) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  char raw[EGHTTP_AUTH_MAX + 1];
  int rlen = snprintf_P(raw, sizeof(raw), PSTR("%s:%s"), (PGM_P)user, password);
  if (rlen <= 0 || (size_t)rlen >= sizeof(raw)) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  unsigned char encoded[EGB64_LEN(EGHTTP_AUTH_MAX) + 1];
  unsigned int elen = encode_base64((const unsigned char*)raw, (unsigned int)rlen, encoded);
  encoded[elen] = '\0';
  int n = snprintf_P(_authValue, sizeof(_authValue), PSTR("Basic %s"), (char*)encoded);
  if (n <= 0 || (size_t)n >= sizeof(_authValue)) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  if (realm && pgm_read_byte((PGM_P)realm)) {
    strncpy_P(_authRealm, (PGM_P)realm, sizeof(_authRealm) - 1);
    _authRealm[sizeof(_authRealm) - 1] = '\0';
  }
  _authEnabled = true;
}

void EGHttpServer::begin(uint16_t port) {
  if (_server) return;
  // give back the growth room
  if (_routeCount && _routeCount < _routeCap) {
    Route* r = (Route*)realloc(_routes, _routeCount * sizeof(Route));
    if (r) {
      _routes = r;
      _routeCap = _routeCount;
    }
  }
  _server = new EGTcpServer();
#ifdef ESP32
  _server->setMaxClients(EGHTTP_MAX_INFLIGHT);
#endif
  _server->onAccept([this](EGTcpClient* c) { onClient(c); });
  _server->begin(port);
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

// fresh slot, buf kept
static void clear_slot(EGHttpServer::Slot* s) {
  s->delete_pending = false;
  s->len            = 0;
  s->headersDone    = false;
  s->contentLength  = 0;
  s->headerEnd      = 0;
  s->streaming      = false;
  s->stream_begun   = false;
  s->stream_ended   = false;
  s->stream_aborted = false;
  s->ack_paused     = false;
  s->ack_owed       = 0;
  s->bodyAcked      = 0;
  s->send_aborted   = false;
  s->responded      = false;
  s->routeIdx       = 0xFF;
  s->chunkAcc       = nullptr;
  s->chunkAccLen    = 0;
  s->chunkAccCap    = 0;
  s->tail           = nullptr;
  s->tailLen        = 0;
}

EGHttpServer::Slot* EGHttpServer::allocSlot(EGTcpClient* c) {
  for (uint8_t i = 0; i < _maxClients; i++) {
    Slot* s = &_slots[i];
    if (s->state != IDLE || s->client) continue;
    if (!s->buf) s->buf = (char*)malloc(EGHTTP_REQ_BUF);
    if (!s->buf) return nullptr;
    clear_slot(s);
    s->client = c;
    s->state  = RECEIVING;
    return s;
  }
  return nullptr;
}

void EGHttpServer::markDone(Slot* s) {
  if (s->buf) { free(s->buf); s->buf = nullptr; }
  s->chunkAcc       = nullptr;
  s->chunkAccLen    = 0;
  s->chunkAccCap    = 0;
  s->state          = DONE;
  s->delete_pending = true;
  _tickWanted       = true;
}

// the slot keeps the client till reclaimed
void EGHttpServer::sendStatusAndClose(Slot* s, const char* body, size_t len) {
  markDone(s);
  if (!s->client) return;
  write_status(s->client, (PGM_P)body, len);
  s->client->close();
}

void EGHttpServer::resetSlot(Slot* s) {
  if (s->buf) { free(s->buf); s->buf = nullptr; }
  clear_slot(s);
  EGTcpClient* c = s->client;
  s->client = nullptr;
  s->state  = IDLE;
  delete c;   // closes it if still open
}

static bool eghttp_stream_open(const EGHttpServer::Slot* s) {
  return s->streaming && s->stream_begun && !s->stream_ended;
}

// handler hears it once
void EGHttpServer::abortStream(Slot* s) {
  if (!eghttp_stream_open(s) || s->stream_aborted) return;
  s->stream_aborted = true;
  deliverBodyEvent(s, BODY_ABORT, nullptr, 0);
}

void EGHttpServer::wireSlotCallbacks(Slot* s, EGTcpClient* c) {
  c->setIdleTimeout(60000);
  c->setAckTimeout(5000);
  c->onSent([this, s](size_t) { sentMore(s); });
  c->onData([this, s](const uint8_t* data, size_t len) {
    if (s->state != RECEIVING) return;
    takeData(s, data, len);
    _tickWanted = true;
  });
  c->onClose([this, s](int8_t err) {
#ifdef EGHTTP_DEBUG
    Serial.printf_P(PSTR("[EGHttp] closed err=%d streaming=%d acked=%u/%u\n"),
                    (int)err, (int)s->streaming, (unsigned)s->bodyAcked,
                    (unsigned)s->contentLength);
#else
    (void)err;
#endif
    onClosed(s);
  });
}

void EGHttpServer::onClient(EGTcpClient* c) {
  _tickWanted = true;
  Slot* s = allocSlot(c);
  if (s) { wireSlotCallbacks(s, c); return; }

  // full, 503 as queueing drops pbufs
  write_status(c, R503_RETRY1, sizeof(R503_RETRY1) - 1);
  c->close();
  delete c;
}

// peer, error or timeout; tick() reclaims
void EGHttpServer::onClosed(Slot* s) {
  s->state          = DONE;
  s->delete_pending = true;
  _tickWanted       = true;
}

void EGHttpServer::takeStream(Slot* s, const uint8_t* data, size_t len) {
  if (_streamOwner != s || !_streamBuf) return;
  // past Content-Length is dropped
  size_t have = s->bodyAcked + _streamLen;
  size_t room = s->contentLength > have ? s->contentLength - have : 0;
  if (len > room) len = room;
  if (!len) return;
  if (_streamLen + len > STREAM_BUF_SIZE) {
    Serial.printf_P(PSTR("[EGHttp] stream buf overflow: %u+%u>%u, aborting\n"),
                    (unsigned)_streamLen, (unsigned)len, (unsigned)STREAM_BUF_SIZE);
    sendStatusAndClose(s, nullptr, 0);
    return;
  }
  memcpy(_streamBuf + _streamLen, data, len);
  _streamLen += len;
  s->ack_paused = true;
  s->client->ackLater();
}

void EGHttpServer::takeData(Slot* s, const uint8_t* data, size_t len) {
  if (!s->headersDone) {
    receiveHeaders(s, data, len);
    return;
  }

  if (s->streaming) {
    takeStream(s, data, len);
    return;
  }

  if (s->len + len > EGHTTP_REQ_BUF) {
    sendStatusAndClose(s, R413, sizeof(R413) - 1);
    return;
  }
  memcpy(s->buf + s->len, data, len);
  s->len += len;
  if (s->len < s->headerEnd + s->contentLength) return;
  s->state = READY;
}

void EGHttpServer::receiveHeaders(Slot* s, const uint8_t* data, size_t len) {
  if (s->len + len > EGHTTP_REQ_BUF) {
    Serial.printf_P(PSTR("[EGHttp] headers exceed REQ_BUF (%u+%u>%u) - 413\n"),
                    (unsigned)s->len, (unsigned)len, (unsigned)EGHTTP_REQ_BUF);
    sendStatusAndClose(s, R413, sizeof(R413) - 1);
    return;
  }
  memcpy(s->buf + s->len, data, len);
  s->len += len;

  size_t headerEnd = eghttp_find_header_end(s->buf, s->len);
  if (!headerEnd) return;
  s->headerEnd   = headerEnd;
  s->headersDone = true;

  if (eghttp_content_length(s->buf, s->headerEnd, &s->contentLength) == EGHTTP_CL_BAD) {
    sendStatusAndClose(s, R400, sizeof(R400) - 1);
    return;
  }

  identifyRoute(s);
  if (uploadFor(s->routeIdx)) {
    // auth before any body
    if (_authEnabled && !authorized(s)) {
      sendUnauthorized(s);
      sendStatusAndClose(s, nullptr, 0);
      return;
    }
    beginStream(s, len);
    return;
  }

  // room for the NUL
  if (s->contentLength >= EGHTTP_REQ_BUF - s->headerEnd) {
    sendStatusAndClose(s, R413, sizeof(R413) - 1);
    return;
  }
  if (s->len >= s->headerEnd + s->contentLength) {
    s->state = READY;
  }
}

void EGHttpServer::beginStream(Slot* s, size_t len) {
  if (_streamOwner) {
    sendStatusAndClose(s, R503_RETRY5, sizeof(R503_RETRY5) - 1);
    return;
  }
  uint8_t* buf = (uint8_t*)malloc(STREAM_BUF_SIZE);
  if (!buf) {
    Serial.printf_P(PSTR("[EGHttp] stream buf alloc failed (need %u)\n"),
                    (unsigned)STREAM_BUF_SIZE);
    sendStatusAndClose(s, R503, sizeof(R503) - 1);
    return;
  }
  size_t inBuf = s->len - s->headerEnd;
  if (inBuf > s->contentLength) inBuf = s->contentLength;
  if (inBuf) memcpy(buf, s->buf + s->headerEnd, inBuf);
  s->len = s->headerEnd;
  // an ack inside onData is clamped to 0
  s->ack_owed = (uint16_t)(len - inBuf);
  _streamBuf    = buf;
  _streamLen    = inBuf;
  _streamOwner  = s;
  s->ack_paused = true;
  s->streaming  = true;
  s->client->ackLater();
}

void EGHttpServer::releaseStream() {
  free(_streamBuf);
  _streamBuf   = nullptr;
  _streamLen   = 0;
  _streamOwner = nullptr;
}

// writes NULs into s->buf
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
  char savedQ = 0;
  if (q) { savedQ = *q; *q = '\0'; }
  s->routeIdx = findRoute(path, method);
  if (q) *q = savedQ;
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
  return 0xFF;
}

void EGHttpServer::deliverBodyEvent(Slot* s, BodyEvent ev,
                                     const char* chunk, size_t len) {
  const Upload* u = uploadFor(s->routeIdx);
  if (!u) return;
  const Route& r = _routes[u->route];
  EGHttpRequest req;
  req._method  = r.method;
  req._path    = r.path;
  req._body    = nullptr;
  req._bodyLen = s->contentLength;
  u->handler(req, ev, chunk, len, u->user);
}

void EGHttpServer::drainStream(Slot* s) {
  // snapshot, a handler that polls adds more
  bool mine = s->streaming && _streamOwner == s;
  size_t toDeliver = _streamLen;
  if (!mine || s->stream_ended) return;

  if (!s->stream_begun) {
    s->stream_begun = true;
    deliverBodyEvent(s, BODY_BEGIN, nullptr, 0);
  }
  if (s->ack_owed && s->client) {
    s->client->ack(s->ack_owed);
    s->ack_owed = 0;
  }

  if (toDeliver > 0) {
    deliverBodyEvent(s, BODY_DATA, (const char*)_streamBuf, toDeliver);
    // a second ackLater() loses bytes
    if (s->client) s->client->ack(toDeliver);
    s->bodyAcked += toDeliver;
    size_t leftover = (_streamLen > toDeliver) ? _streamLen - toDeliver : 0;
    if (leftover > 0) {
      memmove(_streamBuf, _streamBuf + toDeliver, leftover);
    }
    _streamLen = leftover;
  }

  if (s->bodyAcked >= s->contentLength) {
    s->stream_ended = true;
    deliverBodyEvent(s, BODY_END, nullptr, 0);
    s->state = READY;
    releaseStream();
  }
}

void EGHttpServer::claimChunkAcc(Slot* s) {
#ifdef ESP32
  if (!_dispatchChunkAcc) {
    _dispatchChunkAcc = (char*)malloc(EGHTTP_CHUNK_ACC);
  }
  s->chunkAcc    = _dispatchChunkAcc;
  s->chunkAccLen = 0;
  s->chunkAccCap = _dispatchChunkAcc ? EGHTTP_CHUNK_ACC : 0;
#else
  // tail only, +1 keeps the body's NUL
  size_t reqUsed = s->headerEnd + (s->streaming ? 0 : s->contentLength) + 1;
  if (s->buf && reqUsed < EGHTTP_REQ_BUF) {
    s->chunkAcc    = s->buf + reqUsed;
    s->chunkAccLen = 0;
    s->chunkAccCap = EGHTTP_REQ_BUF - reqUsed;
  } else {
    s->chunkAcc    = nullptr;
    s->chunkAccLen = 0;
    s->chunkAccCap = 0;
  }
#endif
}

bool EGHttpServer::readRequest(Slot* s, EGHttpRequest& req) {
  char* sp1 = strchr(s->buf, '\0');
  if (!sp1 || sp1 >= s->buf + s->headerEnd) return false;

  char* path = sp1 + 1;
  if (!memchr(path, '\0', s->buf + s->headerEnd - path)) return false;
  char* q = strchr(path, '?');
  if (q) {
    *q = '\0';
    req._query    = q + 1;
    req._queryLen = strlen(q + 1);
  }
  req._path = path;

  req._body    = s->buf + s->headerEnd;
  req._bodyLen = s->streaming ? 0 : s->contentLength;
  if (!s->streaming && s->headerEnd + req._bodyLen < EGHTTP_REQ_BUF) {
    s->buf[s->headerEnd + req._bodyLen] = '\0';
  }
  return true;
}

static bool at_line_end(char c) {
  return c == '\r' || c == '\n' || c == '\0';
}

bool EGHttpServer::authorized(const Slot* s) const {
  const char* p = eghttp_find_header(s->buf, s->headerEnd, PSTR("Authorization:"));
  if (!p) return false;
  const char* hend = s->buf + s->headerEnd;
  while (p < hend && (*p == ' ' || *p == '\t')) p++;
  size_t vlen = strlen(_authValue);
  return (size_t)(hend - p) > vlen &&
         strncmp(p, _authValue, vlen) == 0 &&
         at_line_end(p[vlen]);
}

static const char HEAD_401[] PROGMEM =
  "HTTP/1.1 401 Unauthorized\r\n"
  "WWW-Authenticate: Basic realm=\"%s\"\r\n"
  "%s%s\r\n";

void EGHttpServer::sendUnauthorized(Slot* s) {
  char head[sizeof(HEAD_401) + sizeof(_authRealm) + sizeof(CL0_CRLF) + sizeof(CLOSE_NOSNIFF)];
  int hn = snprintf_P(head, sizeof(head), HEAD_401, _authRealm, CL0_CRLF, CLOSE_NOSNIFF);
  if (eghttp_head_fits(hn, sizeof(head)) && s->client) {
    s->client->write(head, (size_t)hn);
  }
}

void EGHttpServer::dispatch(Slot* s) {
  EGHttpRequest req;
  EGHttpResponse res;
  res._slot = s;

  if (!eghttp_parse_method(s->buf, &req._method)) {
    yield_write_pgm(s, R501, sizeof(R501) - 1);
    return;
  }
  if (!readRequest(s, req)) return;

  if (_authEnabled && !authorized(s)) {
    sendUnauthorized(s);
    return;
  }

  claimChunkAcc(s);
  Route* r = matchedRoute(s);
  if (r) {
    r->handler(req, res, r->user);
  } else {
    if (_notFound) _notFound(req, res, _notFoundUser);
    else           res.send(404, FPSTR(HTTP_CT_PLAIN), F("Not Found"));
  }

  if (!res._started && !res._done) {
    res.send(500, FPSTR(HTTP_CT_PLAIN), F(""));
  }
  dropChunkAcc(s);
}

void EGHttpServer::dropChunkAcc(Slot* s) {
  s->chunkAcc    = nullptr;
  s->chunkAccLen = 0;
  s->chunkAccCap = 0;
#ifdef ESP32
  // per response, not held idle
  free(_dispatchChunkAcc);
  _dispatchChunkAcc = nullptr;
#endif
}

void EGHttpServer::tick() {
  if (!_tickWanted) return;
  _tickWanted = false;

  for (auto& s : _slots) {
    if (s.delete_pending) {
      reclaimSlot(&s);
      continue;
    }
    if (s.state == RECEIVING && s.streaming) drainStream(&s);
    if (s.state == READY) respond(&s);
  }
}

void EGHttpServer::reclaimSlot(Slot* s) {
  if (eghttp_stream_open(s) && _streamOwner == s && _streamLen > 0) {
    drainStream(s);
  }
  // whole body came, handler frees
  if (s->stream_ended && !s->responded) respond(s);
  abortStream(s);
  if (_streamOwner == s) releaseStream();
  resetSlot(s);
}

void EGHttpServer::respond(Slot* s) {
  s->responded = true;
  s->state = RESPONDING;
  dispatch(s);
  if (!s->tailLen || !s->client || s->send_aborted) {
    finish(s);
    return;
  }
  // body goes as acks come, the request isn't needed
  free(s->buf);
  s->buf = nullptr;
}

void EGHttpServer::finish(Slot* s) {
  if (s->client) s->client->close();
  markDone(s);
}

void EGHttpServer::sentMore(Slot* s) {
  if (!s->tailLen) return;
  send_tail(s);
  if (!s->tailLen && s->state == RESPONDING) finish(s);
}

// ---------- EGHttpRequest ----------

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

static bool eghttp_acc_append(EGHttpServer::Slot* s, const char* data, size_t len) {
  if (!s->chunkAcc || s->chunkAccCap == 0) return yield_write_sram(s, data, len);
  if (s->chunkAccLen + len > s->chunkAccCap) {
    if (s->chunkAccLen > 0) {
      if (!yield_write_sram(s, s->chunkAcc, s->chunkAccLen)) return false;
      s->chunkAccLen = 0;
    }
    if (len >= s->chunkAccCap) return yield_write_sram(s, data, len);
  }
  memcpy(s->chunkAcc + s->chunkAccLen, data, len);
  s->chunkAccLen += len;
  return true;
}

static bool eghttp_acc_append_pgm(EGHttpServer::Slot* s, PGM_P data, size_t len) {
  if (!s->chunkAcc || s->chunkAccCap == 0) return yield_write_pgm(s, data, len);
  if (s->chunkAccLen + len > s->chunkAccCap) {
    if (s->chunkAccLen > 0) {
      if (!yield_write_sram(s, s->chunkAcc, s->chunkAccLen)) return false;
      s->chunkAccLen = 0;
    }
    if (len >= s->chunkAccCap) return yield_write_pgm(s, data, len);
  }
  memcpy_P(s->chunkAcc + s->chunkAccLen, data, len);
  s->chunkAccLen += len;
  return true;
}

static bool eghttp_acc_flush(EGHttpServer::Slot* s) {
  if (!s->chunkAcc || s->chunkAccLen == 0) return true;
  bool ok = yield_write_sram(s, s->chunkAcc, s->chunkAccLen);
  s->chunkAccLen = 0;
  return ok;
}

// ---------- EGHttpResponse ----------

// no CR or LF in a value
static bool eghttp_text(const char* s) {
  return s && eg_http_text(s, strlen(s));
}

void EGHttpResponse::addHeaderP(const __FlashStringHelper* name, PGM_P value) {
  if (_started) return;
  int n = snprintf_P(_extraHeaders + _extraLen,
                     sizeof(_extraHeaders) - _extraLen,
                     PSTR("%s: %s\r\n"), (PGM_P)name, value);
  if (n > 0 && (size_t)n < sizeof(_extraHeaders) - _extraLen) _extraLen += n;
  else _extraHeaders[_extraLen] = '\0';
}

void EGHttpResponse::addHeader(const __FlashStringHelper* name, const char* value) {
  if (eghttp_text(value)) addHeaderP(name, value);
}

void EGHttpResponse::addHeader(const __FlashStringHelper* name, const __FlashStringHelper* value) {
  addHeaderP(name, (PGM_P)value);
}

void EGHttpResponse::redirect(const __FlashStringHelper* location) {
  redirectP((PGM_P)location);
}

void EGHttpResponse::redirect(const char* location) {
  if (eghttp_text(location)) redirectP(location);
}

// RAM only, F() has its own
void EGHttpResponse::send(uint16_t status, const __FlashStringHelper* contentType, const char* body) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t bodyLen = body ? strlen(body) : 0;
  char head[256];
  int n = snprintf_P(head, sizeof(head), HEAD_OK,
    status, (PGM_P)contentType, (unsigned)bodyLen, CLOSE_NOSNIFF, _extraHeaders);
  if (!eghttp_head_fits(n, sizeof(head))) return;
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
  char head[256];
  int n = snprintf_P(head, sizeof(head), HEAD_OK,
    status, (PGM_P)contentType, (unsigned)bodyLen, CLOSE_NOSNIFF, _extraHeaders);
  if (!eghttp_head_fits(n, sizeof(head))) return;
  writeHeadAndBodyP(head, (size_t)n, (PGM_P)body, bodyLen);
}

void EGHttpResponse::sendGzipP(uint16_t status, const __FlashStringHelper* contentType,
                                 const uint8_t* body, size_t len) {
  if (!_slot || _started || _done) return;
  char head[256];
  int n = snprintf_P(head, sizeof(head),
    PSTR("HTTP/1.1 %u \r\nContent-Type: %s\r\nContent-Encoding: gzip\r\n"
         "Content-Length: %u\r\n%s%s\r\n"),
    status, (PGM_P)contentType, (unsigned)len, CLOSE_NOSNIFF, _extraHeaders);
  if (!eghttp_head_fits(n, sizeof(head))) return;
  writeHeadAndBodyP(head, (size_t)n, (PGM_P)body, len);
}

void EGHttpResponse::writeHeadAndBodyP(const char* head, size_t headLen,
                                       PGM_P body, size_t bodyLen) {
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  _started = true;
  _done    = true;
  // first write, the head fits; MORE keeps it with the body
  uint8_t f = bodyLen ? EGTcpClient::COPY | EGTcpClient::MORE : EGTcpClient::COPY;
  if (!s->client || s->client->write(head, headLen, f) < headLen) {
    s->send_aborted = true;
    return;
  }
  s->tail    = body;
  s->tailLen = bodyLen;
  send_tail(s);
}

void EGHttpResponse::redirectP(PGM_P location) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[256];
  int n = snprintf_P(head, sizeof(head),
    PSTR("HTTP/1.1 302 Found\r\nLocation: %s\r\n%s%s\r\n"), location, CL0_CRLF, CLOSE_NOSNIFF);
  if (!eghttp_head_fits(n, sizeof(head))) return;
  yield_write_sram(s, head, (size_t)n);
  _started = true;
  _done    = true;
}

bool EGHttpResponse::beginChunked(uint16_t status, const __FlashStringHelper* contentType) {
  if (!_slot || _started || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[256];
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
  char hdr[16];
  int hn = snprintf_P(hdr, sizeof(hdr), CHUNK_HEAD, (unsigned)len);
  if (!eghttp_acc_append(s, hdr, (size_t)hn))   return false;
  if (!eghttp_acc_append(s, data, len))         return false;
  if (!eghttp_acc_append_pgm(s, CRLF, 2))       return false;
  return true;
}

bool EGHttpResponse::sendChunk(const char* data) {
  if (!data) return false;
  return sendChunk(data, strlen(data));
}

bool EGHttpResponse::sendChunk(const __FlashStringHelper* data) {
  if (!chunkOpen() || !data) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t len = strlen_P((PGM_P)data);
  if (len == 0) return false;
  char hdr[16];
  int hn = snprintf_P(hdr, sizeof(hdr), CHUNK_HEAD, (unsigned)len);
  if (!eghttp_acc_append(s, hdr, (size_t)hn))      return false;
  if (!eghttp_acc_append_pgm(s, (PGM_P)data, len)) return false;
  if (!eghttp_acc_append_pgm(s, CRLF, 2))          return false;
  return true;
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
  char hdr[16];
  int hn = snprintf_P(hdr, sizeof(hdr), CHUNK_HEAD, (unsigned)total);
  if (!eghttp_acc_append(s, hdr, (size_t)hn))            return false;
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
