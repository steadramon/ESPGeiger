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
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Globals.h"
#include "Corium/Prefs/EGPrefs.h"
#include <EGHttpStrings.h>

static_assert(EGHTTP_HOST_MAX >= EG_HOST_MAX, "a host pref must fit the client");

// off mid-post: loop() still reaps the reply, then sleeps
void EGHttpPoster::set_enabled(bool on) {
  _send_enabled = on;
  EGModuleRegistry::set_loop_interval(this, (on || request) ? 500 : -1);
}

size_t EGHttpPoster::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
  return write_status_json(buf, cap, status_key(), last_ok, last_attempt_ms, now);
}

void EGHttpPoster::loop(unsigned long now)
{
  if (request && request->takeDone()) {
    take_reply();
    // a fresh one each post, nothing held between
    delete request;
    request = nullptr;
  }
  if (!_send_enabled) {
    if (!request) EGModuleRegistry::set_loop_interval(this, -1);
    return;
  }
  if (lastPing == 0) {
    lastPing = EGModuleRegistry::initial_ping(name(), now, pingIntervalMs);
  } else if ((now - lastPing) >= pingIntervalMs) {
    // whole intervals, keeps the slot
    lastPing += ((now - lastPing) / pingIntervalMs) * pingIntervalMs;
    post();
  }
  if (request && !request->idle()) {
    EGModuleRegistry::set_loop_interval(this, loop_interval_ms());   // reply due, poll
    return;
  }
  EGModuleRegistry::sleep_until(this, now, lastPing + pingIntervalMs);
}

void EGHttpPoster::take_reply()
{
  bool ok = false;
  if (request->responseHTTPcode() == 200)
  {
    char r[REPLY_CAP];
    size_t cap = reply_cap();
    if (cap > sizeof(r)) cap = sizeof(r);
    size_t got = request->responseRead((uint8_t*)r, cap - 1);
    r[got] = 0;
    ok = interpret(r);
  } else {
    char why[32];
    strncpy_P(why, (PGM_P)request->responseHTTPStringF(), sizeof(why) - 1);
    why[sizeof(why) - 1] = '\0';
    Log::console(PSTR("%s: Error %d - %s"), log_tag(), request->responseHTTPcode(), why);
  }
  note_result(ok);
}

bool EGHttpPoster::url_fits(int n, size_t cap) {
  if (n >= 0 && (size_t)n < cap) return true;
  Log::console(PSTR("%s: URL too long, not sent"), log_tag());
  return false;
}

void EGHttpPoster::post() {
  if (request) return;   // last reply not in yet
  char url[URL_CAP];
  const char* body = nullptr;
  if (!prepare(url, sizeof(url), &body)) return;
  send(url, body);
  free((void*)body);   // the client copied it
}

void EGHttpPoster::send(const char* url, const char* body) {
  request = new EGHttpClient();
  if (!request) { Log::console(PSTR("%s: alloc failed"), log_tag()); return; }

  if (!request->open(method(), url)) {
    Log::console(PSTR("%s: Can't send request"), log_tag());
    note_attempt();
    note_result(false);
    delete request;
    request = nullptr;
    return;
  }
  LedSignal::activity();
  request->setReqHeader(FPSTR(HTTP_H_UA), DeviceInfo::useragent());
  add_headers(request);
  request->setTimeout(timeout_s());
  request->setDeadline(timeout_s() * 2000UL);   // a trickling reply can't hold it
  if (body) request->send(body);
  else      request->send();
  note_attempt();
  send_indicator = 2;
}
