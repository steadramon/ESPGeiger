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

#include <EGAsyncTCP.h>
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
  #define EGHTTP_REQ_BUF 1536
#endif
#ifndef EGHTTP_CHUNK_ACC
  // esp32 only, esp8266 uses the Slot::buf tail
  #define EGHTTP_CHUNK_ACC 12288
#endif
#ifndef EGHTTP_MAX_UPLOADS
  #define EGHTTP_MAX_UPLOADS 4
#endif
#ifndef EGHTTP_ARG_MAX
  #define EGHTTP_ARG_MAX 127   // longest arg() value
#endif
#ifndef EGHTTP_AUTH_MAX
  // user:password bytes
  #define EGHTTP_AUTH_MAX 64
#endif
#ifndef EGHTTP_MAX_INFLIGHT
  // FIN past this
  #ifdef ESP32
    #define EGHTTP_MAX_INFLIGHT 10
  #else
    #define EGHTTP_MAX_INFLIGHT 8
  #endif
#endif
// EGHTTP_DEBUG traces to Serial

class EGHttpServer;

class EGHttpRequest {
  public:
    enum Method : uint8_t { GET = 1, POST = 2, ANY = 0xFF };

    Method method() const { return _method; }
    const char* path() const { return _path; }   // PROGMEM for body handlers
    const char* query() const { return _query; }
    const char* body() const { return _body; }
    size_t bodyLen() const { return _bodyLen; }

    // static buffer, copy before next arg()
    const char* arg(const __FlashStringHelper* name) const;

    // name may be PROGMEM
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

class EGHttpResponse {
  public:
    void send(uint16_t status, const __FlashStringHelper* contentType, const char* body);
    void send(uint16_t status, const __FlashStringHelper* contentType, const __FlashStringHelper* body);
    void sendGzipP(uint16_t status, const __FlashStringHelper* contentType,
                   const uint8_t* body, size_t len);
    void redirect(const char* location);
    void redirect(const __FlashStringHelper* location);

    bool beginChunked(uint16_t status, const __FlashStringHelper* contentType);
    bool sendChunk(const char* data, size_t len);
    bool sendChunk(const char* data);
    bool sendChunk(const __FlashStringHelper* data);
    bool sendKV(const __FlashStringHelper* prefix, const char* value);
    bool sendKV(const __FlashStringHelper* prefix, const char* value,
                const __FlashStringHelper* suffix);
    bool endChunked();

    void addHeader(const __FlashStringHelper* name, const char* value);
    void addHeader(const __FlashStringHelper* name, const __FlashStringHelper* value);

  private:
    friend class EGHttpServer;
    void* _slot = nullptr;        // EGHttpServer::Slot*
    char _extraHeaders[128] = {0};
    size_t _extraLen = 0;
    bool _started = false;
    bool _chunked = false;
    bool _done = false;

    bool chunkOpen() const { return _slot && _chunked && !_done; }
    void addHeaderP(const __FlashStringHelper* name, PGM_P value);
    void redirectP(PGM_P location);
    void writeHeadAndBodyP(const char* head, size_t headLen, PGM_P body, size_t bodyLen);
};

class EGHttpServer {
  public:
    using Handler = void(*)(EGHttpRequest& req, EGHttpResponse& res, void* user);

    EGHttpServer() = default;
    ~EGHttpServer() { end(); free(_routes); }
    EGHttpServer(const EGHttpServer&) = delete;
    EGHttpServer& operator=(const EGHttpServer&) = delete;

    // main loop, register after on()
    enum BodyEvent : uint8_t {
      BODY_BEGIN = 1, BODY_DATA = 2, BODY_END = 3, BODY_ABORT = 4
    };
    using BodyHandler = void(*)(EGHttpRequest& req, BodyEvent ev,
                                const char* chunk, size_t len, void* user);

    bool on(const __FlashStringHelper* path, EGHttpRequest::Method method, Handler h, void* user = nullptr);
    bool onUpload(const __FlashStringHelper* path, BodyHandler bh, void* user = nullptr);
    void onNotFound(Handler h, void* user = nullptr);

    // empty = no auth
    void setBasicAuth(const __FlashStringHelper* user, const char* password,
                      const __FlashStringHelper* realm = nullptr);

