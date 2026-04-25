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
#include "sha256.h"
#include <string.h>

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

void GRNG::extract(uint8_t* out, size_t n) {
  while (n > 0) {
    Sha256.init();
    Sha256.write((const uint8_t*)&s_pool, sizeof(s_pool));
    for (const char* p = DeviceInfo::mac(); *p; ++p) Sha256.write((uint8_t)*p);
    uint32_t cc = ESP.getCycleCount();
    Sha256.write((const uint8_t*)&cc, sizeof(cc));
#ifdef ESP8266
    uint32_t hw = RANDOM_REG32;
#else
    uint32_t hw = esp_random();
#endif
    Sha256.write((const uint8_t*)&hw, sizeof(hw));
    uint8_t* h = Sha256.result();
    size_t take = n > 32 ? 32 : n;
    memcpy(out, h, take);
    out += take;
    n -= take;
    s_pool ^= ((uint32_t*)h)[0];
  }
}
