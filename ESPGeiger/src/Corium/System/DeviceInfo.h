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
#ifdef ESP32
#include <esp_task_wdt.h>
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
#ifdef ESP8266
  inline uint32_t realFlash()  { return ESP.getFlashChipRealSize(); }
  inline uint8_t bootVersion() { return ESP.getBootVersion(); }
  String coreVersion();
#elif defined(ESP32)
  inline const char* chipModel()  { return ESP.getChipModel(); }
  inline uint8_t chipRev()        { return ESP.getChipRevision(); }
  inline const char* sdkVersion() { return ESP.getSdkVersion(); }
  bool efuseMac(uint8_t* out);   // 6 bytes
#endif

  inline uint32_t hwRandom() {
#ifdef ESP8266
    return RANDOM_REG32;
#else
    return esp_random();
#endif
  }

  // ram left from before the boot, a seed; the esp32's survives a warm reset, bootRamSet() changes it
#ifdef ESP8266
  inline const uint8_t* bootRam(size_t& n) {
    n = 0x800;
    return (const uint8_t*)(0x3FFFC000 - 0x800);   // top of dram
  }
  inline void bootRamSet(const uint8_t*, size_t) {}
#else
  extern uint8_t bootRamBuf[256];   // .noinit
  inline const uint8_t* bootRam(size_t& n) {
    n = sizeof(bootRamBuf);
    return bootRamBuf;
  }
  inline void bootRamSet(const uint8_t* b, size_t n) { memcpy(bootRamBuf, b, n); }
#endif

  inline __attribute__((always_inline)) uint32_t cycles() { return ESP.getCycleCount(); }
  inline uint32_t heapNow() { return ESP.getFreeHeap(); }   // raw, freeHeap() is smoothed
  // raw too
#ifdef ESP8266
  void heapStats(uint32_t* free_, uint16_t* max_, uint8_t* frag);
#else
  inline uint32_t heapMin() { return ESP.getMinFreeHeap(); }
  inline uint32_t heapMaxBlock() { return ESP.getMaxAllocHeap(); }
#endif

  unsigned long uptime();

  char* uptimeString();

  uint32_t freeHeap();

  uint8_t heapFrag();   // %

  uint32_t largestFreeBlock();
  uint32_t largestFreeBlockLow();       // since boot
  uint32_t largestFreeBlockLowTake();   // since the last take, one taker

#ifdef ESP32
  float chipTempC();   // die not ambient, classic esp32 reads high
  extern uint8_t ctWait;
  void chipTempRead();
  // 1 Hz, no call till a read is due
  inline void chipTempSample() {
    if (ctWait) { ctWait--; return; }
    chipTempRead();
  }
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

  // 8266 sw wdt only, the hw wdt still runs; esp32 a fed task wdt, 5 s from here
  inline void wdtPause() {
#ifdef ESP8266
    ESP.wdtDisable();
#elif defined(ESP32)
    esp_task_wdt_reset();
#endif
  }
  inline void wdtResume() {
#ifdef ESP8266
    ESP.wdtEnable(WDTO_8S);
#elif defined(ESP32)
    esp_task_wdt_reset();
#endif
  }

  // false on esp32
  bool resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                uint32_t* epc2 = nullptr, uint32_t* epc3 = nullptr,
                uint32_t* depc = nullptr);
}
}  // namespace corium

#endif
