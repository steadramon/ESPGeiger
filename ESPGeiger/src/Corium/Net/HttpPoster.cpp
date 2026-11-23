/*
  HttpPoster.cpp - periodic HTTP upload, shared by the output modules

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

static_assert(EGHTTP_HOST_MAX >= CORIUM_HOST_MAX, "a host pref must fit the client");

bool (*corium::HttpPoster::hold)() = nullptr;

// Turned off mid post it still loops to reap the reply
void corium::HttpPoster::setEnabled(bool on) {
  _send_enabled = on;
  corium::Registry::every(this, (on || request.busy()) ? LOOP_MS : 0);
}

void corium::HttpPoster::onPrefsLoaded() {
  setEnabled(corium::Prefs::groupEnabled(prefsGroup()));
}

void corium::HttpPoster::onPrefsSaved() {
  _told = false;
  _fails = 0;
  _skip = 0;
  onPrefsLoaded();
}

void corium::HttpPoster::result(bool ok) {
  noteResult(ok);
  backoff(ok);
}

// Backs off in whole slots so the send phase holds
void corium::HttpPoster::backoff(bool ok) {
  uint32_t max_s = ok ? 0 : corium::Prefs::getUInt("sys", corium::K_BACKOFF);
  if (!max_s) {
    _fails = 0;
    _skip = 0;
    return;
  }
  if (_fails < 15) _fails++;
  uint32_t slots = 1UL << (_fails - 1);
  uint32_t cap = _every_ms ? max_s * 1000UL / _every_ms : 1;
  if (slots > cap) slots = cap;
  _skip = slots > 1 ? (uint8_t)(slots - 1 > 255 ? 255 : slots - 1) : 0;
}

uint16_t corium::HttpPoster::setIntervalS(int s) {
  _every_ms = (uint32_t)s * 1000UL;
  return (uint16_t)s;
}

size_t corium::HttpPoster::statusJson(char* buf, size_t cap) {
  if (!_send_enabled) return 0;
  return writeStatusJson(buf, cap, statusKey(), lastOk, lastAttempt);
}

void corium::HttpPoster::loop(unsigned long now)
{
  bool got = take_reply();
  if (hold && hold()) {
    corium::Registry::dueAt(this, now + LOOP_MS);
    return;
  }
  if (_send_enabled && got && _again) {
    _again = false;
    post();
  } else if (_send_enabled && slot_due(now)) {
    if (_skip) _skip--;
    else post();
  }
  if (request.busy()) {
    corium::Registry::dueAt(this, request.due());   // the reply posts the module sooner
  } else if (_send_enabled) {
    corium::Registry::dueAt(this, _ping_ms + _every_ms);
  } else {
    corium::Registry::every(this, 0);
  }
}

// Steps in whole intervals, so the send keeps its slot
bool corium::HttpPoster::slot_due(unsigned long now) {
  if (_ping_ms == 0) {
    _ping_ms = corium::SendSlot::initialPing(name(), now, _every_ms);
    return false;
  }
  return corium::dueSteps(now, _ping_ms, _every_ms);
}

bool corium::HttpPoster::take_reply()
{
  char r[REPLY_CAP];
  size_t cap = replyCap();
  if (cap > sizeof(r)) cap = sizeof(r);
  int code;
  size_t got;
  if (!request.take(&code, (uint8_t*)r, cap - 1, &got)) return false;
  bool ok = false;
  if (okCode(code)) {
    while (got && (uint8_t)r[got - 1] <= ' ') got--;   // drop a trailing newline
    r[got] = 0;
    ok = interpret(r);
  } else {
    corium::HttpDriver::logError(logTag(), code);
  }
  result(ok);
  return true;
}

bool corium::HttpPoster::urlFits(int n, size_t cap) {
  if (n >= 0 && (size_t)n < cap) return true;
  corium::Log::console(PSTR("%s: URL too long, not sent"), logTag());
  notePublish(false);
  return false;
}

bool corium::HttpPoster::unset(const char* v, PGM_P label) {
  if (v[0]) return false;
  if (!_told) {
    corium::Log::console(PSTR("%s: %s not set, not sent"), logTag(), label);
    _told = true;
  }
  return true;
}

void corium::HttpPoster::post() {
  if (request.busy()) return;
  char url[URL_CAP];
  const char* body = nullptr;
  _again = false;
  if (!prepare(url, sizeof(url), &body)) {
    _again = false;
    return;
  }
  send(url, body);
  free((void*)body);   // the client copied it
  if (!request.busy()) _again = false;   // not sent, no reply to follow
}

void corium::HttpPoster::send(const char* url, const char* body) {
  corium::HttpMeta m;
  m.method = method();
  m.url = url;
  m.ctype = contentType();
  m.accept = m.ctype;
  m.timeoutS = timeoutS();
  corium::HttpDriver::Sent s = request.send(m, (const uint8_t*)body, body ? strlen(body) : 0);
  if (s == corium::HttpDriver::NO_MEMORY) {
    corium::Log::console(PSTR("%s: alloc failed"), logTag());
    notePublish(false);
    backoff(false);   // memory won't be back by the next slot
    return;
  }
  noteAttempt();
  if (s == corium::HttpDriver::BAD_URL) {
    if (!_told) {
      corium::Log::console(PSTR("%s: URL not usable (%s)"), logTag(), (PGM_P)EGHttpClient::urlWhy(url));
      _told = true;
    }
    result(false);
    return;
  }
  if (s == corium::HttpDriver::NOT_OPENED) {
    corium::Log::console(PSTR("%s: Can't send request"), logTag());
    result(false);
    return;
  }
  corium::Registry::emit(TOPIC_SENT);
}
