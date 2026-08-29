/*
  RtcMap.h - who owns which ESP8266 RTC user memory words

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

// RTC user words, survive reset + OTA, not power
// eboot uses 0-31 for OTA

#ifndef ESPG_RTCMAP_H
#define ESPG_RTCMAP_H

#include <stdint.h>

namespace RtcMap {

constexpr uint32_t EBOOT_WORDS = 32;

// Core
constexpr uint32_t BOOTGUARD_AT    = 52;
constexpr uint32_t BOOTGUARD_WORDS = 8;
// past eboot, a crash before the OTA reboot can't eat its command
constexpr uint32_t CRASHDUMP_AT    = 76;
constexpr uint32_t CRASHDUMP_WORDS = 43;

// App
constexpr uint32_t APP1_AT    = 48;
constexpr uint32_t APP1_WORDS = 4;

static_assert(EBOOT_WORDS <= APP1_AT,                         "app words run into eboot");
static_assert(APP1_AT + APP1_WORDS <= BOOTGUARD_AT,           "app words run into BootGuard");
static_assert(BOOTGUARD_AT + BOOTGUARD_WORDS <= CRASHDUMP_AT, "BootGuard runs into CrashDump");
static_assert(CRASHDUMP_AT + CRASHDUMP_WORDS <= 128,          "past the 128 user words");

}

#ifdef ESP8266
#include <Arduino.h>
#include <stddef.h>

// one owner's words, T starts with its magic
template <typename T, uint32_t AT, uint32_t WORDS>
struct RtcSlot {
  static_assert(sizeof(T) <= WORDS * 4, "outgrew its RTC words");
  static_assert(sizeof(T) % 4 == 0, "RTC memory is whole words");
  static_assert(offsetof(T, magic) == 0, "magic goes first, clear() zeroes it");

  static bool read(T& v) { return ESP.rtcUserMemoryRead(AT, (uint32_t*)&v, sizeof(T)); }
  static void write(const T& v) { ESP.rtcUserMemoryWrite(AT, (uint32_t*)&v, sizeof(T)); }
  static void clear() {
    uint32_t zero = 0;
    ESP.rtcUserMemoryWrite(AT, &zero, sizeof(zero));
  }
};
#endif

#endif
