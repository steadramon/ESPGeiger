/*
  EGHttpServer.h - Minimal HTTP/1.1 server on EGTcp, dispatched from the
  main loop.

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
#pragma once

#include <Arduino.h>
#include <stdarg.h>

#include <EGTcp.h>
#include <EGBase64.h>

#include "EGHttpStrings.h"

#ifndef EGHTTP_MAX_CLIENTS
  #ifdef ESP32
    #define EGHTTP_MAX_CLIENTS 6
  #else
    #define EGHTTP_MAX_CLIENTS 5
  #endif
#endif
#ifndef EGHTTP_REQ_BUF
  // per client: request line, headers and a body that isn't streamed
  #define EGHTTP_REQ_BUF 1536
#endif
#ifndef EGHTTP_HEAP_MIN
  // free heap under this, a new request gets 503
  #define EGHTTP_HEAP_MIN 6144
#endif
#ifndef EGHTTP_CHUNK_ACC
  // esp32 only, esp8266 uses the Slot::buf tail
  #define EGHTTP_CHUNK_ACC 12288
#endif
#ifndef EGHTTP_MAX_UPLOADS
  #define EGHTTP_MAX_UPLOADS 4   // onUpload() routes
#endif
#ifndef EGHTTP_ARG_MAX
  #define EGHTTP_ARG_MAX 127   // longest arg() value
#endif
#ifndef EGHTTP_AUTH_MAX
  // user:password bytes
  #define EGHTTP_AUTH_MAX 64
#endif
#ifndef EGHTTP_REQ_MS
  // request in by then; a streamed body moves a window per this
  #define EGHTTP_REQ_MS 10000
#endif
#ifndef EGHTTP_SEND_MS
  // whole response, the loop waits on it
  #define EGHTTP_SEND_MS 15000
#endif
#ifndef EGHTTP_SEND_BUDGET_MS
  // no room to write this long, the send gives up
  #define EGHTTP_SEND_BUDGET_MS 2500
#endif
#ifndef EGHTTP_ACK_MS
  // sent, not acked this long, dropped
  #define EGHTTP_ACK_MS 5000
#endif
#ifndef EGHTTP_STREAM_IDLE_MS
  // streamed body silent this long; a slow flash write stalls the window
  #define EGHTTP_STREAM_IDLE_MS 60000
#endif
#ifndef EGHTTP_PULL
  #define EGHTTP_PULL 1   // 0 = no sendPull()
#endif
#ifndef EGHTTP_SAY_GAP_MS
  #define EGHTTP_SAY_GAP_MS 1000   // error lines, the rest counted
#endif
#ifndef EGHTTP_REPLIES_A_TICK
  #define EGHTTP_REPLIES_A_TICK 3   // each holds the loop for its render
#endif
#if defined(ESP32) && !defined(EGHTTP_MAX_INFLIGHT)
  // esp32 only, lwIP drops SYNs past this; the 8266 keeps EGTcp's backlog
  #define EGHTTP_MAX_INFLIGHT 10
#endif
// EGHTTP_DEBUG traces to Serial

class EGHttpServer;

class EGHttpRequest {
  public:
    enum Method : uint8_t { GET = 1, POST = 2, ANY = 0xFF };

    Method method() const { return _method; }
    const char* path() const { return _path; }   // PROGMEM for body handlers
    const char* query() const { return _query; }   // past '?', still encoded, or nullptr
    const char* body() const { return _body; }     // bodyLen bytes, bodyLen 0 on upload routes
    size_t bodyLen() const { return _bodyLen; }

    // query then body, nullptr if absent; static buffer, copy before next arg()
    const char* arg(const __FlashStringHelper* name) const;

    // out or nullptr; name may be PROGMEM
    static const char* decodeArg(const char* body, size_t bodyLen,
                                  const char* name, char* out, size_t cap);

  private:
    friend class EGHttpServer;
    Method _method = ANY;
    const char* _path = nullptr;
    const char* _query = nullptr;
    size_t _queryLen = 0;
    const char* _body = nullptr;
    size_t _bodyLen = 0;
};

// one send, sendGzipP, redirect or chunked reply per request, inside the handler
class EGHttpResponse {
  public:
    void send(uint16_t status, const __FlashStringHelper* contentType, const char* body);
    void send(uint16_t status, const __FlashStringHelper* contentType, const __FlashStringHelper* body);
    // body in flash, sent as acks come
    void sendGzipP(uint16_t status, const __FlashStringHelper* contentType,
                   const uint8_t* body, size_t len);
#if EGHTTP_PULL
    // up to room bytes of the body from at, 0 = no more
    using Fill = size_t (*)(void* ctx, size_t at, char* buf, size_t room);
    using Done = void (*)(void* ctx);
    // len bytes from fill() as acks come; done() once however it ends, even when refused
    void sendPull(uint16_t status, const __FlashStringHelper* contentType, size_t len,
                  Fill fill, Done done, void* ctx);
#endif
    // false = not sent, eg CR/LF in it
    bool redirect(const char* location);
    bool redirect(const __FlashStringHelper* location);

    // then sendChunk() / sendKV(), endChunked() to finish
    bool beginChunked(uint16_t status, const __FlashStringHelper* contentType);
    bool sendChunk(const char* data, size_t len);
    bool sendChunk(const char* data);
    bool sendChunk(const __FlashStringHelper* data);
    // prefix, value, suffix as one chunk
    bool sendKV(const __FlashStringHelper* prefix, const char* value);
    bool sendKV(const __FlashStringHelper* prefix, const char* value,
                const __FlashStringHelper* suffix);
    bool endChunked();

    // false = dropped: no room, CR/LF, or sent already
    bool addHeader(const __FlashStringHelper* name, const char* value);
    bool addHeader(const __FlashStringHelper* name, const __FlashStringHelper* value);

  private:
    friend class EGHttpServer;
    void* _slot = nullptr;        // EGHttpServer::Slot*
    char _extraHeaders[128] = {};
    size_t _extraLen = 0;
    bool _started = false;
    bool _chunked = false;
    bool _done = false;

    bool chunkOpen() const { return _slot && _chunked && !_done; }
    bool addHeaderP(const __FlashStringHelper* name, PGM_P value);
    bool redirectP(PGM_P location);
    void writeHeadAndBodyP(const char* head, size_t headLen, PGM_P body, size_t bodyLen);
};

class EGHttpServer {
  public:
    using Handler = void(*)(EGHttpRequest& req, EGHttpResponse& res, void* user);

    EGHttpServer() { strncpy_P(_authRealm, PSTR("Login"), sizeof(_authRealm) - 1); }
    ~EGHttpServer() { end(); free(_routes); }
    EGHttpServer(const EGHttpServer&) = delete;
    EGHttpServer& operator=(const EGHttpServer&) = delete;

    // main loop, register after on()
    enum BodyEvent : uint8_t {
      BODY_BEGIN = 1, BODY_DATA = 2, BODY_END = 3, BODY_ABORT = 4
    };
    // false refuses: rest not read, the route replies, no END/ABORT
    using BodyHandler = bool(*)(EGHttpRequest& req, BodyEvent ev,
                                const char* chunk, size_t len, void* user);

    // before begin(), first match wins; handlers run in tick()
    bool on(const __FlashStringHelper* path, EGHttpRequest::Method method, Handler h, void* user = nullptr);
    bool onUpload(const __FlashStringHelper* path, BodyHandler bh, void* user = nullptr);
    void onNotFound(Handler h, void* user = nullptr);   // default a plain 404

    // empty = no auth, user:password up to EGHTTP_AUTH_MAX
    void setBasicAuth(const __FlashStringHelper* user, const char* password,
                      const __FlashStringHelper* realm = nullptr);
    // a 401's text/plain body, PROGMEM; nullptr = empty
    void setAuthHelp(PGM_P text);
    // every server: a POST whose Origin isn't its Host gets 403; no Origin (curl, scripts) passes
    static void setSameOrigin(bool on) { s_sameOrigin = on; }

    // capped at EGHTTP_MAX_CLIENTS
    void setMaxClients(uint8_t n) { _maxClients = n < EGHTTP_MAX_CLIENTS ? n : EGHTTP_MAX_CLIENTS; }

    void begin(uint16_t port);
    void end();

    void tick();   // call from loop()
    // the owner's log for errors, fmt in flash; nullptr = Serial
    using LogFn = void (*)(const char* fmt, va_list ap);
    static void setLog(LogFn fn) { s_log = fn; }
    bool tickWanted() const { return _tickWanted; }   // work waiting, tick soon

    bool hasActiveClients() const;

  public:
    enum SlotState : uint8_t { IDLE, RECEIVING, READY, RESPONDING, DONE };
    enum StreamState : uint8_t { NO_STREAM, STREAM_WAIT, STREAM_OPEN, STREAM_ENDED, STREAM_ABORTED };
    static constexpr uint8_t NO_ROUTE = 0xFF;

    struct Slot {
      EGTcpClient* client = nullptr;   // resetSlot deletes it
      EGHttpServer* srv = nullptr;     // its callbacks' way back
      SlotState state = IDLE;
      uint16_t query = 0;   // in buf past '?', 0 = none
      char* buf = nullptr;
      size_t len = 0;
      size_t contentLength = 0;
      size_t headerEnd = 0;
      // onData can't yield, tick() drains
      StreamState stream = NO_STREAM;
      uint16_t ack_owed = 0;   // header bytes in first pbuf
      size_t bodyAcked = 0;
      bool send_aborted = false;   // sticky
      bool responded = false;
      uint8_t routeIdx = NO_ROUTE;
      char*  chunkAcc    = nullptr;
      size_t chunkAccLen = 0;
      size_t chunkAccCap = 0;
      PGM_P  tail        = nullptr;   // static body left, onSent sends it
      size_t tailLen     = 0;
#if EGHTTP_PULL
      EGHttpResponse::Fill pull = nullptr;   // the tail from here instead
      EGHttpResponse::Done pulled = nullptr;   // resetSlot runs it
      void*  pullCtx     = nullptr;
      size_t pullAt      = 0;
#endif
      uint32_t due       = 0;         // request, then response deadline
    };

  private:
    static LogFn s_log;
    static bool s_sameOrigin;
    static void say(PGM_P fmt, ...);
    struct Route {
      PGM_P path;
      EGHttpRequest::Method method;
      Handler handler;
      void* user;
    };
    struct Upload {
      uint8_t route;
      BodyHandler handler;
      void* user;
    };
    Route* _routes = nullptr;
    uint8_t _routeCount = 0;
    uint8_t _routeCap = 0;
    Upload _uploads[EGHTTP_MAX_UPLOADS];
    uint8_t _uploadCount = 0;
    Handler _notFound = nullptr;
    void* _notFoundUser = nullptr;

    EGTcpServer* _server = nullptr;
    Slot _slots[EGHTTP_MAX_CLIENTS];
    uint8_t _maxClients = EGHTTP_MAX_CLIENTS;

    // one TCP window
#ifdef ESP32
    static constexpr size_t STREAM_BUF_SIZE = 16384;   // TCP_WND ~16 KB
#else
    static constexpr size_t STREAM_BUF_SIZE = 6144;    // TCP_WND ~5840
#endif
    uint8_t*          _streamBuf  = nullptr;
    size_t            _streamLen  = 0;
    Slot*             _streamOwner = nullptr;

    bool              _tickWanted = false;
    uint32_t          _relistenAt = 0;

#ifdef ESP32
    char*  _dispatchChunkAcc    = nullptr;
#endif

    bool _authEnabled = false;
    char _authValue[EGB64_LEN(EGHTTP_AUTH_MAX) + 1] = {};   // base64, past "Basic "
    char _authRealm[40] = {};
    PGM_P _authHelp = nullptr;

    Slot* allocSlot(EGTcpClient* c);
    void  resetSlot(Slot* s);
    void  abortStream(Slot* s);
    void  markDone(Slot* s);
    void  sendStatusAndClose(Slot* s, const char* status, uint8_t retry_s = 0);   // nullptr = close only

    void  wireSlotCallbacks(Slot* s, EGTcpClient* c);
    bool  growRoutes();
    const Upload* uploadFor(uint8_t route) const;

    Route* matchedRoute(Slot* s) {
      if (s->routeIdx == NO_ROUTE || s->routeIdx >= _routeCount) return nullptr;
      return &_routes[s->routeIdx];
    }
    inline uint8_t findRoute(const char* path, EGHttpRequest::Method method) const;

    void relisten();
    void onClient(EGTcpClient* c);
    void onClosed(Slot* s);
    void takeData(Slot* s, const uint8_t* data, size_t len);
    void takeStream(Slot* s, const uint8_t* data, size_t len);
    inline void receiveHeaders(Slot* s, const uint8_t* data, size_t len);
    inline void beginStream(Slot* s, size_t len);
    void releaseStream();
    void identifyRoute(Slot* s);
    void drainStream(Slot* s);
    bool deliverBodyEvent(Slot* s, BodyEvent ev, const char* chunk, size_t len);
    void refuseStream(Slot* s);
    void streamTaken(Slot* s, size_t n);
    inline void reclaimSlot(Slot* s);
    inline void respond(Slot* s);
    void finish(Slot* s);
    void drop(Slot* s);
    void sentMore(Slot* s);
    inline void claimChunkAcc(Slot* s);
    void dropChunkAcc(Slot* s);
    bool readRequest(Slot* s, EGHttpRequest& req);
    inline bool authorized(const Slot* s) const;
    inline bool crossSite(const Slot* s) const;
    inline bool argsFit(const Slot* s, const EGHttpRequest& req) const;
    uint16_t refusal(const Slot* s, const EGHttpRequest& req) const;   // 0, 403, 401 or 400
    void sendUnauthorized(Slot* s);
    void dispatch(Slot* s);
};
