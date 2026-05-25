/*
  EGHttpServer.h - Minimal HTTP/1.1 server on (ESP)AsyncTCP, dispatched from
  the main loop.

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

#if defined(ESP8266)
  #include <EGAsyncTCP.h>
#elif defined(ESP32)
  #include <AsyncTCP.h>
#endif

#ifndef EGHTTP_MAX_CLIENTS
  // Queued clients silently lose inbound data: AsyncTCP ACKs and frees
  // pbufs on connections that don't yet have onData registered. Slot
  // pool must directly cover the realistic parallel-fetch burst. ESP8266
  // trades depth for heap; 3 slots fits a typical home-page in practice.
  #ifdef ESP32
    #define EGHTTP_MAX_CLIENTS 6
  #else
    #define EGHTTP_MAX_CLIENTS 5
  #endif
#endif
#ifndef EGHTTP_WAIT_QUEUE
  // Last-resort overflow only; queued clients lose inbound data. Deeper
  // queue on ESP8266 compensates for the smaller slot pool.
  #ifdef ESP32
    #define EGHTTP_WAIT_QUEUE 1
  #else
    #define EGHTTP_WAIT_QUEUE 3
  #endif
#endif
#ifndef EGHTTP_REQ_BUF
  // Browsers send 700-900 B of headers (sec-ch-ua + Sec-Fetch-* + cookies).
  #define EGHTTP_REQ_BUF 1536
#endif
#ifndef EGHTTP_BODY_SCRATCH
  // Max inline body for res.send(char*, ...); larger needs beginChunked.
  #define EGHTTP_BODY_SCRATCH 128
#endif
#ifndef EGHTTP_CHUNK_ACC
  // ESP32: dedicated chunked-response accumulator. Single flush at endChunked
  // closes the AsyncTCP-task-race window that truncates mid-response.
  // ESP8266: not used as a separate alloc - the slot's request buffer
  // (Slot::buf, EGHTTP_REQ_BUF bytes) is repurposed as the accumulator
  // during RESPONDING using its tail (after request headers + body).
  #define EGHTTP_CHUNK_ACC 12288
#endif
#ifndef EGHTTP_MAX_ROUTES
  #define EGHTTP_MAX_ROUTES 64
#endif

class EGHttpServer;

class EGHttpRequest {
  public:
    enum Method : uint8_t { GET = 1, POST = 2, ANY = 0xFF };

    Method method() const { return _method; }
    const char* path() const { return _path; }
    const char* query() const { return _query; }
    const char* body() const { return _body; }
    size_t bodyLen() const { return _bodyLen; }

    // Result points into a static decode buffer; copy before next arg().
    const char* arg(const char* name) const;

    // For streaming routes that hold the body in their own buffer.
    // Same static-decode-buffer caveat as arg().
    static const char* decodeArg(const char* body, size_t bodyLen,
                                  const char* name);

  private:
    friend class EGHttpServer;
    Method _method = ANY;
    const char* _path = nullptr;
    const char* _query = nullptr;
    size_t _queryLen = 0;
    const char* _body = nullptr;
    size_t _bodyLen = 0;
};

class EGHttpResponse {
  public:
    void send(uint16_t status, const char* contentType, const char* body);
    void send(uint16_t status, const char* contentType, const __FlashStringHelper* body);
    // Length-prefixed PROGMEM body with Content-Encoding: gzip header.
    void sendGzipP(uint16_t status, const char* contentType,
                   const uint8_t* body, size_t len);
    void redirect(const char* location);

    // Returns false if connection dropped mid-response.
    bool beginChunked(uint16_t status, const char* contentType);
    bool sendChunk(const char* data, size_t len);
    bool sendChunk(const char* data);   // strlen()s internally
    bool sendChunk(const __FlashStringHelper* data);
    // One-chunk PROGMEM-prefix + value, saves per-chunk framing.
    bool sendKV(const __FlashStringHelper* prefix, const char* value);
    bool sendKV(const __FlashStringHelper* prefix, const char* value,
                const __FlashStringHelper* suffix);
    bool endChunked();

    void addHeader(const char* name, const char* value);

  private:
    friend class EGHttpServer;
    void* _slot = nullptr;        // EGHttpServer::Slot*
    char _extraHeaders[128] = {0};
    size_t _extraLen = 0;
    bool _started = false;        // headers emitted
    bool _chunked = false;
    bool _done = false;
};

class EGHttpServer {
  public:
    using Handler = void(*)(EGHttpRequest& req, EGHttpResponse& res, void* user);

    // Streaming-body POST. BodyHandler runs in main-loop context so
    // flash writes are safe. Register onUpload(path, ...) AFTER on(); the
    // main handler runs once after BODY_END to send the final response.
    //
    //   BODY_BEGIN  headers parsed, body about to start
    //   BODY_DATA   chunk arrived
    //   BODY_END    Content-Length reached cleanly
    //   BODY_ABORT  connection lost / error mid-body
    enum BodyEvent : uint8_t {
      BODY_BEGIN = 1, BODY_DATA = 2, BODY_END = 3, BODY_ABORT = 4
    };
    using BodyHandler = void(*)(EGHttpRequest& req, BodyEvent ev,
                                const char* data, size_t len, void* user);

    void on(const char* path, EGHttpRequest::Method method, Handler h, void* user = nullptr);
    void onUpload(const char* path, BodyHandler bh, void* user = nullptr);
    void onNotFound(Handler h, void* user = nullptr);

    // Empty/null password disables auth. Effective on next request.
    void setBasicAuth(const char* user, const char* password,
                      const char* realm = "ESPGeiger");

    void begin(uint16_t port);
    void end();

    // Drives dispatch + slot reclaim. Call from loop().
    void tick();

  public:
    enum SlotState : uint8_t { IDLE, RECEIVING, READY, RESPONDING, DONE };

    struct Slot {
      // ESP32 AsyncTCP writes from its own task; without volatile the
      // main task can miss updates and the slot wedges forever.
      AsyncClient* client = nullptr;
      volatile SlotState state = IDLE;
      volatile bool delete_pending = false;
      // buf heap-alloc'd at allocSlot, freed at resetSlot. Same-size
      // allocs let umm reuse freed buffers without fragmenting.
      char* buf = nullptr;
      volatile size_t len = 0;
      volatile bool headersDone = false;
      size_t contentLength = 0;
      size_t headerEnd = 0;
      // onData buffers into shared _streamBuf (Update.write/yield panic
      // from lwIP context); tick() drains in main-loop context.
      volatile bool streaming = false;
      volatile bool stream_begun = false;
      volatile bool stream_ended = false;
      volatile bool stream_aborted = false;
      volatile bool ack_paused = false;
      volatile size_t bodyAcked = 0;
      // Sticky once a send fails (timeout, disconnect, write rejected).
      // Subsequent sendChunk calls fast-return false without spinning, so
      // handlers that ignore return values don't pay 1 budget per chunk.
      volatile bool send_aborted = false;
      uint8_t routeIdx = 0xFF;
      // Chunked-response accumulator. ESP32: points into a server-owned
      // 12 KB heap buffer claimed at dispatch entry. ESP8266: points into
      // the slot's own request buffer tail (Slot::buf + headerEnd + bodyLen),
      // reusing already-allocated heap. chunkAccCap is the available size
      // (fixed on ESP32, variable per-request on ESP8266). dispatch() is
      // serialised in tick() so a single accumulator across slots is safe.
      char*  chunkAcc    = nullptr;
      size_t chunkAccLen = 0;
      size_t chunkAccCap = 0;
    };

  private:
    struct Route {
      const char* path;
      EGHttpRequest::Method method;
      Handler handler;
      void* user;
      BodyHandler bodyHandler;
      void* bodyUser;
    };
    Route _routes[EGHTTP_MAX_ROUTES];
    uint8_t _routeCount = 0;
    Handler _notFound = nullptr;
    void* _notFoundUser = nullptr;

    AsyncServer* _server = nullptr;
    Slot _slots[EGHTTP_MAX_CLIENTS];

    // Pointers only; lwIP buffers their inbound data until promotion.
    AsyncClient* _waitQueue[EGHTTP_WAIT_QUEUE] = {};
    uint8_t      _waitCount = 0;

    // Streaming-body buffer, alloc'd only while a stream is active.
    // Must hold one TCP receive window so the peer's initial burst fits
    // before we ack. After that, ackLater-per-packet throttles to ~1 seg
    // per RTT so sustained traffic doesn't need more headroom.
#ifdef ESP32
    static constexpr size_t STREAM_BUF_SIZE = 16384;   // TCP_WND ~16 KB
#else
    static constexpr size_t STREAM_BUF_SIZE = 6144;    // TCP_WND ~5840
#endif
    uint8_t*          _streamBuf  = nullptr;
    volatile size_t   _streamLen  = 0;
    Slot*             _streamOwner = nullptr;

    // tick() fast-path skip. Set by callbacks and dispatch when there's
    // anything for tick to do. Cleared optimistically at top of tick; any
    // callback that fires during the body will re-set it.
    volatile bool     _tickWanted = false;

#ifdef ESP32
    // Dispatch-shared chunk accumulator. Lazy-alloc on first dispatch,
    // kept alive (one-time cost, no per-request churn).
    char*  _dispatchChunkAcc    = nullptr;
#endif

    // _authValue: precomputed "Basic <base64(user:pass)>" for strncmp.
    bool _authEnabled = false;
    char _authValue[96] = {0};
    char _authRealm[40] = "ESPGeiger";

    Slot* findSlot(AsyncClient* c);
    Slot* allocSlot(AsyncClient* c);
    void  resetSlot(Slot* s);
    // Mark slot for tick reclaim (state triple, no client touch).
    inline void markDone(Slot* s) {
      s->state          = DONE;
      s->delete_pending = true;
      _tickWanted       = true;
    }
    // Write status template + close + markDone. body=nullptr = close only.
    void  sendStatusAndClose(Slot* s, const char* body, size_t len);

    bool  enqueueWaiter(AsyncClient* c);
    void  dropWaiter(AsyncClient* c);
    void  promoteWaiters();
    void  wireSlotCallbacks(Slot* s, AsyncClient* c);

    void onClient(AsyncClient* c);
    void onData(Slot* s, void* data, size_t len);
    void parseRequest(Slot* s);
    void identifyRoute(Slot* s);
    void drainStream(Slot* s);
    void deliverBodyEvent(Slot* s, BodyEvent ev, const char* data, size_t len);
    void dispatch(Slot* s);
};
