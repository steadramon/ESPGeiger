/*
  DeviceInfoChip.h - what each chip's DeviceInfo file gives DeviceInfo.cpp

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
#ifndef CORIUM_DEVICEINFOCHIP_H
#define CORIUM_DEVICEINFOCHIP_H

#include <stddef.h>
#include <stdint.h>

namespace corium {
namespace DeviceInfo {
namespace Chip {
  uint32_t id();   // low 24 bits name the device
  void flashText(char* buf, size_t cap);   // "ef4016 DIO 40MHz"
  uint8_t heapRead(uint32_t* lfb);   // frag %, and the largest free block

  struct ResetRow {
    uint8_t cause;   // sdk reset cause
    uint8_t rr;
    uint8_t kind;    // CrashDump::Kind
  };
  const ResetRow* resets(size_t* n);   // PROGMEM on the 8266
  uint32_t resetCause();
}
}
}  // namespace corium

#endif
