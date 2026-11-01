/*
  corium::HttpPoster.cpp - periodic HTTP upload, shared by the output modules

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
#include "HttpPoster.h"
#include "Corium/Module/Registry.h"
#include "SendSlot.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Util/MathUtil.h"

static_assert(EGHTTP_HOST_MAX >= CORIUM_HOST_MAX, "a host pref must fit the client");

bool (*corium::HttpPoster::hold)() = nullptr;

// off mid-post: loop() still reaps the reply, then sleeps
void corium::HttpPoster::set_enabled(bool on) {
  _send_enabled = on;
  corium::Registry::every(this, (on || request.busy()) ? LOOP_MS : 0);
}

uint16_t corium::HttpPoster::set_interval_s(int s, int lo, int hi) {
  s = clamp(s, lo, hi);
  pingIntervalMs = (uint32_t)s * 1000UL;
  return (uint16_t)s;
}

size_t corium::HttpPoster::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
  return write_status_json(buf, cap, status_key(), last_ok, last_attempt_ms, now);
}

void corium::HttpPoster::loop(unsigned long now)
{
  take_reply();
  if (hold && hold()) {
    corium::Registry::due_at(this, now + LOOP_MS);
    return;
  }
  if (_send_enabled && slot_due(now)) post();
  if (request.busy()) {
    corium::Registry::due_at(this, request.due());   // the reply posts us sooner
  } else if (_send_enabled) {
    corium::Registry::due_at(this, lastPing + pingIntervalMs);
  } else {
    corium::Registry::every(this, 0);
  }
}

// whole intervals, keeps the slot
bool corium::HttpPoster::slot_due(unsigned long now) {
  if (lastPing == 0) {
    lastPing = SendSlot::initial_ping(name(), now, pingIntervalMs);
    return false;
  }
  return due_steps(now, lastPing, pingIntervalMs);
}

void corium::HttpPoster::take_reply()
{
  char r[REPLY_CAP];
  size_t cap = reply_cap();
  if (cap > sizeof(r)) cap = sizeof(r);
  int code;
  size_t got;
  if (!request.take(&code, (uint8_t*)r, cap - 1, &got)) return;
  bool ok = false;
  if (ok_code(code)) {
    r[got] = 0;
    ok = interpret(r);
  } else {
    Log::console(PSTR("%s: Error %d - %s"), log_tag(), code, (PGM_P)EGHttpClient::reason(code));
  }
  note_result(ok);
}

bool corium::HttpPoster::url_fits(int n, size_t cap) {
  if (n >= 0 && (size_t)n < cap) return true;
  Log::console(PSTR("%s: URL too long, not sent"), log_tag());
  note_publish(false);
  return false;
}

void corium::HttpPoster::post() {
  if (request.busy()) return;   // last reply not in yet
  char url[URL_CAP];
  const char* body = nullptr;
  if (!prepare(url, sizeof(url), &body)) return;
  send(url, body);
  free((void*)body);   // the client copied it
}

void corium::HttpPoster::send(const char* url, const char* body) {
  corium::HttpMeta m;
  m.method = method();
  m.url = url;
  m.ctype = content_type();
  m.accept = m.ctype;
  m.timeout_s = timeout_s();
  corium::HttpDriver::Sent s = request.send(m, (const uint8_t*)body, body ? strlen(body) : 0);
  if (s == corium::HttpDriver::NO_MEMORY) {
    Log::console(PSTR("%s: alloc failed"), log_tag());
    note_publish(false);
    return;
  }
  note_attempt();
  if (s == corium::HttpDriver::NOT_OPENED) {
    Log::console(PSTR("%s: Can't send request"), log_tag());
    note_result(false);
    return;
  }
  corium::Registry::emit(TOPIC_SENT);
}
