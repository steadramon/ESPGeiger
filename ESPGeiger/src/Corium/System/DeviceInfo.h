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
#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <Arduino.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#define EG_CACHE_BUST "?v=" GIT_VERSION

#include "App/Version.h"
#include "App/AppConfig.h"

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
#endif

#define CHIPID_LEN 6   // low 24 bits, hex

namespace DeviceInfo {
  void begin();   // early in setup()
  void factoryReset();

  const char* hostname();
  const char* chipid();
  const char* useragent();
  const char* chipmodel();
  const char* friendlyName();
  void setFriendlyName(const char* s);
  const char* mac();
  const char* flashInfo();   // "ef4016 DIO 40MHz"
  uint64_t hwid();   // chip id, efuse mac on esp32

  inline uint32_t hwRandom() {
#ifdef ESP8266
    return RANDOM_REG32;
#else
    return esp_random();
#endif
  }

  unsigned long uptime();

  char* uptimeString();

  uint32_t freeHeap();

  uint8_t heapFrag();   // %

  uint32_t largestFreeBlock();
  uint32_t largestFreeBlockLow();
  void     largestFreeBlockLowReset();

#ifdef ESP32
  float chipTempC();   // die not ambient, classic esp32 reads high
  void chipTempSample();   // 1 Hz
#endif

  // Sent to WebAPI, never renumber
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

  void safeRestart(uint32_t delayMs = 0);

  // false on esp32
  bool resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                uint32_t* epc2 = nullptr, uint32_t* epc3 = nullptr,
                uint32_t* depc = nullptr);
}

#endif
