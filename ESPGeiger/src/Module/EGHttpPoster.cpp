/*
  EGHttpPoster.cpp - periodic HTTP upload, shared by the output modules

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
#include "EGHttpPoster.h"
#include "EGModuleRegistry.h"
#include "../Logger/Logger.h"
#include "../Util/LedSignal.h"
#include "../Util/DeviceInfo.h"

extern uint8_t send_indicator;

void EGHttpPoster::set_enabled(bool on) {
  _send_enabled = on;
  EGModuleRegistry::set_loop_interval(this, on ? 500 : -1);
}

size_t EGHttpPoster::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
  return write_status_json(buf, cap, status_key(), last_ok, last_attempt_ms, now);
}

void EGHttpPoster::loop(unsigned long now)
{
  if (!_send_enabled) return;
  if (lastPing == 0) {
    lastPing = EGModuleRegistry::initial_ping(name(), now, pingIntervalMs);
  } else if ((now - lastPing) >= pingIntervalMs) {
    // Whole intervals, so the slot offset survives a catch-up.
    lastPing += ((now - lastPing) / pingIntervalMs) * pingIntervalMs;
    post();
  }
  EGModuleRegistry::sleep_until(this, now, lastPing + pingIntervalMs);
}

void EGHttpPoster::httpRequestCb(void* optParm, AsyncHTTPRequest* request, int readyState)
{
  if (readyState != readyStateDone) return;
  EGHttpPoster* self = static_cast<EGHttpPoster*>(optParm);
  bool ok = false;
  if (request->responseHTTPcode() == 200)
  {
    char r[REPLY_CAP];
    size_t cap = self->reply_cap();
    if (cap > sizeof(r)) cap = sizeof(r);
    size_t got = request->responseRead((uint8_t*)r, cap - 1);
    r[got] = 0;
    ok = self->interpret(r);
  } else {
    char why[32];
    strncpy_P(why, (PGM_P)request->responseHTTPStringF(), sizeof(why) - 1);
    why[sizeof(why) - 1] = '\0';
    Log::console(PSTR("%s: Error %d - %s"), self->log_tag(), request->responseHTTPcode(), why);
  }
  self->note_result(ok);
}

void EGHttpPoster::post() {
  char url[URL_CAP];
  const char* body = nullptr;
  if (!prepare(url, sizeof(url), &body)) return;

  if (!request) request = new AsyncHTTPRequest();
  if (!request) { Log::console(PSTR("%s: alloc failed"), log_tag()); return; }

  if (request->readyState() != readyStateUnsent && request->readyState() != readyStateDone) return;

  if (!request->open(method(), url)) {
    Log::console(PSTR("%s: Can't send request"), log_tag());
    return;
  }
  LedSignal::activity();
  request->setReqHeader(F("User-Agent"), DeviceInfo::useragent());
  add_headers(request);
  request->onReadyStateChange(httpRequestCb, this);
  request->setTimeout(timeout_s());
  if (body) request->send(body);
  else      request->send();
  note_attempt();
  send_indicator = 2;
}
