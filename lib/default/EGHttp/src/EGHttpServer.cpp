/*
  EGHttpServer.cpp - Main-loop-dispatched HTTP/1.1 server on (ESP)AsyncTCP.

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
#include <string.h>
#include <stdlib.h>
#include <EGBase64.h>   // buffer-in/out, no heap (vs core's heap-using <base64.h>)

// ESP32 AsyncTCP callbacks race with tick(); ESP8266 NONOS is single-task.
#ifdef ESP32
  #include <freertos/portmacro.h>
  static portMUX_TYPE s_eghttp_mux = portMUX_INITIALIZER_UNLOCKED;
  #define EGHTTP_LOCK()   portENTER_CRITICAL(&s_eghttp_mux)
  #define EGHTTP_UNLOCK() portEXIT_CRITICAL(&s_eghttp_mux)
#else
  #define EGHTTP_LOCK()   ((void)0)
  #define EGHTTP_UNLOCK() ((void)0)
#endif

// Yield-retry write: blocks until len bytes go on the wire or the
// connection drops. Main-loop context only (handlers via tick()).
static bool yield_write_sram(EGHttpServer::Slot* s, const char* data, size_t len) {
  if (!s) return false;
  size_t sent = 0;
#ifdef ESP32
  // taskYIELD relinquishes to same-priority tasks (lwIP/AsyncTCP) without
  // sleeping a full RTOS tick (~10ms) like vTaskDelay(1) would.
  #define EGHTTP_BACKPRESSURE_PAUSE() do { taskYIELD(); } while (0)
#else
  #define EGHTTP_BACKPRESSURE_PAUSE() do { yield(); delay(0); } while (0)
#endif
  uint16_t spin_no_progress = 0;
  const uint16_t SPIN_LIMIT = 2000;
  while (sent < len) {
    if (!s->client) return false;
    size_t can = s->client->space();
    if (can == 0) {
      if (++spin_no_progress > SPIN_LIMIT) return false;
      EGHTTP_BACKPRESSURE_PAUSE();
      continue;
    }
    size_t take = (len - sent) < can ? (len - sent) : can;
    size_t wrote = s->client->write(data + sent, take);
    if (wrote == 0) {
      if (++spin_no_progress > SPIN_LIMIT) return false;
      EGHTTP_BACKPRESSURE_PAUSE();
      continue;
    }
    spin_no_progress = 0;
    s->client->send();
    sent += wrote;
    yield();
  }
#undef EGHTTP_BACKPRESSURE_PAUSE
  return true;
}

static bool yield_write_pgm(EGHttpServer::Slot* s, PGM_P data, size_t len) {
  // 512 fits comfortably in one TCP MSS write while keeping stack modest.
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

static bool yield_write_str(EGHttpServer::Slot* s, const char* str) {
  return yield_write_sram(s, str, strlen(str));
}

// ---------- EGHttpServer ----------

void EGHttpServer::on(const char* path, EGHttpRequest::Method method, Handler h, void* user) {
  if (_routeCount >= EGHTTP_MAX_ROUTES) {
    Serial.printf_P(PSTR("[EGHttp] route table full (%u), dropped: %s\n"),
                    (unsigned)EGHTTP_MAX_ROUTES, path);
    return;
  }
  _routes[_routeCount++] = { path, method, h, user, nullptr, nullptr };
}

void EGHttpServer::onUpload(const char* path, BodyHandler bh, void* user) {
  // POST-only; matches by path against a previously-registered route.
  for (uint8_t i = 0; i < _routeCount; i++) {
    Route& r = _routes[i];
    if (r.method == EGHttpRequest::POST && strcmp(r.path, path) == 0) {
      r.bodyHandler = bh;
      r.bodyUser    = user;
      return;
    }
  }
  Serial.printf_P(PSTR("[EGHttp] onUpload: no POST route for %s\n"), path);
}

void EGHttpServer::onNotFound(Handler h, void* user) {
  _notFound = h;
  _notFoundUser = user;
}

void EGHttpServer::setBasicAuth(const char* user, const char* password,
                                  const char* realm) {
  if (!user || !password || !*password) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  char raw[96];
  int rlen = snprintf(raw, sizeof(raw), "%s:%s", user, password);
  if (rlen <= 0 || (size_t)rlen >= sizeof(raw)) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  unsigned char encoded[128];
  unsigned int elen = encode_base64((unsigned char*)raw, (unsigned int)rlen, encoded);
  encoded[elen] = '\0';
  int n = snprintf(_authValue, sizeof(_authValue), "Basic %s", (char*)encoded);
  if (n <= 0 || (size_t)n >= sizeof(_authValue)) {
    _authEnabled = false;
    _authValue[0] = '\0';
    return;
  }
  if (realm && *realm) {
    strncpy(_authRealm, realm, sizeof(_authRealm) - 1);
    _authRealm[sizeof(_authRealm) - 1] = '\0';
  }
  _authEnabled = true;
}

void EGHttpServer::begin(uint16_t port) {
  if (_server) return;
  _server = new AsyncServer(port);
  _server->onClient([](void* arg, AsyncClient* c) {
    static_cast<EGHttpServer*>(arg)->onClient(c);
  }, this);
  _server->begin();
}

void EGHttpServer::end() {
  if (_server) { _server->end(); delete _server; _server = nullptr; }
  for (auto& s : _slots) {
    if (s.client) { s.client->close(true); }
    s.client = nullptr;
    s.state  = IDLE;
  }
#ifdef ESP32
  if (_dispatchChunkAcc) { free(_dispatchChunkAcc); _dispatchChunkAcc = nullptr; }
#endif
}

EGHttpServer::Slot* EGHttpServer::findSlot(AsyncClient* c) {
  for (auto& s : _slots) if (s.client == c) return &s;
  return nullptr;
}

EGHttpServer::Slot* EGHttpServer::allocSlot(AsyncClient* c) {
  for (auto& s : _slots) {
    if (s.state == IDLE && s.client == nullptr) {
      // Only buf is per-slot; chunkAcc is server-shared (claimed at dispatch).
      if (!s.buf) s.buf = (char*)malloc(EGHTTP_REQ_BUF);
      if (!s.buf) return nullptr;
      s.client         = c;
      s.state          = RECEIVING;
      s.delete_pending = false;
      s.len            = 0;
      s.headersDone    = false;
      s.contentLength  = 0;
      s.headerEnd      = 0;
      s.streaming      = false;
      s.stream_begun   = false;
      s.stream_ended   = false;
      s.stream_aborted = false;
      s.ack_paused     = false;
      s.bodyAcked      = 0;
      s.routeIdx       = 0xFF;
      return &s;
    }
  }
  return nullptr;
}

void EGHttpServer::resetSlot(Slot* s) {
  s->client         = nullptr;
  s->state          = IDLE;
  s->delete_pending = false;
  s->len            = 0;
  s->headersDone    = false;
  s->streaming      = false;
  s->stream_begun   = false;
  s->stream_ended   = false;
  s->stream_aborted = false;
  s->ack_paused     = false;
  s->bodyAcked      = 0;
  s->routeIdx       = 0xFF;
  // buf returned so idle server costs only its bookkeeping fields.
  if (s->buf)         { free(s->buf);         s->buf         = nullptr; }
#ifdef ESP32
  // chunkAcc is server-shared; just null the slot's view.
  s->chunkAcc    = nullptr;
  s->chunkAccLen = 0;
#endif
}

bool EGHttpServer::enqueueWaiter(AsyncClient* c) {
  EGHTTP_LOCK();
  if (_waitCount >= EGHTTP_WAIT_QUEUE) { EGHTTP_UNLOCK(); return false; }
  _waitQueue[_waitCount++] = c;
  EGHTTP_UNLOCK();
  return true;
}

void EGHttpServer::dropWaiter(AsyncClient* c) {
  EGHTTP_LOCK();
  for (uint8_t i = 0; i < _waitCount; i++) {
    if (_waitQueue[i] == c) {
      for (uint8_t j = i; j + 1 < _waitCount; j++)
        _waitQueue[j] = _waitQueue[j + 1];
      _waitQueue[--_waitCount] = nullptr;
      break;
    }
  }
  EGHTTP_UNLOCK();
}

void EGHttpServer::promoteWaiters() {
  // Pop one under the lock; alloc + wire outside (allocSlot mallocs and
  // AsyncTCP has its own locks - keep our critical section short).
  while (true) {
    AsyncClient* c = nullptr;
    EGHTTP_LOCK();
    if (_waitCount > 0) {
      c = _waitQueue[0];
      for (uint8_t j = 1; j < _waitCount; j++) _waitQueue[j - 1] = _waitQueue[j];
      _waitQueue[--_waitCount] = nullptr;
    }
    EGHTTP_UNLOCK();
    if (!c) return;
    Slot* s = allocSlot(c);
    if (!s) {
      // Slot pool full again - re-enqueue at the tail and stop.
      EGHTTP_LOCK();
      if (_waitCount < EGHTTP_WAIT_QUEUE) _waitQueue[_waitCount++] = c;
      EGHTTP_UNLOCK();
      return;
    }
    wireSlotCallbacks(s, c);
  }
}

void EGHttpServer::wireSlotCallbacks(Slot* /*s*/, AsyncClient* c) {
  // 60s no-rx timeout so a stuck upload doesn't pin its slot forever.
  c->setRxTimeout(60);

  c->onData([](void* arg, AsyncClient* cli, void* data, size_t len) {
    auto* self = static_cast<EGHttpServer*>(arg);
    Slot* sl = self->findSlot(cli);
    if (sl) self->onData(sl, data, len);
  }, this);

  c->onError([](void* arg, AsyncClient* cli, int8_t err) {
    auto* self = static_cast<EGHttpServer*>(arg);
    Slot* sl = self->findSlot(cli);
    Serial.printf_P(PSTR("[EGHttp] onError cli=%p err=%d streaming=%d acked=%u\n"),
                    cli, (int)err, sl ? (int)sl->streaming : -1,
                    sl ? (unsigned)sl->bodyAcked : 0u);
    if (sl) {
      sl->state          = DONE;
      sl->delete_pending = true;
      self->_tickWanted  = true;
      cli->close();   // force onDisconnect so the client gets deleted
    }
  }, this);

  c->onTimeout([](void* arg, AsyncClient* cli, uint32_t time) {
    auto* self = static_cast<EGHttpServer*>(arg);
    Slot* sl = self->findSlot(cli);
    Serial.printf_P(PSTR("[EGHttp] onTimeout cli=%p t=%u streaming=%d acked=%u\n"),
                    cli, (unsigned)time, sl ? (int)sl->streaming : -1,
                    sl ? (unsigned)sl->bodyAcked : 0u);
    if (sl) {
      sl->state          = DONE;
      sl->delete_pending = true;
      self->_tickWanted  = true;
      cli->close();
    }
  }, this);
}

