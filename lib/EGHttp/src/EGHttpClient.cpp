/*
  EGHttpClient.cpp - small HTTP/1.1 client, run from the main loop.

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
#include "EGHttpClient.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <lwip/err.h>
#include "RespParse.h"
#include "UrlParse.h"

EGHttpClient::~EGHttpClient() {
  drop();
  free(_body);
  free(_location);
}

bool EGHttpClient::Buf::add(const char* s, size_t n) {
  if (len + n + 1 > cap) {
    size_t want = cap ? cap * 2 : 128;
    if (want < len + n + 1) want = len + n + 1;
    char* bigger = (char*)realloc(p, want);
    if (!bigger) return false;
    p = bigger;
    cap = want;
  }
  memcpy(p + len, s, n);
  len += n;
  p[len] = '\0';
  return true;
}

void EGHttpClient::Buf::clear() {
  free(p);
  p = nullptr;
  len = cap = 0;
}

// Request

bool EGHttpClient::open(const char* method, const char* url) {
  if (!idle()) return false;
  drop();
  forget();
  _phase = IDLE;
  _status = _err = 0;
  _pending = false;
  _bad = false;
  _hops = 0;
  _sink = nullptr;
  _startMs = millis();

  size_t n = strlen(method);
  int err = HTTPCODE_NOT_CONNECTED;
  if (n < sizeof(_method) && eg_http_token(method, n)) {
    memcpy(_method, method, n + 1);
    err = start(url);
  }
  if (err) {
    end(err);
    _pending = false;
    return false;
  }
  return true;
}

bool EGHttpClient::open(const __FlashStringHelper* method, const __FlashStringHelper* url) {
  PGM_P mp = (PGM_P)method;
  PGM_P up = (PGM_P)url;
  char m[sizeof(_method)];
  char u[URL_P_MAX + 1];
  size_t mn = strlen_P(mp);
  size_t un = strlen_P(up);
  if (mn >= sizeof(m) || un > URL_P_MAX) return false;
  memcpy_P(m, mp, mn + 1);
  memcpy_P(u, up, un + 1);
  return open(m, u);
}

// 0 or an HTTPCODE
int EGHttpClient::start(const char* url) {
  EGUrlParts u;
  if (!eg_parse_url(url, &u) || u.host_len >= sizeof(_host)) return HTTPCODE_NOT_CONNECTED;
  memcpy(_host, u.host, u.host_len);
  _host[u.host_len] = '\0';
  _port = (uint16_t)u.port;

  _req.clear();
  _sent = 0;
  _ready = false;
  char port[8] = "";
  if (_port != 80) snprintf(port, sizeof(port), ":%u", _port);
  bool ok = _req.add(_method) && _req.add(" ") &&
            _req.add(u.path, u.path_len) && _req.add(u.query, u.query_len) &&
            _req.add(" HTTP/1.1\r\nHost: ") && _req.add(_host) && _req.add(port) &&
            _req.add("\r\nConnection: close\r\n");
  if (!ok) return HTTPCODE_TOO_LESS_RAM;

  free(_in);
  _in = nullptr;
  _inLen = _inOff = 0;
  _line.clear();
  _lineDone = _cut = false;
  _lineBytes = 0;
  _got = 0;
  _held = false;
  fresh_reply();

  _heardMs = millis();
  _phase = DIAL;
  if (!_tcp.connect(_host, _port)) return HTTPCODE_NOT_CONNECTED;
  return 0;
}

// bad ones dropped
void EGHttpClient::setReqHeader(const char* name, const char* value) {
  if (_phase != DIAL || _ready) return;
  if (!eg_http_token(name, strlen(name)) || !eg_http_text(value, strlen(value))) return;
  bool ok = _hdrs.add(name) && _hdrs.add(": ") && _hdrs.add(value) && _hdrs.add("\r\n");
  if (!ok) _bad = true;
}

void EGHttpClient::setReqHeader(const __FlashStringHelper* name, const char* value) {
  char n[48];
  if (strlen_P((PGM_P)name) >= sizeof(n)) {
    _bad = true;
    return;
  }
  strncpy_P(n, (PGM_P)name, sizeof(n));
  setReqHeader(n, value);
}

void EGHttpClient::setReqHeader(const __FlashStringHelper* name, const __FlashStringHelper* value) {
  char v[64];
  if (strlen_P((PGM_P)value) >= sizeof(v)) {
    _bad = true;
    return;
  }
  strncpy_P(v, (PGM_P)value, sizeof(v));
  setReqHeader(name, v);
}

bool EGHttpClient::send(const char* body) {
  if (!body) return send(nullptr, 0);
  return send((const uint8_t*)body, strlen(body));
}

bool EGHttpClient::send(const uint8_t* body, size_t len) {
  if (!seal(body, len, body != nullptr)) return false;
  step();
  return true;
}

// headers, length, blank line, body
bool EGHttpClient::seal(const uint8_t* body, size_t len, bool has_body) {
  if (_phase != DIAL || _ready) return false;
  bool ok = !_bad && _req.add(_hdrs.p ? _hdrs.p : "", _hdrs.len);
  if (has_body) {
    char cl[32];
    snprintf(cl, sizeof(cl), "Content-Length: %u\r\n", (unsigned)len);
    ok = ok && _req.add(cl);
  }
  ok = ok && _req.add("\r\n");
  if (has_body && len) ok = ok && _req.add((const char*)body, len);
  if (!ok) {
    end(HTTPCODE_TOO_LESS_RAM);
    return false;
  }
  _bodyless = !has_body;
  _ready = true;
  return true;
}

void EGHttpClient::abort() {
  if (_phase == IDLE || _phase == DONE) return;
  end(HTTPCODE_CONNECTION_LOST);
  _pending = false;
  forget();
}

// Loop side

bool EGHttpClient::idle() {
  if (_taken) forget();
  step();
  return _phase == IDLE || (_phase == DONE && !_pending);
}

bool EGHttpClient::takeDone() {
  if (_taken) forget();
  step();
  if (_phase != DONE || !_pending) return false;
  _pending = false;
  _taken = true;
  return true;
}

void EGHttpClient::forget() {
  _taken = false;
  free(_body);
  _body = nullptr;
  _bodyLen = _bodyRead = 0;
  free(_location);
  _location = nullptr;
}

void EGHttpClient::step() {
  Phase was;
  do {
    was = _phase;
    if (_phase == IDLE || _phase == DONE) return;
    uint32_t now = millis();
    if (_held) _heardMs = now;   // sink waiting, not the server
    bool late = _deadlineMs && now - _startMs >= _deadlineMs;
    if (late || now - _heardMs >= _timeoutS * 1000UL) {
      end(HTTPCODE_TIMEOUT);
      return;
    }
    switch (_phase) {
      case DIAL: dial(); break;
      case SEND: push(); break;
      case REPLY:
      case HEAD: head(); break;
      case BODY: body(); break;
      default: break;
    }
  } while (_phase != was);
}

void EGHttpClient::dial() {
  EGTcpConn::State s = _tcp.state();
  if (s == EGTcpConn::CLOSED) {
    // lwIP gave up
    end(_tcp.error() == ERR_ABRT ? HTTPCODE_TIMEOUT : HTTPCODE_NOT_CONNECTED);
  } else if (s == EGTcpConn::OPEN && _ready) {
    _heardMs = millis();
    _phase = SEND;
  }
}

void EGHttpClient::push() {
  // early reply, eg 413 or 401
  if (_tcp.available()) {
    listen();
    return;
  }
  if (_tcp.state() != EGTcpConn::OPEN) {
    end(HTTPCODE_CONNECTION_LOST);
    return;
  }
  size_t n = _tcp.write(_req.p + _sent, _req.len - _sent);
  if (n) _heardMs = millis();
  _sent += n;
  if (_sent == _req.len) listen();
}

void EGHttpClient::listen() {
  _req.clear();
  _in = (uint8_t*)malloc(IN_CAP);
  if (!_in) {
    end(HTTPCODE_TOO_LESS_RAM);
    return;
  }
  _phase = REPLY;
}

// Reply

// 0 = none for now
size_t EGHttpClient::fill() {
  if (_inOff == _inLen) {
    _inLen = _tcp.read(_in, IN_CAP);
    _inOff = 0;
    if (_inLen) _heardMs = millis();
  }
  return _inLen - _inOff;
}

bool EGHttpClient::gone() {
  return _inOff == _inLen && !_tcp.available() && _tcp.state() == EGTcpConn::CLOSED;
}

// false = not all here yet
bool EGHttpClient::line() {
  if (_lineDone) {
    _line.len = 0;
    _lineDone = _cut = false;
  }
  while (fill()) {
    const uint8_t* at = _in + _inOff;
    size_t n = _inLen - _inOff;
    const uint8_t* nl = (const uint8_t*)memchr(at, '\n', n);
    size_t used = nl ? (size_t)(nl - at) + 1 : n;
    size_t keep = nl ? used - 1 : used;
    _inOff += used;
    _lineBytes += used;
    if (keep > LINE_CAP - 1 - _line.len) {
      keep = LINE_CAP - 1 - _line.len;
      _cut = true;
    }
    if (!_line.add((const char*)at, keep)) {
      end(HTTPCODE_TOO_LESS_RAM);
      return false;
    }
    if (nl) {
      if (_line.len && _line.p[_line.len - 1] == '\r') _line.p[--_line.len] = '\0';
      _lineDone = true;
      return true;
    }
  }
  return false;
}

void EGHttpClient::head() {
  while ((_phase == REPLY || _phase == HEAD) && line()) {
    if (_lineBytes > HEAD_CAP) end(HTTPCODE_ENCODING);
    else if (_phase == REPLY) status_line();
    else header_line();
  }
  if (_phase != REPLY && _phase != HEAD) return;
  if (_lineBytes > HEAD_CAP) end(HTTPCODE_ENCODING);   // one endless line
  else if (gone()) end(HTTPCODE_CONNECTION_LOST);
}

void EGHttpClient::status_line() {
  int code = _cut ? -1 : eg_http_status(_line.p, _line.len);
  if (code < 0) {
    end(HTTPCODE_NO_HTTP_SERVER);
    return;
  }
  _status = (int16_t)code;
  _phase = HEAD;
}

static bool same(const char* a, const char* b) {
  return strcasecmp(a, b) == 0;
}

static bool parse_length(const char* s, int32_t* out) {
  if (!*s) return false;
  uint32_t v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    if (v > 0x7FFFFFFF / 10) return false;
    v = v * 10 + (uint32_t)(*s - '0');
  }
  if (v > 0x7FFFFFFF) return false;
  *out = (int32_t)v;
  return true;
}

void EGHttpClient::header_line() {
  if (_line.len == 0) {
    head_done();
    return;
  }
  // obs-fold
  if (_line.p[0] == ' ' || _line.p[0] == '\t') {
    if (_mattered) end(HTTPCODE_ENCODING);
    return;
  }
  char* name;
  char* value;
  _mattered = false;
  if (!eg_http_header(_line.p, _line.len, &name, &value)) return;   // junk, skipped
  _mattered = same(name, "Content-Length") || same(name, "Transfer-Encoding") ||
              same(name, "Location");
  if (!_mattered) return;
  if (_cut) end(HTTPCODE_ENCODING);   // can't act on half
  else act_on(name, value);
}

void EGHttpClient::act_on(const char* name, const char* value) {
  if (same(name, "Content-Length")) {
    int32_t n;
    // a second has to agree
    if (!parse_length(value, &n) || (_length >= 0 && n != _length)) end(HTTPCODE_ENCODING);
    else _length = n;
  } else if (same(name, "Transfer-Encoding")) {
    // chunked, once
    if (_chunked || !same(value, "chunked")) end(HTTPCODE_ENCODING);
    else _chunked = true;
  } else {
    free(_location);
    _location = strdup(value);
    if (!_location) end(HTTPCODE_TOO_LESS_RAM);
  }
}

static bool moved(int code) {
  return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

void EGHttpClient::head_done() {
  if (_status < 200) {   // 1xx, real one follows
    fresh_reply();
    _phase = REPLY;
    return;
  }
  if (_chunked) _length = -1;   // chunked wins over a length
  if (moved(_status) && _location && follow()) return;
  bool none = same(_method, "HEAD") || _status == 204 || _status == 304;
  if (none || (!_chunked && _length == 0)) {
    end(0);
    return;
  }
  _toClose = !_chunked && _length < 0;
  _chunk = AT_SIZE;
  _phase = BODY;
}

// nullptr = not followed
const char* EGHttpClient::url_start(const char* to, char* base, size_t n) {
  if (to[0] == '/' && to[1] == '/') return "http:";
  if (to[0] == '/') {
    snprintf(base, n, "http://%s:%u", _host, _port);
    return base;
  }
  if (strncasecmp(to, "http://", 7) == 0) return "";
  return nullptr;
}

// false = caller gets the 3xx
bool EGHttpClient::follow() {
  // 303 means GET anyway
  bool again = _bodyless || _status == 303;
  if (!again || _hops >= _redirects) return false;

  char base[sizeof("http://:65535") + EGHTTP_HOST_MAX];
  const char* before = url_start(_location, base, sizeof(base));
  if (!before) return false;
  Buf url;
  bool ok = url.add(before) && url.add(_location);

  _hops++;
  char was[sizeof(_host)];
  strlcpy(was, _host, sizeof(was));
  uint16_t wasPort = _port;
  if (_status == 303 && !same(_method, "HEAD")) strlcpy(_method, "GET", sizeof(_method));
  _tcp.close();
  int err = ok ? start(url.p) : HTTPCODE_TOO_LESS_RAM;
  url.clear();
  // headers for that host only
  if (!err && (!same(was, _host) || wasPort != _port)) _hdrs.clear();
  if (err || !seal(nullptr, 0, false)) end(err ? err : HTTPCODE_TOO_LESS_RAM);
  return true;
}

void EGHttpClient::body() {
  if (_chunked) {
    chunked();
    return;
  }
  while (_phase == BODY && fill()) {
    size_t n = _inLen - _inOff;
    if (_length >= 0 && n > (uint32_t)_length - _got) n = (uint32_t)_length - _got;
    if (take(n) < n) return;   // sink full
    if (_length >= 0 && _got == (uint32_t)_length) {
      end(0);
      return;
    }
  }
  if (_phase != BODY || !gone()) return;
  // no length, clean close ends it
  end(_toClose && !_tcp.error() ? 0 : HTTPCODE_CONNECTION_LOST);
}

void EGHttpClient::chunked() {
  while (_phase == BODY) {
    if (_chunk == AT_DATA) {
      if (!fill()) break;
      size_t n = _inLen - _inOff;
      if (n > _chunkLeft) n = _chunkLeft;
      size_t took = take(n);
      _chunkLeft -= took;
      if (took < n) return;
      if (!_chunkLeft) _chunk = AT_CRLF;
      continue;
    }
    if (!line()) break;
    uint32_t size;
    if (_chunk == AT_CRLF) {
      // data ran past its size
      if (_line.len) end(HTTPCODE_ENCODING);
      else _chunk = AT_SIZE;
    } else if (_cut || !eg_http_chunk_size(_line.p, _line.len, &size)) {
      end(HTTPCODE_ENCODING);
    } else if (size == 0) {
      end(0);   // trailers ignored
    } else {
      _chunkLeft = size;
      _chunk = AT_DATA;
    }
  }
  if (_phase == BODY && gone()) end(HTTPCODE_CONNECTION_LOST);
}

// sink can end it, check _phase
size_t EGHttpClient::take(size_t n) {
  size_t took = give(_in + _inOff, n);
  _inOff += took;
  _got += took;
  _held = took < n;
  return took;
}

// sink, or kept up to BODY_CAP
size_t EGHttpClient::give(const uint8_t* buf, size_t n) {
  if (_sink) {
    int took = _sink(_sinkArg, buf, n);
    if (took < 0) {
      end(HTTPCODE_STREAM_WRITE);
      return 0;
    }
    return (size_t)took < n ? (size_t)took : n;
  }
  if (!_body) _body = (uint8_t*)malloc(BODY_CAP);
  if (!_body) {
    end(HTTPCODE_TOO_LESS_RAM);
    return 0;
  }
  size_t keep = BODY_CAP - _bodyLen;
  if (keep > n) keep = n;
  memcpy(_body + _bodyLen, buf, keep);
  _bodyLen += keep;
  return n;
}

size_t EGHttpClient::responseRead(uint8_t* out, size_t len) {
  size_t left = _bodyLen - _bodyRead;
  if (len > left) len = left;
  if (!len) return 0;
  memcpy(out, _body + _bodyRead, len);
  _bodyRead += len;
  return len;
}

// Endings

void EGHttpClient::fresh_reply() {
  _status = 0;
  _length = -1;
  _chunked = _toClose = _mattered = false;
  free(_location);
  _location = nullptr;
}

// first ending wins, 0 = fine
void EGHttpClient::end(int err) {
  if (_phase == DONE) return;
  _err = (int16_t)err;
  _phase = DONE;
  _pending = true;
  _tcp.close();
  drop();
}

void EGHttpClient::drop() {
  _req.clear();
  _hdrs.clear();
  _line.clear();
  free(_in);
  _in = nullptr;
  _inLen = _inOff = 0;
}

struct Reason {
  int16_t code;
  const char* text;
};

static const char R_NC[] PROGMEM = "NOT_CONNECTED";
static const char R_LOST[] PROGMEM = "CONNECTION_LOST";
static const char R_NOHTTP[] PROGMEM = "NO_HTTP_SERVER";
static const char R_RAM[] PROGMEM = "TOO_LESS_RAM";
static const char R_ENC[] PROGMEM = "ENCODING";
static const char R_WRITE[] PROGMEM = "STREAM_WRITE";
static const char R_TIME[] PROGMEM = "TIMEOUT";
static const char R_200[] PROGMEM = "HTTP OK";
static const char R_400[] PROGMEM = "Bad Request";
static const char R_401[] PROGMEM = "Unauthorized";
static const char R_403[] PROGMEM = "Forbidden";
static const char R_404[] PROGMEM = "Not Found";
static const char R_413[] PROGMEM = "Payload Too Large";
static const char R_429[] PROGMEM = "Too Many Requests";
static const char R_500[] PROGMEM = "Internal Server Error";
static const char R_502[] PROGMEM = "Bad Gateway";
static const char R_503[] PROGMEM = "Service Unavailable";
static const char R_504[] PROGMEM = "Gateway Timeout";
static const char R_3XX[] PROGMEM = "Redirect";
static const char R_4XX[] PROGMEM = "Client Error";
static const char R_5XX[] PROGMEM = "Server Error";

static const Reason REASONS[] PROGMEM = {
  { HTTPCODE_NOT_CONNECTED, R_NC }, { HTTPCODE_CONNECTION_LOST, R_LOST },
  { HTTPCODE_NO_HTTP_SERVER, R_NOHTTP }, { HTTPCODE_TOO_LESS_RAM, R_RAM },
  { HTTPCODE_ENCODING, R_ENC }, { HTTPCODE_STREAM_WRITE, R_WRITE },
  { HTTPCODE_TIMEOUT, R_TIME },
  { 200, R_200 }, { 400, R_400 }, { 401, R_401 }, { 403, R_403 }, { 404, R_404 },
  { 413, R_413 }, { 429, R_429 }, { 500, R_500 }, { 502, R_502 }, { 503, R_503 },
  { 504, R_504 },
};

const __FlashStringHelper* EGHttpClient::responseHTTPStringF() const {
  int code = responseHTTPcode();
  for (const Reason& row : REASONS) {
    Reason r;
    memcpy_P(&r, &row, sizeof(r));
    if (r.code == code) return FPSTR(r.text);
  }
  if (code >= 500) return FPSTR(R_5XX);
  if (code >= 400) return FPSTR(R_4XX);
  if (code >= 300) return FPSTR(R_3XX);
  return F("");
}
