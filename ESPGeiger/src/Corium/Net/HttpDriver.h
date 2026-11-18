/*
  HttpDriver.h - a module's HTTP requests, one at a time, a client each

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
#ifndef CORIUM_HTTPDRIVER_H
#define CORIUM_HTTPDRIVER_H

#include <Arduino.h>
#include <EGHttpClient.h>

namespace corium {

class Module;

struct HttpMeta {
  const char* method = nullptr;
  const char* url = nullptr;
  bool flash = false;              // method and url are in PROGMEM
  PGM_P ctype = nullptr;           // nullptr = none
  PGM_P accept = nullptr;
  PGM_P extra = nullptr;           // One more header
  const char* extraValue = nullptr;
  uint16_t timeoutS = 30;         // Max quiet time, the whole request gets twice that
};

// The reply posts the owner module, the client is freed once the reply is taken
class HttpDriver {
  public:
    enum Sent : uint8_t { SENT, NOT_SEALED, NOT_OPENED, NO_MEMORY, BAD_URL };   // compare against these, SENT is 0

    explicit HttpDriver(Module* owner) : _owner(owner) {}
#ifdef NATIVE_TEST
    ~HttpDriver() { delete _c; }   // On the device drivers are statics and never die
#endif
    HttpDriver(const HttpDriver&) = delete;
    HttpDriver& operator=(const HttpDriver&) = delete;

    // NOT_SEALED still gets a reply, with an error
    Sent send(const HttpMeta& m, const uint8_t* body, size_t len);
    bool busy() const { return _c != nullptr; }
    // Once per request, code < 0 = client error
    bool take(int* code, uint8_t* body, size_t cap, size_t* got);
    uint32_t due() const { return _c ? _c->due() : 0; }   // when the client's next timeout is due
    static void logError(const char* tag, int code);   // "tag: Error code - reason"

  private:
    static void wake(void* owner);
    Module* _owner;
    EGHttpClient* _c = nullptr;
};

}  // namespace corium

#endif