void EGHttpServer::onClient(AsyncClient* c) {
  _tickWanted = true;   // any accept = work for tick (alloc or 503)
  // onDisconnect first - every accepted client must clean up, whether it
  // ended up in a slot or sat in the wait queue.
  c->onDisconnect([](void* arg, AsyncClient* cli) {
    auto* self = static_cast<EGHttpServer*>(arg);
    Slot* sl = self->findSlot(cli);
    if (sl && sl->streaming && !sl->stream_ended) {
      Serial.printf_P(PSTR("[EGHttp] onDisconnect mid-stream cli=%p acked=%u/%u\n"),
                      cli, (unsigned)sl->bodyAcked, (unsigned)sl->contentLength);
    }
    if (sl) {
      // onDisconnect fires once - we own the delete. Null s.client so
      // tick's reclaim doesn't UAF.
      sl->state          = DONE;
      sl->delete_pending = true;
      sl->client         = nullptr;
      self->_tickWanted  = true;
      delete cli;
    } else {
      // Slot already released (eager-close), or client was queued.
      self->dropWaiter(cli);
      delete cli;
    }
  }, this);

  Slot* s = allocSlot(c);
  if (s) { wireSlotCallbacks(s, c); return; }

  // Slots full. Queueing is lossy (AsyncTCP frees pbufs with no onData
  // callback), so a queued client's request bytes get dropped and the
  // promotion lands on a slot that never sees its request - 60s hang
  // until rx timeout. Better to 503 + close immediately; browser retries
  // per Retry-After.
  static const char r503[] =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "Retry-After: 1\r\n\r\n";
  if (c->space() >= sizeof(r503) - 1) {
    c->write(r503, sizeof(r503) - 1);
    c->send();
  }
  c->close();
}

