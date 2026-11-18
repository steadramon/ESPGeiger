/*
  EGHttpClient.h - small HTTP/1.1 client, run from the main loop.

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
#ifndef EGHTTPCLIENT_H
#define EGHTTPCLIENT_H

#include <Arduino.h>
#include <EGTcp.h>

// below 0 = client side
#define HTTPCODE_NOT_CONNECTED   (-4)
#define HTTPCODE_CONNECTION_LOST (-5)
#define HTTPCODE_NO_HTTP_SERVER  (-7)
#define HTTPCODE_TOO_LESS_RAM    (-8)
#define HTTPCODE_ENCODING        (-9)
#define HTTPCODE_STREAM_WRITE    (-10)
#define HTTPCODE_TIMEOUT         (-11)
#define HTTPCODE_BAD_URL         (-12)   // urlWhy() says why

#ifndef EGHTTP_HOST_MAX
  #define EGHTTP_HOST_MAX 64   // longer is refused
#endif
#ifndef EGHTTP_SINK
  #define EGHTTP_SINK 1        // 0 = no setSink()
#endif
#ifndef EGHTTP_REDIRECTS
  #define EGHTTP_REDIRECTS 1   // 0 = no setRedirects(), 3xx never followed
#endif
#ifndef EGHTTP_BODY_CAP
  #define EGHTTP_BODY_CAP 256  // body kept without a sink, the rest dropped
#endif
#ifndef EGHTTP_LINE_CAP
  #define EGHTTP_LINE_CAP 2048 // per reply line
#endif
#ifndef EGHTTP_HEAD_CAP
  #define EGHTTP_HEAD_CAP 8192 // all reply headers
#endif
#ifndef EGHTTP_IN_CAP
  #define EGHTTP_IN_CAP 256    // read buffer, the reply only
#endif
#ifndef EGHTTP_URL_P_MAX
  #define EGHTTP_URL_P_MAX 127 // F() url for open()
#endif

// http only, one at a time, headers after open(), sink and redirects before send()
class EGHttpClient {
  public:
    // Returns bytes taken, below 0 to stop
    typedef int (*Sink)(void* arg, const uint8_t* buf, size_t len);
    typedef void (*Wake)(void* arg);

    static const size_t BODY_CAP = EGHTTP_BODY_CAP;
    static const size_t LINE_CAP = EGHTTP_LINE_CAP;
    static const size_t HEAD_CAP = EGHTTP_HEAD_CAP;
    static const size_t IN_CAP = EGHTTP_IN_CAP;

    EGHttpClient();
    ~EGHttpClient();
    EGHttpClient(const EGHttpClient&) = delete;
    EGHttpClient& operator=(const EGHttpClient&) = delete;

    __attribute__((warn_unused_result))
    bool open(const char* method, const char* url);   // dials
    static const size_t URL_P_MAX = EGHTTP_URL_P_MAX;
    // url up to URL_P_MAX
    __attribute__((warn_unused_result))
    bool open(const __FlashStringHelper* method, const __FlashStringHelper* url);
    // false if dropped. Host, Connection and the lengths are set by the client. One that didn't fit makes send() fail
    bool setReqHeader(const char* name, const char* value);   // send() says
    bool setReqHeader(const __FlashStringHelper* name, const char* value);
    bool setReqHeader(const __FlashStringHelper* name, const __FlashStringHelper* value);
    void setTimeoutS(uint16_t secs) { _timeoutS = secs; }   // max silence, default 10
    void setDeadline(uint32_t ms) { _deadlineMs = ms; }    // whole request, 0 = none
#if EGHTTP_REDIRECTS
    void setRedirects(uint8_t n) { _redirects = n; }       // http only
#endif
#if EGHTTP_SINK
    void setSink(Sink fn, void* arg) { _sink = fn; _sinkArg = arg; }
#endif
    // Called once when it ends, on the owner's task. Not called on abort()
    void setWake(Wake fn, void* arg) { _wake = fn; _wakeArg = arg; }
    // Once per open(), the body is copied. false if not open or no ram
    __attribute__((warn_unused_result))
    bool send(const char* body = nullptr);
    __attribute__((warn_unused_result))
    bool send(const uint8_t* body, size_t len);
    void abort();

    // With no sink or redirects the reply is read as it lands
    bool idle();
    bool takeDone();                       // true once per request
    uint32_t due() const;                  // EGClock ms a timeout could end it

    int responseHTTPcode() const { return _err ? _err : _status; }
    int status() const { return _status; }   // 0 = none came
    int error() const { return _err; }       // 0 = fine, an error with a status means the body was cut
    const __FlashStringHelper* responseHTTPStringF() const;
    static const __FlashStringHelper* reason(int code);   // flash text; unknown = class text, "" below 300
    static const __FlashStringHelper* urlWhy(const char* url);   // "" if open() would take it
    // The kept body and location are freed on the next call after takeDone()
    size_t responseRead(uint8_t* out, size_t len);   // no sink, up to BODY_CAP
    int32_t length() const { return _length; }   // -1 = not said
    uint32_t received() const { return _got; }   // body bytes so far
    const char* location() const { return _location; }   // from a redirect that wasn't followed

  private:
    enum Phase : uint8_t { IDLE, DIAL, SEND, REPLY, HEAD, BODY, DONE };   // REPLY = status line
    enum Chunk : uint8_t { AT_SIZE, AT_DATA, AT_CRLF };
    enum Header : uint8_t { H_OTHER, H_LENGTH, H_CODING, H_LOCATION };   // the ones acted on

    struct Buf {
      char* p = nullptr;
      size_t len = 0;
      size_t cap = 0;
      bool add(const char* s, size_t n, bool flash = false);
      bool add(const char* s) { return add(s, strlen(s)); }
      bool add_P(PGM_P s) { return add(s, strlen_P(s), true); }
      void clear();
    };

    int start(const char* url);
    bool seal(const uint8_t* body, size_t len, bool has_body);
    void step();
    void dial();
    void push();
    void send_more();
    void listen();
    void head();
    void status_line();
    void header_line();
    static uint8_t header_kind(const char* name);
    static bool owned(const char* name);
    void act_on(uint8_t kind, const char* value);
    void head_done();
    const char* url_start(const char* to, char* base, size_t n);
    bool follow();
    void body();
    void chunked();
    void feed(const uint8_t* d, size_t n);
    size_t take(size_t n);
    size_t give(const uint8_t* buf, size_t n);
    size_t fill();
    bool line();
    bool gone();
    void fresh_reply();
    void end(int err);
    void drop();
    void forget();

    EGTcpClient _tcp;
    Phase _phase = IDLE;
    bool _pending = false;     // done, not taken yet
    bool _taken = false;       // freed on next call
    bool _ready = false;
    int16_t _status = 0;
    int16_t _err = 0;

    // request
    char _method[8] = {};
    char _host[EGHTTP_HOST_MAX + 1] = {};
    uint16_t _port = 80;
    Buf _req;
    size_t _sent = 0;
    Buf _hdrs;                 // kept for redirects
    bool _bad = false;         // a header didn't fit
    bool _bodyless = true;

    // reply
    uint8_t* _in = nullptr;
    size_t _inLen = 0;
    size_t _inOff = 0;
    Buf _line;
    bool _lineDone = false;
    bool _cut = false;         // past LINE_CAP
    uint32_t _lineBytes = 0;   // caps the headers
    bool _mattered = false;    // last header acted on
    bool _chunked = false;
    Chunk _chunk = AT_SIZE;
    bool _toClose = false;     // body runs until close
    bool _held = false;        // sink full
    int32_t _length = -1;
    uint32_t _got = 0;
    uint32_t _chunkLeft = 0;
    char* _location = nullptr;
    uint8_t* _body = nullptr;
    size_t _bodyLen = 0;
    size_t _bodyRead = 0;

    Sink _sink = nullptr;
    void* _sinkArg = nullptr;
    bool _push = false;        // the stack hands the reply over
    Wake _wake = nullptr;
    void* _wakeArg = nullptr;
    uint16_t _timeoutS = 10;
    uint32_t _deadlineMs = 0;
    uint32_t _startMs = 0;
    uint32_t _heardMs = 0;     // last bytes either way
    uint8_t _redirects = 0;
    uint8_t _hops = 0;
};

#endif
