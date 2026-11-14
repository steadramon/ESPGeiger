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
#include <math.h>
#include <esp_system.h>
#include <esp_mac.h>

uint8_t corium::DeviceInfo::bootRamBuf[256] __attribute__((section(".noinit")));

static float s_temp_c = NAN;
uint8_t corium::DeviceInfo::ctWait = 0;

// next read 45-75 s on, then a 1/16 ema, out of range reads dropped
void corium::DeviceInfo::chipTempRead() {
  ctWait = 45 + (uint8_t)(esp_random() % 31);
  float t = temperatureRead();
  if (isnan(t) || t <= -40.0f || t >= 150.0f) return;
  s_temp_c = isnan(s_temp_c) ? t : s_temp_c + (t - s_temp_c) * 0.0625f;
}

float corium::DeviceInfo::chipTempC() { return s_temp_c; }

uint64_t corium::DeviceInfo::hwId() {
  return ESP.getEfuseMac();
}

bool corium::DeviceInfo::efuseMac(uint8_t* out) { return esp_efuse_mac_get_default(out) == ESP_OK; }

bool corium::DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                          uint32_t* epc2, uint32_t* epc3, uint32_t* depc) {
  (void)epc1; (void)excvaddr; (void)exccause;
  (void)epc2; (void)epc3; (void)depc;
  return false;
}
#endif
