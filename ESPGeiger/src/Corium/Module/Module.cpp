/*
  corium::Module.cpp - Out-of-line bodies for the corium::Module base class.

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
#include "Module.h"
#include "Corium/Util/Words.h"

// sent and answered in one go
void corium::Sender::notePublish(bool ok) {
  noteAttempt();
  noteResult(ok);
}

// lastOk stays the last answer till this one's in
void corium::Sender::noteAttempt() {
  lastAttemptMs = corium::fastMillis();
  _publish_pending = 1;
}

// counted on the result, not the attempt
void corium::Sender::noteResult(bool ok) {
  if (!_publish_pending) return;
  _publish_pending = 0;
  lastOk = ok;
  if (ok) { if (_pub_ok < 0xFF) ++_pub_ok; }
  else    { if (_pub_err < 0xFF) ++_pub_err; }
}

uint8_t corium::Sender::takePublishOk() {
  uint8_t v = _pub_ok;
  _pub_ok = 0;
  return v;
}

uint8_t corium::Sender::takePublishErr() {
  uint8_t v = _pub_err;
  _pub_err = 0;
  return v;
}

size_t corium::Sender::writeStatusJson(char* buf, size_t cap, const char* key,
                                   bool ok, unsigned long lastAttemptMs,
                                   unsigned long now) {
  if (lastAttemptMs == 0) {
    return snprintf_P(buf, cap, PSTR("\"%s\":{\"ok\":false,\"age\":null}"), key);
  }
  return snprintf_P(buf, cap, PSTR("\"%s\":{\"ok\":%s,\"age\":%lu}"), key,
                    ok ? W_TRUE : W_FALSE, (now - lastAttemptMs) / 1000);
}
