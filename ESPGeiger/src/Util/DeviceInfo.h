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
#include "../NTP/NTP.h"

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

// Query-string suffix for static assets. Compile-time concat - browsers
// treat /style.css?v=<hash> as a new URL per firmware, busting stale
// JS/CSS automatically. Empty GIT_VERSION emits `?v=` which is harmless.
#define EG_CACHE_BUST "?v=" GIT_VERSION

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
#endif

#ifndef THING_NAME
#define THING_NAME "ESPGeiger"
#endif

namespace DeviceInfo {
  // Populates identity buffers; call once early in setup() before any
  // module that publishes hostname/mac/etc.
  void begin();

  // Wipes prefs + WiFi backup + SDK creds. KEEPS /api.key so the WebAPI
  // station identity survives. EGPrefs must already be initialised.
  void factoryReset();

  const char* hostname();
  const char* chipid();
  const char* useragent();
  const char* chipmodel();
  const char* geigermodel();
  void setGeigermodel(const char* s);
  // Bitflag: bit 0 = alpha, bit 1 = beta, bit 2 = gamma.
  uint8_t tubeDetection();
  const char* friendlyName();
  void setFriendlyName(const char* s);
  const char* mac();

  inline unsigned long uptime() { return ntpclient.getUptime(); }

  char* uptimeString();

  uint32_t freeHeap();

  // Heap fragmentation 0-100 %, cached 10 s. Denominator-sensitive across
  // builds; prefer largestFreeBlock() for cross-build comparisons.
  uint8_t heapFrag();
  uint8_t heapFragPeak();
  void    heapFragPeakReset();

  // Largest contiguous free block in bytes. Honest cross-build metric.
  // *Low()*  = low-water mark since last reset.
  uint32_t largestFreeBlock();
  uint32_t largestFreeBlockLow();
  void     largestFreeBlockLowReset();

  // Cross-platform reset-reason. Codes are frozen - never renumber.
  //   0 unknown   1 power-on     2 external reset   3 software restart
  //   4 exception 5 watchdog     6 brown-out        7 deep-sleep wake
  uint8_t resetReason();

  // The only ESP.restart() in the codebase. Flushes lifetime to flash first.
  void safeRestart(uint32_t delayMs = 0);

  // Returns false when not available (ESP32 lacks per-fault details).
  // epc2/epc3/depc are optional; depc holds the faulting PC of a
  // double-exception (load/store during exception handling), which epc1
  // does not. Pass nullptr to skip any of them.
  bool resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                uint32_t* epc2 = nullptr, uint32_t* epc3 = nullptr,
                uint32_t* depc = nullptr);

  // Compile-time optional-module bitmask. Bits are frozen - never reassign.
  //   bit 0  MQTT          (MQTTOUT)
  //   bit 1  Radmon        (RADMONOUT)
  //   bit 2  ThingSpeak    (THINGSPEAKOUT)
  //   bit 3  Webhook       (WEBHOOKOUT)
  //   bit 4  Serial out    (SERIALOUT)
  //   bit 5  GMC out       (GMCOUT)
  //   bit 6  OLED display  (SSD1306_DISPLAY)
  //   bit 7  Push button   (GEIGER_PUSHBUTTON)
  //   bit 8  SD logger     (GEIGER_SDCARD)
  //   bit 9  NeoPixel      (GEIGER_NEOPIXEL)
  uint16_t featureFlags();
}

#endif