void EGHttpServer::onData(Slot* s, void* data, size_t len) {
  if (s->state != RECEIVING) return;   // already past parse, ignore extra
  _tickWanted = true;                  // packet received - tick may have work

  // Phase 1: header accumulation. Streaming routes pass through here
  // too; we need Content-Length before route identification.
  if (!s->headersDone) {
    if (s->len + len > EGHTTP_REQ_BUF) {
      Serial.printf_P(PSTR("[EGHttp] headers exceed REQ_BUF (%u+%u>%u) - 413\n"),
                      (unsigned)s->len, (unsigned)len, (unsigned)EGHTTP_REQ_BUF);
      static const char r413[] =
        "HTTP/1.1 413 Payload Too Large\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
      s->client->write(r413, sizeof(r413) - 1);
      s->client->send();
      s->state          = DONE;
      s->delete_pending = true;
      s->client->close();
      return;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;

    for (size_t i = 3; i < s->len; i++) {
      if (s->buf[i-3] == '\r' && s->buf[i-2] == '\n' &&
          s->buf[i-1] == '\r' && s->buf[i]   == '\n') {
        s->headerEnd  = i + 1;
        s->headersDone = true;
        break;
      }
    }
    if (!s->headersDone) return;

    s->contentLength = 0;
    for (size_t i = 0; i + 16 < s->headerEnd; i++) {
      if (strncasecmp(s->buf + i, "Content-Length:", 15) == 0) {
        const char* p = s->buf + i + 15;
        while (*p == ' ') p++;
        s->contentLength = (size_t)atol(p);
        break;
      }
    }

    identifyRoute(s);
    if (s->routeIdx != 0xFF && s->routeIdx < _routeCount &&
        _routes[s->routeIdx].bodyHandler) {
      // One stream at a time; lazy buffer alloc for zero idle cost.
      if (_streamOwner != nullptr) {
        static const char r503[] =
          "HTTP/1.1 503 Service Unavailable\r\n"
          "Content-Length: 0\r\n"
          "Connection: close\r\n"
          "Retry-After: 5\r\n\r\n";
        s->client->write(r503, sizeof(r503) - 1);
        s->client->send();
        s->state          = DONE;
        s->delete_pending = true;
        s->client->close();
        return;
      }
      _streamBuf = (uint8_t*)malloc(STREAM_BUF_SIZE);
      if (!_streamBuf) {
        Serial.printf_P(PSTR("[EGHttp] stream buf alloc failed (need %u)\n"),
                        (unsigned)STREAM_BUF_SIZE);
        static const char r503[] =
          "HTTP/1.1 503 Service Unavailable\r\n"
          "Content-Length: 0\r\n"
          "Connection: close\r\n\r\n";
        s->client->write(r503, sizeof(r503) - 1);
        s->client->send();
        s->state          = DONE;
        s->delete_pending = true;
        s->client->close();
        return;
      }
      s->streaming = true;
      _streamOwner = s;
      _streamLen   = 0;

      // Body bytes that arrived with the headers carry over.
      size_t inBuf = s->len - s->headerEnd;
      if (inBuf) {
        memcpy(_streamBuf, s->buf + s->headerEnd, inBuf);
        _streamLen = inBuf;
      }
      s->len = s->headerEnd;
      s->client->ackLater();   // peer throttles until tick() drains
      s->ack_paused = true;
      return;
    }

    if (s->len >= s->headerEnd + s->contentLength) {
      s->state = READY;
    }
    return;
  }

  // Phase 2: body after headers (streaming path).
  if (s->streaming) {
    // Lock the read-then-write on _streamLen: drainStream in the main
    // task does the inverse mutation; without this the appended bytes
    // can land beyond a stale snapshot and get clobbered when drainStream
    // resets _streamLen, losing ~1 MTU silently mid-upload.
    EGHTTP_LOCK();
    bool overflow = (_streamLen + len > STREAM_BUF_SIZE);
    if (!overflow) {
      memcpy(_streamBuf + _streamLen, data, len);
      _streamLen += len;
    }
    EGHTTP_UNLOCK();

    if (overflow) {
      Serial.printf_P(PSTR("[EGHttp] stream buf overflow: %u+%u>%u, aborting\n"),
                      (unsigned)_streamLen, (unsigned)len, (unsigned)STREAM_BUF_SIZE);
      s->client->close();
      s->state          = DONE;
      s->delete_pending = true;
      return;
    }
    s->client->ackLater();
    s->ack_paused = true;
    return;
  }

  // Non-streaming body: accumulate, 413 on overflow, READY when full.
  if (s->len + len > EGHTTP_REQ_BUF) {
    static const char r413[] =
      "HTTP/1.1 413 Payload Too Large\r\n"
      "Content-Length: 0\r\n"
      "Connection: close\r\n\r\n";
    s->client->write(r413, sizeof(r413) - 1);
    s->client->send();
    s->state          = DONE;
    s->delete_pending = true;
    s->client->close();
    return;
  }
  memcpy(s->buf + s->len, data, len);
  s->len += len;
  if (s->len < s->headerEnd + s->contentLength) return;
  s->state = READY;
}

void EGHttpServer::parseRequest(Slot* s) {
  (void)s;   // identifyRoute already did the work.
}

void EGHttpServer::identifyRoute(Slot* s) {
  // Mutates s->buf: NULs at method/path/line boundaries. Caches routeIdx.
  if (s->routeIdx != 0xFF) return;
  if (!s->headersDone) return;

  char* lineEnd = nullptr;
  for (size_t i = 0; i + 1 < s->headerEnd; i++) {
    if (s->buf[i] == '\r' && s->buf[i+1] == '\n') { lineEnd = s->buf + i; break; }
  }
  if (!lineEnd) return;
  *lineEnd = '\0';

  char* sp1 = strchr(s->buf, ' ');
  if (!sp1) return;
  *sp1 = '\0';
  EGHttpRequest::Method method;
  if      (strcmp(s->buf, "GET")  == 0) method = EGHttpRequest::GET;
  else if (strcmp(s->buf, "POST") == 0) method = EGHttpRequest::POST;
  else return;

  char* path = sp1 + 1;
  char* sp2 = strchr(path, ' ');
  if (sp2) *sp2 = '\0';
  // Strip query for route match; restore after so dispatch can read it.
  char* q = strchr(path, '?');
  char savedQ = 0;
  if (q) { savedQ = *q; *q = '\0'; }

  for (uint8_t i = 0; i < _routeCount; i++) {
    Route& r = _routes[i];
    if ((r.method == EGHttpRequest::ANY || r.method == method) &&
        strcmp(r.path, path) == 0) {
      s->routeIdx = i;
      break;
    }
  }
  if (q) *q = savedQ;
}

void EGHttpServer::deliverBodyEvent(Slot* s, BodyEvent ev,
                                     const char* data, size_t len) {
  if (s->routeIdx == 0xFF || s->routeIdx >= _routeCount) return;
  Route& r = _routes[s->routeIdx];
  if (!r.bodyHandler) return;
  EGHttpRequest req;
  req._method  = r.method;
  req._path    = r.path;
  req._body    = nullptr;          // body is in transit, not buffered
  req._bodyLen = s->contentLength;
  r.bodyHandler(req, ev, data, len, r.bodyUser);
}

void EGHttpServer::drainStream(Slot* s) {
  if (!s->streaming || s->stream_ended) return;
  if (_streamOwner != s) return;             // not the active stream

  if (!s->stream_begun) {
    s->stream_begun = true;
    deliverBodyEvent(s, BODY_BEGIN, nullptr, 0);
  }

  if (_streamLen > 0) {
    // Snapshot before delivery - Update.write's optimistic_yield can let
    // onData append more bytes; processing past the snapshot would ack
    // bytes that never reach the handler (silent truncation).
    size_t toDeliver = _streamLen;
    deliverBodyEvent(s, BODY_DATA, (const char*)_streamBuf, toDeliver);
    s->bodyAcked += toDeliver;
    // Stay in manual-ack mode: a second ackLater() zeroes _rx_ack_len and
    // loses already-received-but-unacked bytes.
    if (s->client) s->client->ack(toDeliver);
    // Lock the memmove + _streamLen reset against onData: any bytes
    // appended between our read and write of _streamLen here would be
    // silently overwritten on the next onData append. (~1 MTU lost
    // mid-upload, breaks OTA at 99%+ completion.)
    EGHTTP_LOCK();
    size_t leftover = (_streamLen > toDeliver) ? _streamLen - toDeliver : 0;
    if (leftover > 0) {
      memmove(_streamBuf, _streamBuf + toDeliver, leftover);
    }
    _streamLen = leftover;
    EGHTTP_UNLOCK();
  }

  if (s->bodyAcked >= s->contentLength) {
    s->stream_ended = true;
    deliverBodyEvent(s, BODY_END, nullptr, 0);
    s->state = READY;                       // main handler runs next tick
    _streamOwner = nullptr;
    if (_streamBuf) { free(_streamBuf); _streamBuf = nullptr; }
    _streamLen = 0;
  }
}

void EGHttpServer::dispatch(Slot* s) {
  EGHttpRequest req;
  EGHttpResponse res;
  res._slot = s;

#ifdef ESP32
  // Claim server-shared chunk accumulator. Lazy-alloc on first use;
  // kept alive for the life of the server (no per-request churn).
  // dispatch() is serialised in tick() so single buffer is safe.
  if (!_dispatchChunkAcc) {
    _dispatchChunkAcc = (char*)malloc(EGHTTP_CHUNK_ACC);
  }
  s->chunkAcc    = _dispatchChunkAcc;
  s->chunkAccLen = 0;
#endif

  // Walk the (already-NUL-mutated) request line back out for req.
  char* sp1 = strchr(s->buf, '\0');   // first NUL = end of method
  if (!sp1 || sp1 >= s->buf + s->headerEnd) { s->client->close(); return; }
  if      (strcmp(s->buf, "GET")  == 0) req._method = EGHttpRequest::GET;
  else if (strcmp(s->buf, "POST") == 0) req._method = EGHttpRequest::POST;
  else { s->client->close(); return; }

  char* path = sp1 + 1;
  // Path was NUL-terminated by identifyRoute (at the space before HTTP/x).
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

  if (_authEnabled) {
    bool authOk = false;
    for (size_t i = 0; i + 14 < s->headerEnd; i++) {
      if (strncasecmp(s->buf + i, "Authorization:", 14) == 0) {
        const char* p = s->buf + i + 14;
        while (*p == ' ' || *p == '\t') p++;
        size_t vlen = strlen(_authValue);
        if (strncmp(p, _authValue, vlen) == 0 &&
            (p[vlen] == '\r' || p[vlen] == '\n' || p[vlen] == '\0')) {
          authOk = true;
        }
        break;
      }
    }
    if (!authOk) {
      char head[160];
      int hn = snprintf(head, sizeof(head),
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"%s\"\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n", _authRealm);
      if (hn > 0 && s->client) {
        s->client->write(head, (size_t)hn);
        s->client->send();
      }
      return;     // tick() closes the connection
    }
  }

  if (s->routeIdx != 0xFF && s->routeIdx < _routeCount) {
    Route& r = _routes[s->routeIdx];
    r.handler(req, res, r.user);
  } else {
    if (_notFound) _notFound(req, res, _notFoundUser);
    else           res.send(404, "text/plain", "Not Found");
  }

  // Handler that sent nothing: 500 so the client doesn't hang.
  if (!res._started && !res._done) {
    res.send(500, "text/plain", "");
  }
}

void EGHttpServer::tick() {
  // Fast path - one volatile bool read instead of a per-slot scan.
  // Callbacks set _tickWanted; we clear it optimistically and anything
  // firing during the body re-sets it.
  if (!_tickWanted && _waitCount == 0) return;
  _tickWanted = false;

  for (auto& s : _slots) {
    if (s.delete_pending) {
      // One last drain so unfinished streams catch any pending bytes;
      // drainStream transitions to stream_ended on its own if complete.
      if (s.streaming && s.stream_begun && !s.stream_ended &&
          _streamOwner == &s && _streamLen > 0) {
        drainStream(&s);
      }
      if (s.streaming && s.stream_begun && !s.stream_ended && !s.stream_aborted) {
        s.stream_aborted = true;
        deliverBodyEvent(&s, BODY_ABORT, nullptr, 0);
      }
      if (_streamOwner == &s) {
        _streamOwner = nullptr;
        if (_streamBuf) { free(_streamBuf); _streamBuf = nullptr; }
        _streamLen = 0;
      }
      // Don't delete s.client - eager-close path leaves it for AsyncTCP's
      // later onDisconnect; disconnect-driven path already deleted it.
      resetSlot(&s);
      continue;
    }
    if (s.state == RECEIVING && s.streaming) {
      drainStream(&s);
    }
    if (s.state == READY) {
      s.state = RESPONDING;
      dispatch(&s);
      if (s.client && s.client->connected()) s.client->close();
      // Eager reclaim: ESP32 AsyncTCP can lag onDisconnect by hundreds of
      // ms. Without this, queued connections wait, returning 503s.
      s.state          = DONE;
      s.delete_pending = true;
      _tickWanted      = true;     // next tick must run reclaim path
    }
  }

  if (_waitCount > 0) promoteWaiters();
}

// ---------- EGHttpRequest ----------

static bool find_arg_in(const char* region, size_t regionLen,
                        const char* name, size_t nameLen,
                        char* out, size_t outCap) {
  if (!region || regionLen == 0) return false;
  const char* p   = region;
  const char* end = region + regionLen;
  while (p < end) {
    if ((size_t)(end - p) > nameLen + 1 &&
        memcmp(p, name, nameLen) == 0 && p[nameLen] == '=') {
      p += nameLen + 1;
      size_t o = 0;
      while (p < end && *p != '&' && o < outCap - 1) {
        if (*p == '%' && p + 2 < end) {
          char hex[3] = { p[1], p[2], 0 };
          out[o++] = (char)strtol(hex, nullptr, 16);
          p += 3;
        } else if (*p == '+') {
          out[o++] = ' '; p++;
        } else {
          out[o++] = *p++;
        }
      }
      out[o] = '\0';
      return true;
    }
    while (p < end && *p != '&') p++;
    if (p < end) p++;
  }
  return false;
}

// Shared between arg() and decodeArg(); consume before the next call.
static char s_arg_decoded[128];

const char* EGHttpRequest::arg(const char* name) const {
  if (!name) return nullptr;
  size_t nameLen = strlen(name);
  if (find_arg_in(_query, _queryLen, name, nameLen, s_arg_decoded, sizeof(s_arg_decoded)))
    return s_arg_decoded;
  if (find_arg_in(_body,  _bodyLen,  name, nameLen, s_arg_decoded, sizeof(s_arg_decoded)))
    return s_arg_decoded;
  return nullptr;
}

const char* EGHttpRequest::decodeArg(const char* body, size_t bodyLen,
                                       const char* name) {
  if (!body || bodyLen == 0 || !name) return nullptr;
  if (find_arg_in(body, bodyLen, name, strlen(name),
                  s_arg_decoded, sizeof(s_arg_decoded))) {
    return s_arg_decoded;
  }
  return nullptr;
}

#ifdef ESP32
// Append-or-flush on overflow. Goal: one write for the common case
// where the whole chunked response fits in EGHTTP_CHUNK_ACC.
static bool eghttp_acc_append(EGHttpServer::Slot* s, const char* data, size_t len) {
  if (!s->chunkAcc) return yield_write_sram(s, data, len);
  if (s->chunkAccLen + len > EGHTTP_CHUNK_ACC) {
    if (s->chunkAccLen > 0) {
      if (!yield_write_sram(s, s->chunkAcc, s->chunkAccLen)) return false;
      s->chunkAccLen = 0;
    }
    if (len >= EGHTTP_CHUNK_ACC) return yield_write_sram(s, data, len);
  }
  memcpy(s->chunkAcc + s->chunkAccLen, data, len);
  s->chunkAccLen += len;
  return true;
}

static bool eghttp_acc_append_pgm(EGHttpServer::Slot* s, PGM_P data, size_t len) {
  if (!s->chunkAcc) return yield_write_pgm(s, data, len);
  if (s->chunkAccLen + len > EGHTTP_CHUNK_ACC) {
    if (s->chunkAccLen > 0) {
      if (!yield_write_sram(s, s->chunkAcc, s->chunkAccLen)) return false;
      s->chunkAccLen = 0;
    }
    if (len >= EGHTTP_CHUNK_ACC) return yield_write_pgm(s, data, len);
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
#endif

// ---------- EGHttpResponse ----------

void EGHttpResponse::addHeader(const char* name, const char* value) {
  if (_started) return;
  int n = snprintf(_extraHeaders + _extraLen,
                   sizeof(_extraHeaders) - _extraLen,
                   "%s: %s\r\n", name, value);
  if (n > 0 && (size_t)n < sizeof(_extraHeaders) - _extraLen) _extraLen += n;
}

void EGHttpResponse::send(uint16_t status, const char* contentType, const char* body) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t bodyLen = body ? strlen(body) : 0;
  if (bodyLen > EGHTTP_BODY_SCRATCH) {
    static const char too_big[] PROGMEM =
      "EGHttp: body too large for inline send; use beginChunked";
    size_t tlen = strlen_P(too_big);
    char head[256];
    int n = snprintf(head, sizeof(head),
      "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
      (unsigned)tlen);
    yield_write_sram(s, head, (size_t)n);
    yield_write_pgm(s, (PGM_P)too_big, tlen);
    _started = true; _done = true;
    return;
  }
  // AsyncClient::write copies into its tx buffer, so writing `body`
  // directly is safe; no intermediate scratch buffer needed.
  char head[256];
  int n = snprintf(head, sizeof(head),
    "HTTP/1.1 %u OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n%s\r\n",
    status, contentType, (unsigned)bodyLen, _extraHeaders);
#ifdef ESP32
  // Header + body in one write; same anti-race motivation as chunked.
  if (s->chunkAcc) {
    s->chunkAccLen = 0;
    bool ok = eghttp_acc_append(s, head, (size_t)n);
    if (ok && bodyLen > 0) ok = eghttp_acc_append(s, body, bodyLen);
    if (ok) ok = eghttp_acc_flush(s);
    _started = true;
    _done    = true;
    return;
  }
#endif
  if (!yield_write_sram(s, head, (size_t)n)) { _done = true; return; }
  _started = true;
  if (bodyLen > 0) yield_write_sram(s, body, bodyLen);
  _done = true;
}

void EGHttpResponse::send(uint16_t status, const char* contentType, const __FlashStringHelper* body) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t bodyLen = body ? strlen_P((PGM_P)body) : 0;
  char head[256];
  int n = snprintf(head, sizeof(head),
    "HTTP/1.1 %u OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n%s\r\n",
    status, contentType, (unsigned)bodyLen, _extraHeaders);
#ifdef ESP32
  // Pages larger than the accumulator fall through to direct-write
  // mid-body via the append_pgm overflow branch.
  if (s->chunkAcc) {
    s->chunkAccLen = 0;
    bool ok = eghttp_acc_append(s, head, (size_t)n);
    if (ok && bodyLen > 0) ok = eghttp_acc_append_pgm(s, (PGM_P)body, bodyLen);
    if (ok) ok = eghttp_acc_flush(s);
    _started = true;
    _done    = true;
    return;
  }
#endif
  if (!yield_write_sram(s, head, (size_t)n)) { _done = true; return; }
  _started = true;
  if (bodyLen > 0) yield_write_pgm(s, (PGM_P)body, bodyLen);
  _done = true;
}

void EGHttpResponse::redirect(const char* location) {
  if (!_slot || _started || _done) return;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[256];
  int n = snprintf(head, sizeof(head),
    "HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", location);
  yield_write_sram(s, head, (size_t)n);
  _started = true;
  _done    = true;
}

bool EGHttpResponse::beginChunked(uint16_t status, const char* contentType) {
  if (!_slot || _started || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char head[256];
  int n = snprintf(head, sizeof(head),
    "HTTP/1.1 %u OK\r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n%s\r\n",
    status, contentType, _extraHeaders);
  if (!yield_write_sram(s, head, (size_t)n)) { _done = true; return false; }
  _started = true;
  _chunked = true;
  return true;
}

bool EGHttpResponse::sendChunk(const char* data, size_t len) {
  if (!_slot || !_chunked || _done || len == 0) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  char hdr[16];
  int hn = snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)len);
#ifdef ESP32
  if (!eghttp_acc_append(s, hdr, (size_t)hn))   return false;
  if (!eghttp_acc_append(s, data, len))         return false;
  if (!eghttp_acc_append(s, "\r\n", 2))         return false;
#else
  if (!yield_write_sram(s, hdr, (size_t)hn))    return false;
  if (!yield_write_sram(s, data, len))          return false;
  if (!yield_write_sram(s, "\r\n", 2))          return false;
#endif
  return true;
}

