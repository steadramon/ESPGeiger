/*
  DeviceInfo8266.h - DeviceInfo, the esp8266 half, inline

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
#ifndef CORIUM_DEVICEINFO8266_H
#define CORIUM_DEVICEINFO8266_H

// from DeviceInfo.h

// Top of dram, a host build points it at its own buffer
#ifndef CORIUM_BOOT_RAM
#define CORIUM_BOOT_RAM (0x3FFFC000 - 0x800)
#endif

namespace corium {
namespace DeviceInfo {
  // /info
  inline uint32_t realFlash()  { return ESP.getFlashChipRealSize(); }
  inline uint8_t bootVersion() { return ESP.getBootVersion(); }
  String coreVersion();

  inline uint32_t hwRandom() { return RANDOM_REG32; }

  // ram left from before boot, used as a seed
  inline const uint8_t* bootRam(size_t& n) {
    n = 0x800;
    return (const uint8_t*)(CORIUM_BOOT_RAM);
  }
  inline void bootRamSet(const uint8_t*, size_t) {}

  // raw
  void heapStats(uint32_t* free_, uint16_t* max_, uint8_t* frag);

  inline uint32_t stackFree() { return ESP.getFreeContStack(); }

  // Only the sw wdt, the hw wdt still runs
  inline void wdtPause() { ESP.wdtDisable(); }
  inline void wdtResume() { ESP.wdtEnable(WDTO_8S); }
}
}  // namespace corium

#endif
