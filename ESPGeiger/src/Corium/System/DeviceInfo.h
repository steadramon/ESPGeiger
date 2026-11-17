/*
  DeviceInfo.h - Device identity and utility functions.

  Copyright (C) 2025 @steadramon

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
#ifndef CORIUM_DEVICEINFO_H
#define CORIUM_DEVICEINFO_H

#include <Arduino.h>
// the chip's own: /info extras, random, boot ram, raw heap, stack, wdt
#if defined(ESP8266)
#include "DeviceInfo8266.h"
#elif defined(ESP32)
#include "DeviceInfo32.h"
#else
#include "DeviceInfoHost.h"
#endif

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#include "App/Version.h"
#include "App/AppConfig.h"

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
#endif

#define CHIPID_LEN 6   // low 24 bits, hex

namespace corium {
namespace DeviceInfo {
  void begin();   // early in setup()
  void factoryReset();

  const char* hostname();
  const char* chipId();
  uint32_t chipHash();   // fnv1a of chipid
  constexpr size_t UA_MAX = sizeof(THING_NAME "/" RELEASE_VERSION " (" GIT_VERSION "; " BUILD_ENV "; )") + CHIPID_LEN;
  void userAgent(char* buf, size_t cap);   // per request, no buffer kept
  const char* chip();
  const char* board();   // the app's, else the chip
  void setBoard(const char* s);   // from setup()
#ifdef CORIUM_BOARD_NAME
  const char* boardName();   // the revision a user sees, nullptr = none
  void setBoardName(const char* s);
#else
  inline const char* boardName() { return nullptr; }
#endif
  const char* friendlyName();
  void setFriendlyName(const char* s);
  // friendly, else the hostname
  inline const char* displayName() {
    const char* fn = friendlyName();
    return (fn && fn[0]) ? fn : hostname();
  }
  const char* mac();
  const char* flashInfo();   // "ef4016 DIO 40MHz"
  PGM_P buildDate();   // one copy
  uint64_t hwId();   // chip id, efuse mac on esp32

  // /info
  inline uint32_t flashSize()  { return ESP.getFlashChipSize(); }
  inline uint32_t cpuMHz()     { return ESP.getCpuFreqMHz(); }
  inline uint32_t sketchSize() { return ESP.getSketchSize(); }
  inline uint32_t freeSketch() { return ESP.getFreeSketchSpace(); }

  inline __attribute__((always_inline)) uint32_t cycles() { return ESP.getCycleCount(); }
  inline uint32_t heapNow() { return ESP.getFreeHeap(); }   // raw, freeHeap() is smoothed
  uint32_t heapSize();   // 8266 dram, the core has no getter; esp32 free + allocated

  unsigned long uptime();

  char* uptimeString();

  uint32_t freeHeap();

  uint8_t heapFrag();   // %

  uint32_t largestFreeBlock();
  uint32_t largestFreeBlockLow();       // since boot
#ifdef CORIUM_LFB_TAKE
  uint32_t largestFreeBlockLowTake();   // since the last take, one taker
#endif

  // on the wire, never renumber
  enum ResetReason : uint8_t {
    RR_UNKNOWN    = 0,
    RR_POWER_ON   = 1,
    RR_PIN        = 2,
    RR_RESTART    = 3,
    RR_EXCEPTION  = 4,
    RR_WDT        = 5,
    RR_BROWNOUT   = 6,
    RR_DEEP_SLEEP = 7,
  };
  uint8_t resetReason();
  const char* resetText();   // /info text

  uint32_t fwFingerprint();   // never 0

  void safeRestart(uint32_t delay_ms = 0);

  // false on esp32
  bool resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                uint32_t* epc2 = nullptr, uint32_t* epc3 = nullptr,
                uint32_t* depc = nullptr);
}
}  // namespace corium

#endif
