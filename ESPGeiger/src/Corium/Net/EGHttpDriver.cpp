/*
  EGHttpDriver.cpp - a module's HTTP requests, one at a time, a client each

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
#include "EGHttpDriver.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/DeviceInfo.h"
#include <EGHttpStrings.h>
#include <new>

void EGHttpDriver::wake(void* owner) {
  EGModuleRegistry::post((EGModule*)owner);
}

EGHttpDriver::Sent EGHttpDriver::send(const EGHttpMeta& m, const uint8_t* body, size_t len) {
  if (_c) return NOT_OPENED;   // last reply not taken
  _c = new (std::nothrow) EGHttpClient();
  if (!_c) return NO_MEMORY;
  bool open = m.flash ? _c->open(FPSTR(m.method), FPSTR(m.url)) : _c->open(m.method, m.url);
  if (!open) {
    delete _c;
    _c = nullptr;
    return NOT_OPENED;
  }
  char ua[DeviceInfo::UA_MAX];
  DeviceInfo::useragent(ua, sizeof(ua));
  _c->setReqHeader(FPSTR(HTTP_H_UA), ua);
  if (m.accept) _c->setReqHeader(F("Accept"), FPSTR(m.accept));
  if (m.ctype) _c->setReqHeader(FPSTR(HTTP_H_CTYPE), FPSTR(m.ctype));
  if (m.extra) _c->setReqHeader(FPSTR(m.extra), m.extra_value);
  _c->setTimeout(m.timeout_s);
  _c->setDeadline(m.timeout_s * 2000UL);   // a trickling reply can't hold it
  _c->setWake(wake, _owner);
  bool sealed = body ? _c->send(body, len) : _c->send();
  return sealed ? SENT : NOT_SEALED;
}

bool EGHttpDriver::take(int* code, uint8_t* body, size_t cap, size_t* got) {
  if (!_c || !_c->takeDone()) return false;
  *code = _c->responseHTTPcode();
  *got = _c->responseRead(body, cap);
  delete _c;
  _c = nullptr;
  return true;
}
