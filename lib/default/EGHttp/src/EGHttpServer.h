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
  #include <ESPAsyncTCP.h>
#elif defined(ESP32)
  #include <AsyncTCP.h>
#endif

#ifndef EGHTTP_MAX_CLIENTS
  // AsyncTCP drops inbound bytes on connections that don't have an
  // onData callback registered yet — they're ACK'd by lwIP and the
  // pbuf is freed. Queued clients (which only get onDisconnect+onError
  // until promoted) silently lose their request data. Confirmed on
  // both platforms (10 May 2026).
  // Slot pool must directly cover the realistic parallel-fetch burst.
  // 6 = matches the typical browser per-host parallel limit (Chrome,
  // Firefox, Safari). BSS cost per slot is bookkeeping only (~50 B);
  // buffers are heap-allocated lazily, so peak heap during burst is
  // ~6 × 1 KB. Bump this if you legitimately need more concurrency.
  #define EGHTTP_MAX_CLIENTS 6
#endif
#ifndef EGHTTP_WAIT_QUEUE
  // Last-resort overflow only. Queued clients DO lose any inbound data
  // that arrives before promotion — this is set to 1 just so we don't
  // immediately 503 a benign one-over-the-limit accept.
  #define EGHTTP_WAIT_QUEUE 1
#endif
#ifndef EGHTTP_REQ_BUF
  // Holds the request line + headers + (for non-streaming POSTs) the
  // form body. Streaming routes (/update, /param) bypass this — their
  // bodies pipe into a route-owned heap buffer instead. 768 B fits any
  // real-world request line + headers comfortably.
  #define EGHTTP_REQ_BUF 768
#endif
#ifndef EGHTTP_BODY_SCRATCH
  // Inline body copy buffer for res.send(char*, char*) - bodies larger
  // than this use beginChunked() instead. Heap-allocated alongside
  // Slot::buf so this only consumes RAM while a slot is active.
  #define EGHTTP_BODY_SCRATCH 128
#endif
#ifndef EGHTTP_CHUNK_ACC
  // ESP32 only: per-slot chunked-response accumulator. Sized to fit
  // the largest realistic page (/status: picographJS ~5 KB + statusJS
  // ~2 KB + HTML chrome ~2 KB ≈ 9 KB) in one buffer so endChunked()
  // flushes in a single write rather than multiple, each of which is
  // a chance for the AsyncTCP-task race to clip the response. 12 KB
  // × 3 slots = 36 KB peak heap during burst (320 KB available).
  #define EGHTTP_CHUNK_ACC 12288
