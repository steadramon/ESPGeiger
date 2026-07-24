/*
  EGModule.cpp - Out-of-line bodies for the EGModule base class.

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
#include "EGModule.h"

// fast_millis() is read before the lock so the ESP32 critical section stays
// short. EGMOD_PUB_LOCK/UNLOCK are no-ops on ESP8266.

void EGModule::note_publish(bool ok) {
  unsigned long now_ms = fast_millis();
  EGMOD_PUB_LOCK();
  last_ok = ok;
  last_attempt_ms = now_ms;
  if (ok) { if (_pub_ok < 0xFF) ++_pub_ok; }
  else    { if (_pub_err < 0xFF) ++_pub_err; }
  EGMOD_PUB_UNLOCK();
}

void EGModule::note_attempt() {
  unsigned long now_ms = fast_millis();
  EGMOD_PUB_LOCK();
  last_attempt_ms = now_ms;
  last_ok = true;
  _publish_pending = 1;
  if (_pub_ok < 0xFF) ++_pub_ok;
  EGMOD_PUB_UNLOCK();
}

void EGModule::note_result(bool ok) {
  EGMOD_PUB_LOCK();
  if (_publish_pending) {
    _publish_pending = 0;
    last_ok = ok;
    if (!ok) {
      if (_pub_ok > 0) --_pub_ok;
      if (_pub_err < 0xFF) ++_pub_err;
    }
  }
  EGMOD_PUB_UNLOCK();
}

uint8_t EGModule::take_publish_ok() {
  EGMOD_PUB_LOCK();
  uint8_t v = _pub_ok;
  _pub_ok = 0;
  EGMOD_PUB_UNLOCK();
  return v;
}

uint8_t EGModule::take_publish_err() {
  EGMOD_PUB_LOCK();
  uint8_t v = _pub_err;
  _pub_err = 0;
  EGMOD_PUB_UNLOCK();
  return v;
}

size_t EGModule::write_status_json(char* buf, size_t cap, const char* key,
                                   bool ok, unsigned long last_attempt_ms,
                                   unsigned long now) {
  if (last_attempt_ms == 0) {
    return snprintf_P(buf, cap, PSTR("\"%s\":{\"ok\":false,\"age\":null}"), key);
  }
  return snprintf_P(buf, cap, PSTR("\"%s\":{\"ok\":%s,\"age\":%lu}"), key,
                    ok ? "true" : "false", (now - last_attempt_ms) / 1000);
}
