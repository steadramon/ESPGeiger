/*
  DeviceInfo32.cpp - DeviceInfo, the esp32 half

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
#ifdef ESP32
#include "DeviceInfo.h"
#include "DeviceInfoChip.h"
#include "CrashDump.h"
#include <math.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_mac.h>

uint8_t corium::DeviceInfo::bootRamBuf[256] __attribute__((section(".noinit")));

static float s_temp_c = NAN;
uint8_t corium::DeviceInfo::ctWait = 0;

// Next read is 45-75 s on. Smoothed with a 1/16 ema, out of range reads are dropped
void corium::DeviceInfo::chipTempRead() {
  ctWait = 45 + (uint8_t)(esp_random() % 31);
  float t = temperatureRead();
  if (isnan(t) || t <= -40.0f || t >= 150.0f) return;
  s_temp_c = isnan(s_temp_c) ? t : s_temp_c + (t - s_temp_c) * 0.0625f;
}

uint32_t corium::DeviceInfo::heapSize() { return ESP.getHeapSize(); }

float corium::DeviceInfo::chipTempC() { return s_temp_c; }

uint64_t corium::DeviceInfo::hwId() {
  return ESP.getEfuseMac();
}

// One case per chip
const char* corium::DeviceInfo::chip() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  switch ((int)info.model) {
    case 0:  return "ESP8266";
    case (int)esp_chip_model_t::CHIP_ESP32:   return "ESP32";
    case (int)esp_chip_model_t::CHIP_ESP32S2: return "ESP32-S2";
    case (int)esp_chip_model_t::CHIP_ESP32S3: return "ESP32-S3";
    case (int)esp_chip_model_t::CHIP_ESP32C3: return "ESP32-C3";
    // raw model numbers, not in every IDF enum
    case 6:  return "ESP32-H4";
    case 12: return "ESP32-C2";
    case 13: return "ESP32-C6";
    case 16: return "ESP32-H2";
    default: return "ESP32";
  }
}

// last 3 MAC bytes, like the 8266 chip id
uint32_t corium::DeviceInfo::Chip::id() {
  uint32_t id = 0;
  for (int i = 0; i < 17; i += 8) {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  return id;
}

void corium::DeviceInfo::Chip::flashText(char* buf, size_t cap) {
  const uint8_t fm = (uint8_t)ESP.getFlashChipMode();
  static const char FLASH_MODES[][5] PROGMEM = {"QIO", "QOUT", "DIO", "DOUT"};
  const char* mode = fm < 4 ? FLASH_MODES[fm] : PSTR("?");
  const unsigned long mhz = (unsigned long)(ESP.getFlashChipSpeed() / 1000000ul);
  snprintf_P(buf, cap, PSTR("%s %luMHz"), mode, mhz);
}

// Same frag sum as umm_malloc on the 8266
uint8_t corium::DeviceInfo::Chip::heapRead(uint32_t* lfb) {
  uint32_t free_ = ESP.getFreeHeap();
  uint32_t max_  = ESP.getMaxAllocHeap();
  *lfb = max_;
  if (free_ == 0 || max_ > free_) return 0;
  float ratio = (float)max_ / (float)free_;
  return (uint8_t)(100.0f - sqrtf(ratio) * 100.0f);
}

namespace CD = corium::CrashDump;
static const corium::DeviceInfo::Chip::ResetRow RESETS[] = {
  {ESP_RST_POWERON,   corium::DeviceInfo::RR_POWER_ON,   CD::K_POWER_ON},
  {ESP_RST_EXT,       corium::DeviceInfo::RR_PIN,        CD::K_PIN},
  {ESP_RST_SW,        corium::DeviceInfo::RR_RESTART,    CD::K_RESTART},
  {ESP_RST_PANIC,     corium::DeviceInfo::RR_EXCEPTION,  CD::K_EXCEPTION},
  {ESP_RST_INT_WDT,   corium::DeviceInfo::RR_WDT,        CD::K_INT_WDT},
  {ESP_RST_TASK_WDT,  corium::DeviceInfo::RR_WDT,        CD::K_TASK_WDT},
  {ESP_RST_WDT,       corium::DeviceInfo::RR_WDT,        CD::K_WDT},
  {ESP_RST_BROWNOUT,  corium::DeviceInfo::RR_BROWNOUT,   CD::K_BROWNOUT},
  {ESP_RST_DEEPSLEEP, corium::DeviceInfo::RR_DEEP_SLEEP, CD::K_DEEP_SLEEP},
};

const corium::DeviceInfo::Chip::ResetRow* corium::DeviceInfo::Chip::resets(size_t* n) {
  *n = sizeof(RESETS) / sizeof(RESETS[0]);
  return RESETS;
}

uint32_t corium::DeviceInfo::Chip::resetCause() { return esp_reset_reason(); }

bool corium::DeviceInfo::efuseMac(uint8_t* out) { return esp_efuse_mac_get_default(out) == ESP_OK; }

bool corium::DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                                  uint32_t* epc2, uint32_t* epc3, uint32_t* depc) {
  (void)epc1; (void)excvaddr; (void)exccause;
  (void)epc2; (void)epc3; (void)depc;
  return false;
}
#endif