    void setMaxClients(uint8_t n) { _maxClients = n < EGHTTP_MAX_CLIENTS ? n : EGHTTP_MAX_CLIENTS; }

    void begin(uint16_t port);
    void end();

    void tick();   // call from loop()

    bool hasActiveClients() const;

  public:
    enum SlotState : uint8_t { IDLE, RECEIVING, READY, RESPONDING, DONE };

    struct Slot {
      // esp32 AsyncTCP task writes these
      AsyncClient* client = nullptr;
      volatile SlotState state = IDLE;
      volatile bool delete_pending = false;
      volatile bool gone = false;   // resetSlot deletes client
      char* buf = nullptr;
      volatile size_t len = 0;
      volatile bool headersDone = false;
      size_t contentLength = 0;
      size_t headerEnd = 0;
      // lwIP can't yield, tick() drains
      volatile bool streaming = false;
      volatile bool stream_begun = false;
      volatile bool stream_ended = false;
      volatile bool stream_aborted = false;
      volatile bool ack_paused = false;
      uint16_t ack_owed = 0;   // header bytes in first pbuf
      volatile size_t bodyAcked = 0;
      volatile bool send_aborted = false;   // sticky
      bool responded = false;
      uint8_t routeIdx = 0xFF;   // 0xFF = no route
      char*  chunkAcc    = nullptr;
      size_t chunkAccLen = 0;
      size_t chunkAccCap = 0;
    };

  private:
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

    AsyncServer* _server = nullptr;
    Slot _slots[EGHTTP_MAX_CLIENTS];
    uint8_t _maxClients = EGHTTP_MAX_CLIENTS;

    // one TCP window
#ifdef ESP32
    static constexpr size_t STREAM_BUF_SIZE = 16384;   // TCP_WND ~16 KB
#else
    static constexpr size_t STREAM_BUF_SIZE = 6144;    // TCP_WND ~5840
#endif
    uint8_t*          _streamBuf  = nullptr;
    volatile size_t   _streamLen  = 0;
    Slot*             _streamOwner = nullptr;

    volatile bool     _tickWanted = false;

    volatile uint8_t  _inFlight    = 0;
    volatile uint16_t _rejectCount = 0;

#ifdef ESP32
    char*  _dispatchChunkAcc    = nullptr;
#endif

    bool _authEnabled = false;
    char _authValue[6 + EGB64_LEN(EGHTTP_AUTH_MAX) + 1] = {0};   // "Basic " + base64
    char _authRealm[40] = "ESPGeiger";

    Slot* findSlot(AsyncClient* c);
    Slot* allocSlot(AsyncClient* c);
    void  resetSlot(Slot* s);
    void  markDone(Slot* s);
    // AsyncTCP task, flag only
    bool markDoneAsync(AsyncClient* c, int* streaming, unsigned* acked);
    void  sendStatusAndClose(Slot* s, const char* body, size_t len);   // nullptr = close only

    void  wireSlotCallbacks(Slot* s, AsyncClient* c);
    bool  growRoutes();
    const Upload* uploadFor(uint8_t route) const;

    Route* matchedRoute(Slot* s) {
      if (s->routeIdx == 0xFF || s->routeIdx >= _routeCount) return nullptr;
      return &_routes[s->routeIdx];
    }
    inline uint8_t findRoute(const char* path, EGHttpRequest::Method method) const;

    void onClient(AsyncClient* c);
    void onDisconnect(AsyncClient* c);
    void onData(Slot* s, AsyncClient* c, void* data, size_t len);
    void takeData(Slot* s, AsyncClient* c, void* data, size_t len);
    void takeStream(Slot* s, AsyncClient* c, void* data, size_t len);
    inline void receiveHeaders(Slot* s, void* data, size_t len);
    inline void beginStream(Slot* s, size_t len);
    void releaseStream();
    void identifyRoute(Slot* s);
    void drainStream(Slot* s);
    void deliverBodyEvent(Slot* s, BodyEvent ev, const char* chunk, size_t len);
    inline void reclaimSlot(Slot* s);
    inline void respond(Slot* s);
    inline void claimChunkAcc(Slot* s);
    void dropChunkAcc(Slot* s);
    inline bool readRequest(Slot* s, EGHttpRequest& req);
    inline bool authorized(const Slot* s) const;
    void sendUnauthorized(Slot* s);
    void dispatch(Slot* s);
};