#endif
#ifndef EGHTTP_MAX_ROUTES
  // Current route count after Phase B (counted at boot):
  //   WebPortal cross-cutting    20  (/, /style.css, /favicon.{svg,ico},
  //                                   /restart, /reset, /erase, /info,
  //                                   /about, /status, /js, /cs, /outputs,
  //                                   /param GET+POST, /update GET+POST,
  //                                   /wifi, /wifiscan, /wifisave)
  //   Counter                    4   (/clicks /lastdata /json /hist  + /cpm on test)
  //   HV (when built)            4   (/hv /hvset /hvjs /hvjson)
  //   NTP_Client                 3   (/ntp /ntpset /ntpjs)
  //   WebAPI                     4   (/webapi GET+POST /webapikeyreset /webapiforget)
  //   GRNG                       2   (/random /random.do)
  //   SerialOut                  1   (/serial)
  // → ~38 active. 48 leaves room for upcoming additions; each entry is
  //   ~24 B (path ptr + method + 2 handler ptrs + 2 user ptrs).
  #define EGHTTP_MAX_ROUTES 48
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

    // Form-urlencoded arg lookup — searches query string then POST body.
    // Returns nullptr if absent. Result points into a static decode buffer;
    // copy it out before the next arg() call.
    const char* arg(const char* name) const;

    // Form-urlencoded arg lookup against an externally-provided buffer.
    // Used by streaming routes that accumulate the body into their own
    // heap allocation rather than the slot buffer (e.g. /param POST).
    // Returns nullptr if absent. Result lives in the same static decode
    // buffer as arg() — same consume-before-next-call rule applies.
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
    // One-shot responses. Headers + body are emitted to the wire before the
    // call returns (with yield-retry on TCP back-pressure).
    void send(uint16_t status, const char* contentType, const char* body);
    void send(uint16_t status, const char* contentType, const __FlashStringHelper* body);
    void redirect(const char* location);

    // Streaming response. beginChunked emits the response line + headers
    // including `Transfer-Encoding: chunked`. Each sendChunk emits one
    // chunk on the wire (size header + data + trailer). endChunked emits
    // the zero-length terminating chunk.
    //
    // No accumulator buffer — bytes go straight to TCP, with yield-retry
    // when the send buffer is full. Returns false if the connection has
    // been disconnected mid-response.
    bool beginChunked(uint16_t status, const char* contentType);
    bool sendChunk(const char* data, size_t len);
    bool sendChunk(const char* data);   // strlen()s data internally
    bool sendChunk(const __FlashStringHelper* data);
    // Combined PROGMEM-prefix + dynamic-value chunk. Emits ONE chunk on
    // the wire (vs two for sendChunk(F(prefix)) + sendChunk(value, len)),
    // saving the per-chunk size header / trailer overhead.
    bool sendKV(const __FlashStringHelper* prefix, const char* value);
    bool sendKV(const __FlashStringHelper* prefix, const char* value,
                const __FlashStringHelper* suffix);
    bool endChunked();

    // Optional extra header to set before send/beginChunked.
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

    // Streaming-body POST. Bodies that don't fit (or shouldn't fit) in
    // EGHTTP_REQ_BUF — e.g. firmware uploads — are delivered chunk-by-chunk
    // to a BodyHandler from main-loop context (so flash writes are safe).
    //
    //   BODY_BEGIN  data=null, len=0   — headers parsed, body about to start
    //   BODY_DATA   data, len           — chunk arrived, deliver to receiver
    //   BODY_END    data=null, len=0   — Content-Length reached cleanly
    //   BODY_ABORT  data=null, len=0   — connection lost / error mid-body
    //
    // Register with onUpload(path, body_handler) AFTER registering the
    // route's main handler with on(). The main handler runs once after
    // BODY_END to send the final response.
    enum BodyEvent : uint8_t {
      BODY_BEGIN = 1, BODY_DATA = 2, BODY_END = 3, BODY_ABORT = 4
    };
    using BodyHandler = void(*)(EGHttpRequest& req, BodyEvent ev,
                                const char* data, size_t len, void* user);

    void on(const char* path, EGHttpRequest::Method method, Handler h, void* user = nullptr);
    void onUpload(const char* path, BodyHandler bh, void* user = nullptr);
    void onNotFound(Handler h, void* user = nullptr);

    // Optional HTTP Basic Auth. Calling with a non-empty password requires
    // every request to carry `Authorization: Basic base64(user:password)`;
    // missing/wrong gets 401 with WWW-Authenticate. Empty password (or
    // null) disables auth entirely. Realm shown to the browser is "realm".
    // Caller may change at any time; effective on the next request.
    void setBasicAuth(const char* user, const char* password,
                      const char* realm = "ESPGeiger");

    void begin(uint16_t port);
    void end();

    // Drive request dispatch + slot reclaim from the main loop. Call
    // frequently (e.g. every loop()).
    void tick();

  public:
    enum SlotState : uint8_t { IDLE, RECEIVING, READY, RESPONDING, DONE };

    struct Slot {
      // volatile on cross-task fields: AsyncTCP runs in its own task on
      // ESP32 and writes state/delete_pending/etc from there, while
      // tick() reads them in the main task. Without volatile the compiler
      // can cache reads across calls and the main task never sees the
      // write — slot wedges READY/DONE forever, queue stops promoting.
      // ESP8266 NONOS is cooperative single-thread; volatile is harmless.
      AsyncClient* client = nullptr;
      volatile SlotState state = IDLE;
      volatile bool delete_pending = false;
      // Request parsing state. buf + bodyScratch are heap-allocated at
      // allocSlot() time and freed at resetSlot(). Idle slots cost only
      // their bookkeeping fields in BSS (~30 B). The two allocations are
      // fixed-size (EGHTTP_REQ_BUF / EGHTTP_BODY_SCRATCH) so umm_malloc
      // reuses freed slot buffers in place rather than fragmenting.
      char* buf = nullptr;
      volatile size_t len = 0;
      volatile bool headersDone = false;
      size_t contentLength = 0;
      size_t headerEnd = 0;
      // Streaming-body state — set after headers are parsed if the matched
      // route has a BodyHandler. onData buffers body bytes into the shared
      // _streamBuf (because Update.write/yield panic from lwIP context);
      // tick() drains the buffer to the handler in main-loop context.
      volatile bool streaming = false;
      volatile bool stream_begun = false;
      volatile bool stream_ended = false;
      volatile bool stream_aborted = false;
      volatile bool ack_paused = false;
      volatile size_t bodyAcked = 0;        // total body bytes delivered to handler
      uint8_t routeIdx = 0xFF;     // 0xFF = no match yet / unparsed
      // Response side: small scratch for auto-copy of short dynamic
      // res.send(char*) bodies. Other paths (PROGMEM, chunked) write
      // directly to TCP without buffering. Heap-allocated alongside buf.
      char* bodyScratch = nullptr;
#ifdef ESP32
      // Chunked-response accumulator. ESP32 AsyncTCP refuses writes when
      // _pcb->state != ESTABLISHED — under multi-task burst the AsyncTCP
      // task can flip state between our sendChunks (peer FIN, internal
      // cleanup) and the response truncates mid-stream. Accumulating the
      // entire chunked body into one buffer + flushing once at endChunked
      // shrinks the window from 30+ AsyncClient calls to 1. Heap-alloc'd
      // alongside buf; freed at resetSlot. ESP8266's cooperative scheduler
      // doesn't have this race so the path stays direct-write there.
      char* chunkAcc      = nullptr;
      size_t chunkAccLen  = 0;
#endif
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
    // Route lives in BSS array - zero-init guarantees bodyHandler/bodyUser
    // start nullptr without explicit member defaults (ESP32 toolchain rejects
    // brace-init combined with in-class member initializers).

    Route _routes[EGHTTP_MAX_ROUTES];
    uint8_t _routeCount = 0;
    Handler _notFound = nullptr;
    void* _notFoundUser = nullptr;

    AsyncServer* _server = nullptr;
    Slot _slots[EGHTTP_MAX_CLIENTS];

    // Wait queue: clients accepted while all slots are busy. lwIP holds
    // their inbound TCP data until tick() promotes them into a freed
    // slot and registers onData. Pointers only, ~16 B BSS.
    AsyncClient* _waitQueue[EGHTTP_WAIT_QUEUE] = {};
    uint8_t      _waitCount = 0;

    // Streaming-body buffer. Sized to absorb one TCP receive window
    // (~5.8 KB for ESP8266 lwIPv2) so onData can keep writing while
    // tick() catches up — main loop drains via deliverBodyEvent where
    // Update.write/yield are safe. Heap-allocated only during an active
    // upload so it costs zero when idle. Only one stream at a time;
    // concurrent streaming attempts get 503.
    // _streamLen volatile: onData (AsyncTCP task) appends + increments;
    // drainStream (main task) snapshots, delivers, memmoves, decrements.
    // ESP32 needs the volatile to flush the writes across cores.
    // Must be >= one full TCP receive window so peer's initial burst
    // fits before we start ack-ing. We use the original "ackLater on
    // every packet" pattern in onData (not "ackLater once") - it
    // throttles peer to roughly one segment per ACK round-trip after
    // the first burst, so we don't need a buffer big enough to hold
    // sustained traffic, just the initial TCP_WND window.
    // ESP32 framework TCP_WND tends to be ~16 KB; bump for headroom.
    // ESP8266 framework TCP_WND ≈ 5840 → 6 KB.
#ifdef ESP32
    static constexpr size_t STREAM_BUF_SIZE = 16384;
#else
    static constexpr size_t STREAM_BUF_SIZE = 6144;
#endif
    uint8_t*          _streamBuf  = nullptr;
    volatile size_t   _streamLen  = 0;
    Slot*             _streamOwner = nullptr;

    // Basic Auth state. Populated by setBasicAuth(). _authValue holds
    // the precomputed "Basic <base64(user:pass)>" header value so the
    // dispatch-time check is a single strncmp.
    bool _authEnabled = false;
    char _authValue[96] = {0};
    char _authRealm[40] = "ESPGeiger";

    Slot* findSlot(AsyncClient* c);
    Slot* allocSlot(AsyncClient* c);
    void  resetSlot(Slot* s);

    // Wait-queue management. enqueueWaiter/dropWaiter mutate _waitQueue;
    // promoteWaiters runs from tick() and shifts queued clients into
    // freed slots, registering their onData callback at that point.
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
