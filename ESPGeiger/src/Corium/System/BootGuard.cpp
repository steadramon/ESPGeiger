/*
  BootGuard.cpp - see header.

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
#include "BootGuard.h"

#ifdef ESP8266
#include <Arduino.h>

namespace BootGuard {

static constexpr uint32_t MAGIC      = 0xE56B0007;
static constexpr uint32_t RTC_OFFSET = 40;

struct State {
  uint32_t magic;
  uint8_t  count;
  uint8_t  pad[3];
};

static State s = { 0, 0, { 0, 0, 0 } };

void on_boot() {
  State stored;
  if (!ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&stored, sizeof(stored))) {
    stored.magic = 0;
  }
  if (stored.magic != MAGIC) {
    s.magic = MAGIC;
    s.count = 0;
  } else {
    s = stored;
  }
  if (s.count < 255) s.count++;
  ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&s, sizeof(s));
}

void mark_ok() {
  s.magic = MAGIC;
  s.count = 0;
  ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&s, sizeof(s));
}

uint8_t boot_count() { return s.count; }

} // namespace BootGuard

#else  // ESP32: stubbed for v0.12.0. v0.13.x replaces this with
       // esp_ota_mark_app_valid_cancel_rollback() against the OTA
       // partition state, which gives true bootloader-level rollback.

namespace BootGuard {
void    on_boot()    {}
void    mark_ok()    {}
uint8_t boot_count() { return 0; }
}

#endif
