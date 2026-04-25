/*
  GRNG.cpp - RNG wrapper

  Copyright (C) 2023 @steadramon

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
#include "GRNG.h"
#include "../Logger/Logger.h"
#include "../Util/DeviceInfo.h"

static volatile uint32_t s_pool = 0;

GRNG::GRNG() {
};

void GRNG::begin() {
#ifdef ESP8266
  mix(RANDOM_REG32);
#else
  mix(esp_random());
#endif
  stir();
}

void GRNG::mix(uint32_t bits) {
  s_pool ^= bits;
}

uint32_t GRNG::stir() {
  uint32_t m = 0;
  for (const char* p = DeviceInfo::mac(); *p; ++p) {
    m = (m * 31) ^ (uint8_t)*p;
  }
  uint32_t e = m ^ (uint32_t)millis() ^ (uint32_t)micros()
               ^ ESP.getCycleCount() ^ s_pool;
  randomSeed(e);
  return e;
}
