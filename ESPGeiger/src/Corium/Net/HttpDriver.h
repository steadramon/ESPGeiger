/*
  HttpDriver.h -a module's HTTP requests, one at a time, a client each

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

// what goes with the payload
struct HttpMeta {
  const char* method = nullptr;
  const char* url = nullptr;
  bool flash = false;              // method and url PROGMEM
  PGM_P ctype = nullptr;           // Content-Type, nullptr = none
  PGM_P accept = nullptr;          // Accept, nullptr = none
  PGM_P extra = nullptr;           // one more header
  const char* extraValue = nullptr;
  uint16_t timeoutS = 30;         // quiet, twice that whole
};

// the reply posts the owner, its client goes with it
class HttpDriver {
  public:
    enum Sent : uint8_t { SENT, NOT_SEALED, NOT_OPENED, NO_MEMORY, BAD_URL };   // compare, SENT is 0

    explicit HttpDriver(Module* owner) : _owner(owner) {}
#ifdef NATIVE_TEST
    ~HttpDriver() { delete _c; }   // device drivers are statics, never die
#endif
    HttpDriver(const HttpDriver&) = delete;
    HttpDriver& operator=(const HttpDriver&) = delete;

    // NOT_SEALED still replies, with an error
    Sent send(const HttpMeta& m, const uint8_t* body, size_t len);
    bool busy() const { return _c != nullptr; }
    // once a request: code below 0 = client error, body cut to cap
    bool take(int* code, uint8_t* body, size_t cap, size_t* got);
    uint32_t due() const { return _c ? _c->due() : 0; }   // its timeouts

  private:
    static void wake(void* owner);
    Module* _owner;
    EGHttpClient* _c = nullptr;
};

}  // namespace corium

#endif
