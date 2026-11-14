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

#ifndef CORIUM_RTCMAP_H
#define CORIUM_RTCMAP_H

#include <stdint.h>
#include "App/AppConfig.h"

#ifndef CORIUM_RTCMAP_APP1_WORDS
#define CORIUM_RTCMAP_APP1_WORDS 4
#endif
#ifndef CORIUM_RTCMAP_APP2_WORDS
#define CORIUM_RTCMAP_APP2_WORDS 4
#endif

namespace corium {
namespace RtcMap {

constexpr uint32_t EBOOT_WORDS = 32;

// Core
constexpr uint32_t BOOTGUARD_AT    = 52;
constexpr uint32_t BOOTGUARD_WORDS = 8;
// esp32: the last time() seen, what a kept clock is checked against
constexpr uint32_t CLOCK_AT        = 64;
constexpr uint32_t CLOCK_WORDS     = 2;
// past eboot, a crash before the OTA reboot can't eat its command
constexpr uint32_t CRASHDUMP_AT    = 76;
constexpr uint32_t CRASHDUMP_WORDS = 43;

// App
constexpr uint32_t APP1_AT    = 48;
constexpr uint32_t APP1_WORDS = CORIUM_RTCMAP_APP1_WORDS;
constexpr uint32_t APP2_AT    = 60;
constexpr uint32_t APP2_WORDS = CORIUM_RTCMAP_APP2_WORDS;

static_assert(EBOOT_WORDS <= APP1_AT,                         "app words run into eboot");
static_assert(APP1_AT + APP1_WORDS <= BOOTGUARD_AT,           "app words run into BootGuard");
static_assert(BOOTGUARD_AT + BOOTGUARD_WORDS <= APP2_AT,      "BootGuard runs into app words");
static_assert(APP2_AT + APP2_WORDS <= CLOCK_AT,               "app words run into Clock");
static_assert(CLOCK_AT + CLOCK_WORDS <= CRASHDUMP_AT,          "Clock runs into CrashDump");
static_assert(CRASHDUMP_AT + CRASHDUMP_WORDS <= 128,          "past the 128 user words");

}
}  // namespace corium

#include <stddef.h>
#ifdef ESP8266
#include <Arduino.h>

namespace corium {
namespace RtcMap {
inline bool get(uint32_t at, void* p, size_t n) { return ESP.rtcUserMemoryRead(at, (uint32_t*)p, n); }
inline void put(uint32_t at, const void* p, size_t n) { ESP.rtcUserMemoryWrite(at, (uint32_t*)p, n); }
}
}  // namespace corium
#else
#include <string.h>
#ifdef ESP32
#include <esp_attr.h>
#define CORIUM_RTC_NOINIT RTC_NOINIT_ATTR
#else
#define CORIUM_RTC_NOINIT
#endif

namespace corium {
namespace RtcMap {
// esp32: app and clock words as one block, garbage after power-on. its size never changes:
// size fixed, the IDF's rtc time sits behind it
constexpr uint32_t BLOCK_AT    = APP1_AT;
constexpr uint32_t BLOCK_WORDS = CLOCK_AT + CLOCK_WORDS - APP1_AT;

inline uint32_t* block() {
  static CORIUM_RTC_NOINIT uint32_t words[BLOCK_WORDS];
  return words;
}
inline bool get(uint32_t at, void* p, size_t n) { memcpy(p, block() + at - BLOCK_AT, n); return true; }
inline void put(uint32_t at, const void* p, size_t n) { memcpy(block() + at - BLOCK_AT, p, n); }
}
}  // namespace corium
#endif

namespace corium {
// one owner's words, T starts with its magic
template <typename T, uint32_t AT, uint32_t WORDS>
struct RtcSlot {
  static_assert(sizeof(T) <= WORDS * 4, "outgrew its RTC words");
  static_assert(sizeof(T) % 4 == 0, "RTC memory is whole words");
  static_assert(offsetof(T, magic) == 0, "magic goes first, clear() zeroes it");
#ifndef ESP8266
  static_assert(AT >= RtcMap::BLOCK_AT && AT + WORDS <= RtcMap::BLOCK_AT + RtcMap::BLOCK_WORDS, "outside the block");
#endif

  static bool read(T& v) { return RtcMap::get(AT, &v, sizeof(T)); }
  static void write(const T& v) { RtcMap::put(AT, &v, sizeof(T)); }
  static void clear() {
    uint32_t zero = 0;
    RtcMap::put(AT, &zero, sizeof(zero));
  }
};
}  // namespace corium
// either chip: CORIUM_RTC_SLOT(Slot, T, AT, WORDS); then Slot::read(), write()
#define CORIUM_RTC_SLOT(Name, T, AT, WORDS) typedef corium::RtcSlot<T, AT, WORDS> Name

#endif