bool EGHttpResponse::sendChunk(const char* data) {
  if (!data) return false;
  return sendChunk(data, strlen(data));
}

bool EGHttpResponse::sendChunk(const __FlashStringHelper* data) {
  if (!_slot || !_chunked || _done || !data) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t len = strlen_P((PGM_P)data);
  if (len == 0) return false;
  char hdr[16];
  int hn = snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)len);
#ifdef ESP32
  if (!eghttp_acc_append(s, hdr, (size_t)hn))      return false;
  if (!eghttp_acc_append_pgm(s, (PGM_P)data, len)) return false;
  if (!eghttp_acc_append(s, "\r\n", 2))            return false;
#else
  if (!yield_write_sram(s, hdr, (size_t)hn))       return false;
  if (!yield_write_pgm(s, (PGM_P)data, len))       return false;
  if (!yield_write_sram(s, "\r\n", 2))             return false;
#endif
  return true;
}

bool EGHttpResponse::sendKV(const __FlashStringHelper* prefix, const char* value) {
  return sendKV(prefix, value, nullptr);
}

bool EGHttpResponse::sendKV(const __FlashStringHelper* prefix, const char* value,
                            const __FlashStringHelper* suffix) {
  if (!_slot || !_chunked || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
  size_t pLen = prefix ? strlen_P((PGM_P)prefix) : 0;
  size_t vLen = (value && *value) ? strlen(value) : 0;
  size_t sLen = suffix ? strlen_P((PGM_P)suffix) : 0;
  size_t total = pLen + vLen + sLen;
  if (total == 0) return true;
  char hdr[16];
  int hn = snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)total);
