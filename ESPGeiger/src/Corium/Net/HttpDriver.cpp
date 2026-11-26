/*
  HttpDriver.cpp - a module's HTTP requests, one at a time, a client each

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
#include "HttpDriver.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/DeviceInfo.h"
#include <EGHttpStrings.h>
#include <new>

void corium::HttpDriver::wake(void* owner) {
  corium::Registry::post((corium::Module*)owner);
}

corium::HttpDriver::Sent corium::HttpDriver::send(const corium::HttpMeta& m, const uint8_t* body, size_t len) {
  if (_c) return NOT_OPENED;   // last reply hasn't been taken yet
  _c = new (std::nothrow) EGHttpClient();
  if (!_c) return NO_MEMORY;
  bool open = m.flash ? _c->open(FPSTR(m.method), FPSTR(m.url)) : _c->open(m.method, m.url);
  if (!open) {
    bool bad = _c->responseHTTPcode() == HTTPCODE_BAD_URL;
    delete _c;
    _c = nullptr;
    return bad ? BAD_URL : NOT_OPENED;
  }
  char ua[corium::DeviceInfo::UA_MAX];
  corium::DeviceInfo::userAgent(ua, sizeof(ua));
  // No room makes the send below fail, a header with bad text is just left off
  (void)_c->setReqHeader(FPSTR(HTTP_H_UA), ua);
  if (m.accept) (void)_c->setReqHeader(F("Accept"), FPSTR(m.accept));
  if (m.ctype) (void)_c->setReqHeader(FPSTR(HTTP_H_CTYPE), FPSTR(m.ctype));
  if (m.extra) (void)_c->setReqHeader(FPSTR(m.extra), m.extraValue);
  _c->setTimeoutS(m.timeoutS);
  _c->setDeadline(m.timeoutS * 2000UL);   // hard limit, so a trickling reply can't hold it open
  _c->setWake(wake, _owner);
  bool sealed = body ? _c->send(body, len) : _c->send();
  return sealed ? SENT : NOT_SEALED;
}

bool corium::HttpDriver::take(int* code, uint8_t* body, size_t cap, size_t* got) {
  if (!_c || !_c->takeDone()) return false;
  *code = _c->responseHTTPcode();
  *got = _c->responseRead(body, cap);
  delete _c;
  _c = nullptr;
  return true;
}

void corium::HttpDriver::logError(const char* tag, int code) {
  corium::Log::console(PSTR("%s: Error %d - %s"), tag, code, (PGM_P)EGHttpClient::reason(code));
}
