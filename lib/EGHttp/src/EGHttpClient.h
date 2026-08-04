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
#include <EGTcpConn.h>

// below 0 = client side
#define HTTPCODE_NOT_CONNECTED   (-4)
#define HTTPCODE_CONNECTION_LOST (-5)
#define HTTPCODE_NO_HTTP_SERVER  (-7)
#define HTTPCODE_TOO_LESS_RAM    (-8)
#define HTTPCODE_ENCODING        (-9)
#define HTTPCODE_STREAM_WRITE    (-10)
#define HTTPCODE_TIMEOUT         (-11)

#ifndef EGHTTP_HOST_MAX
  #define EGHTTP_HOST_MAX 64   // longer is refused
#endif

// http only, one at a time, headers after open()
class EGHttpClient {
  public:
    // bytes taken, below 0 to stop
    typedef int (*Sink)(void* arg, const uint8_t* buf, size_t len);

    static const size_t BODY_CAP = 256;
    static const size_t LINE_CAP = 2048;   // per line
    static const size_t HEAD_CAP = 8192;   // all headers
    static const size_t IN_CAP = 256;

    EGHttpClient() {}
    ~EGHttpClient();
    EGHttpClient(const EGHttpClient&) = delete;
    EGHttpClient& operator=(const EGHttpClient&) = delete;

    bool open(const char* method, const char* url);   // dials
    static const size_t URL_P_MAX = 127;
    // url up to URL_P_MAX
    bool open(const __FlashStringHelper* method, const __FlashStringHelper* url);
    void setReqHeader(const char* name, const char* value);
    void setReqHeader(const __FlashStringHelper* name, const char* value);
    void setReqHeader(const __FlashStringHelper* name, const __FlashStringHelper* value);
    void setTimeout(uint16_t secs) { _timeoutS = secs; }   // silence, default 10
    void setDeadline(uint32_t ms) { _deadlineMs = ms; }    // whole request, 0 = none
    void setRedirects(uint8_t n) { _redirects = n; }       // http only
    void setSink(Sink fn, void* arg) { _sink = fn; _sinkArg = arg; }
    bool send(const char* body = nullptr);
    bool send(const uint8_t* body, size_t len);
    void abort();

    bool idle();
    bool takeDone();                       // true once per request

    int responseHTTPcode() const { return _err ? _err : _status; }
    int status() const { return _status; }   // 0 = none came
    int error() const { return _err; }       // 0 = fine, with a status = body cut
    const __FlashStringHelper* responseHTTPStringF() const;
    size_t responseRead(uint8_t* out, size_t len);
    int32_t length() const { return _length; }   // -1 = not said
    uint32_t received() const { return _got; }   // body bytes so far
    const char* location() const { return _location; }   // a redirect not followed

  private:
    enum Phase : uint8_t { IDLE, DIAL, SEND, REPLY, HEAD, BODY, DONE };   // REPLY = status line
    enum Chunk : uint8_t { AT_SIZE, AT_DATA, AT_CRLF };

    struct Buf {
      char* p = nullptr;
      size_t len = 0;
      size_t cap = 0;
      bool add(const char* s, size_t n);
      bool add(const char* s) { return add(s, strlen(s)); }
      void clear();
    };

    int start(const char* url);
    bool seal(const uint8_t* body, size_t len, bool has_body);
    void step();
    void dial();
    void push();
    void listen();
    void head();
    void status_line();
    void header_line();
    void act_on(const char* name, const char* value);
    void head_done();
    const char* url_start(const char* to, char* base, size_t n);
    bool follow();
    void body();
    void chunked();
    size_t take(size_t n);
    size_t give(const uint8_t* buf, size_t n);
    size_t fill();
    bool line();
    bool gone();
    void fresh_reply();
    void end(int err);
    void drop();
    void forget();

    EGTcpConn _tcp;
    Phase _phase = IDLE;
    bool _pending = false;     // done, not taken yet
    bool _taken = false;       // freed on next call
    bool _ready = false;
    int16_t _status = 0;
    int16_t _err = 0;

    // request
    char _method[8] = "";
    char _host[EGHTTP_HOST_MAX + 1] = "";
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
    bool _toClose = false;     // body runs till close
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
    uint16_t _timeoutS = 10;
    uint32_t _deadlineMs = 0;
    uint32_t _startMs = 0;
    uint32_t _heardMs = 0;     // last bytes either way
    uint8_t _redirects = 0;
    uint8_t _hops = 0;
};

#endif
