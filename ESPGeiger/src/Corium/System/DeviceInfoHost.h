/*
  DeviceInfoHost.h - DeviceInfo off the chips, inline

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
#ifndef CORIUM_DEVICEINFOHOST_H
#define CORIUM_DEVICEINFOHOST_H

// from DeviceInfo.h

namespace corium {
namespace DeviceInfo {
  inline uint32_t hwRandom() { return esp_random(); }

  // Ram from before boot, used as a seed
  extern uint8_t bootRamBuf[256];
  inline const uint8_t* bootRam(size_t& n) {
    n = sizeof(bootRamBuf);
    return bootRamBuf;
  }
  inline void bootRamSet(const uint8_t* b, size_t n) { memcpy(bootRamBuf, b, n); }

  // raw
  inline uint32_t heapMin() { return ESP.getMinFreeHeap(); }
  inline uint32_t heapMaxBlock() { return ESP.getMaxAllocHeap(); }

  inline uint32_t stackFree() { return 0; }   // Not known
  inline uint32_t appMax() { return 0xFFFFFFFFu; }   // no limit past the slot

  inline void wdtPause() {}
  inline void wdtResume() {}
}
}  // namespace corium

#endif
