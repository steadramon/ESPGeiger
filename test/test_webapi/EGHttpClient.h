/*
  EGHttpClient.h - host stand-in: keeps what WebAPI sends and hands back
  the reply a test sets.

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
#include <string>

class EGHttpClient {
  public:
    std::string url, body, reply;
    int code = 200;
    uint32_t deadline = 0;
    bool done = false;     // test sets: reply arrived
    bool flying = false;   // sent, no reply yet
    bool refuse = false;   // test sets: open() fails

    bool idle() { return !done && !flying; }
    bool takeDone() {
      if (!done) return false;
      done = flying = false;
      return true;
    }

    void setTimeout(uint16_t) {}
    void setDeadline(uint32_t ms) { deadline = ms; }
    bool open(const char*, const char* u) { url = u; return !refuse; }
    bool open(const __FlashStringHelper* m, const __FlashStringHelper* u) {
      return open((const char*)m, (const char*)u);
    }
    template <class N, class V> void setReqHeader(N, V) {}
    bool send(const uint8_t* b, size_t n) {
      body.assign((const char*)b, n);
      flying = true;
      return true;
    }
    int responseHTTPcode() { return code; }
    const __FlashStringHelper* responseHTTPStringF() { return F("OK"); }
    size_t responseRead(uint8_t* b, size_t n) {
      if (n > reply.size()) n = reply.size();
      memcpy(b, reply.data(), n);
      return n;
    }
};

#endif
