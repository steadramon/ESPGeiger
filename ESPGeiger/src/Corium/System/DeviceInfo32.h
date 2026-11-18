/*
  DeviceInfo32.h - DeviceInfo, the esp32 half, inline

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
#ifndef CORIUM_DEVICEINFO32_H
#define CORIUM_DEVICEINFO32_H

// Included from DeviceInfo.h
#include <esp_task_wdt.h>

namespace corium {
namespace DeviceInfo {
  // /info
  inline const char* chipModel()  { return ESP.getChipModel(); }
  inline uint8_t chipRev()        { return ESP.getChipRevision(); }
  inline const char* sdkVersion() { return ESP.getSdkVersion(); }
  bool efuseMac(uint8_t* out);   // 6 bytes

  inline uint32_t hwRandom() { return esp_random(); }

  // Ram left from before boot, used as a seed
  extern uint8_t bootRamBuf[256];   // .noinit
  inline const uint8_t* bootRam(size_t& n) {
    n = sizeof(bootRamBuf);
    return bootRamBuf;
  }
  inline void bootRamSet(const uint8_t* b, size_t n) { memcpy(bootRamBuf, b, n); }

  // raw
  inline uint32_t heapMin() { return ESP.getMinFreeHeap(); }
  inline uint32_t heapMaxBlock() { return ESP.getMaxAllocHeap(); }

  inline uint32_t stackFree() {
    return uxTaskGetStackHighWaterMark(nullptr);   // handlers run on the loop task
  }

  float chipTempC();   // die temp, not ambient. The classic esp32 reads high
  extern uint8_t ctWait;
  void chipTempRead();
  // call at 1 Hz
  inline void chipTempSample() {
    if (ctWait) { ctWait--; return; }
    chipTempRead();
  }

  // Just feeds the task wdt, so 5 s from here
  inline void wdtPause() { esp_task_wdt_reset(); }
  inline void wdtResume() { esp_task_wdt_reset(); }
}
}  // namespace corium

#endif
