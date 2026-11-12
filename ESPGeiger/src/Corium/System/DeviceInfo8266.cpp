/*
  DeviceInfo8266.cpp - DeviceInfo, the esp8266 half.

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
#ifdef ESP8266
#include "DeviceInfo.h"
extern "C" {
#include <user_interface.h>
}

void corium::DeviceInfo::heapStats(uint32_t* free_, uint16_t* max_, uint8_t* frag) {
  ESP.getHeapStats(free_, max_, frag);
}

uint64_t corium::DeviceInfo::hwId() {
  return ESP.getChipId();
}

String corium::DeviceInfo::coreVersion() { return ESP.getCoreVersion(); }

bool corium::DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                          uint32_t* epc2, uint32_t* epc3, uint32_t* depc) {
  rst_info* ri = ESP.getResetInfoPtr();
  if (!ri || ri->reason != REASON_EXCEPTION_RST) return false;
  if (epc1)     *epc1     = ri->epc1;
  if (excvaddr) *excvaddr = ri->excvaddr;
  if (exccause) *exccause = (uint8_t)ri->exccause;
  if (epc2)     *epc2     = ri->epc2;
  if (epc3)     *epc3     = ri->epc3;
  if (depc)     *depc     = ri->depc;
  return true;
}
#endif