#ifdef ESP32
  if (!eghttp_acc_append(s, hdr, (size_t)hn))                          return false;
  if (pLen && !eghttp_acc_append_pgm(s, (PGM_P)prefix, pLen))          return false;
  if (vLen && !eghttp_acc_append(s, value, vLen))                      return false;
  if (sLen && !eghttp_acc_append_pgm(s, (PGM_P)suffix, sLen))          return false;
  return eghttp_acc_append(s, "\r\n", 2);
#else
  if (!yield_write_sram(s, hdr, (size_t)hn))                       return false;
  if (pLen && !yield_write_pgm(s, (PGM_P)prefix, pLen))            return false;
  if (vLen && !yield_write_sram(s, value, vLen))                   return false;
  if (sLen && !yield_write_pgm(s, (PGM_P)suffix, sLen))            return false;
  return yield_write_sram(s, "\r\n", 2);
#endif
}

bool EGHttpResponse::endChunked() {
  if (!_slot || !_chunked || _done) return false;
  auto* s = static_cast<EGHttpServer::Slot*>(_slot);
#ifdef ESP32
  // Append terminating zero-chunk into accumulator, then one big write.
  bool ok = eghttp_acc_append(s, "0\r\n\r\n", 5) && eghttp_acc_flush(s);
#else
  bool ok = yield_write_sram(s, "0\r\n\r\n", 5);
#endif
  _done = true;
  return ok;
}
