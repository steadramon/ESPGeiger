/*
  DeviceInfo8266.cpp - DeviceInfo, the esp8266 half

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
#include "DeviceInfoChip.h"
#include "CrashDump.h"
extern "C" {
#include <user_interface.h>
}

extern char _heap_start[];
uint32_t corium::DeviceInfo::heapSize() { return 0x3FFFC000u - (uint32_t)(uintptr_t)_heap_start; }   // same end as umm uses

void corium::DeviceInfo::heapStats(uint32_t* free_, uint16_t* max_, uint8_t* frag) {
  ESP.getHeapStats(free_, max_, frag);
}

uint64_t corium::DeviceInfo::hwId() {
  return ESP.getChipId();
}

String corium::DeviceInfo::coreVersion() { return ESP.getCoreVersion(); }

const char* corium::DeviceInfo::chip() { return "ESP8266"; }

uint32_t corium::DeviceInfo::Chip::id() { return ESP.getChipId(); }

void corium::DeviceInfo::Chip::flashText(char* buf, size_t cap) {
  const uint8_t fm = (uint8_t)ESP.getFlashChipMode();
  static const char FLASH_MODES[][5] PROGMEM = {"QIO", "QOUT", "DIO", "DOUT"};
  const char* mode = fm < 4 ? FLASH_MODES[fm] : PSTR("?");
  const unsigned long mhz = (unsigned long)(ESP.getFlashChipSpeed() / 1000000ul);
  snprintf_P(buf, cap, PSTR("%06lx %s %luMHz"),
             (unsigned long)(ESP.getFlashChipId() & 0xFFFFFFul), mode, mhz);
}

uint8_t corium::DeviceInfo::Chip::heapRead(uint32_t* lfb) {
  uint32_t free_;
  uint16_t max_;
  uint8_t frag_;
  ESP.getHeapStats(&free_, &max_, &frag_);
  *lfb = max_;
  return frag_;
}

namespace CD = corium::CrashDump;
static const corium::DeviceInfo::Chip::ResetRow RESETS[] PROGMEM = {
  {REASON_DEFAULT_RST,      corium::DeviceInfo::RR_POWER_ON,   CD::K_POWER_ON},
  {REASON_WDT_RST,          corium::DeviceInfo::RR_WDT,        CD::K_HW_WDT},
  {REASON_EXCEPTION_RST,    corium::DeviceInfo::RR_EXCEPTION,  CD::K_EXCEPTION},
  {REASON_SOFT_WDT_RST,     corium::DeviceInfo::RR_WDT,        CD::K_SW_WDT},
  {REASON_SOFT_RESTART,     corium::DeviceInfo::RR_RESTART,    CD::K_RESTART},
  {REASON_DEEP_SLEEP_AWAKE, corium::DeviceInfo::RR_DEEP_SLEEP, CD::K_DEEP_SLEEP},
  {REASON_EXT_SYS_RST,      corium::DeviceInfo::RR_PIN,        CD::K_PIN},
};

const corium::DeviceInfo::Chip::ResetRow* corium::DeviceInfo::Chip::resets(size_t* n) {
  *n = sizeof(RESETS) / sizeof(RESETS[0]);
  return RESETS;
}

uint32_t corium::DeviceInfo::Chip::resetCause() { return ESP.getResetInfoPtr()->reason; }

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
