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
// keep host-buildable, web stuff in GRNGWeb.cpp

#include "GRNG.h"
#include "Corium/Module/EGModule.h"   // EG_XTASK_VOLATILE
#include "Corium/System/DeviceInfo.h"
#include "sha256.h"
#include <string.h>

static EG_XTASK_VOLATILE uint32_t s_pool[8] = {0};
static uint8_t s_mix_idx = 0;

// own hasher, runs inside uECC_sign
static Sha256Class s_sha;

// survives warm reset
#ifdef NATIVE_TEST
#  define EG_NOINIT
#else
#  define EG_NOINIT __attribute__((section(".noinit")))
#endif

#ifndef ESP8266
static uint8_t s_boot_sram[256] EG_NOINIT;
#endif

GRNG::GRNG() {
};

void GRNG::begin() {
  s_sha.init();
#ifdef ESP8266
  s_sha.write((const uint8_t*)(0x3FFFC000 - 0x800), 0x800);
#else
  s_sha.write(s_boot_sram, sizeof(s_boot_sram));
#endif
  uint32_t cc = ESP.getCycleCount();
  s_sha.write((const uint8_t*)&cc, sizeof(cc));
  uint8_t* h = s_sha.result();
  for (uint8_t i = 0; i < 8; i++) s_pool[i] ^= ((uint32_t*)h)[i];
#ifndef ESP8266
  memcpy(s_boot_sram, h, 32);
#endif
  for (uint8_t i = 0; i < 8; i++) mix(DeviceInfo::hwRandom());
  stir();
}

void GRNG::mix(uint32_t bits) {
  s_pool[s_mix_idx++ & 7] ^= bits;
}

uint32_t GRNG::stir() {
  uint32_t e = DeviceInfo::hwRandom() ^ ESP.getCycleCount();
  mix(e);
  randomSeed(e ^ s_pool[0]);
  return e;
}

static constexpr uint8_t EXTRACT_HW_WORDS = 4;
// 55 bytes a block, more costs two
static_assert(sizeof(s_pool) + sizeof(uint32_t) + EXTRACT_HW_WORDS * sizeof(uint32_t) <= 55,
              "extract() input past one SHA-256 block");

void GRNG::extract(uint8_t* out, size_t n) {
  while (n > 0) {
    s_sha.init();
    s_sha.write((const uint8_t*)s_pool, sizeof(s_pool));
    uint32_t cc = ESP.getCycleCount();
    s_sha.write((const uint8_t*)&cc, sizeof(cc));
    uint32_t hw[EXTRACT_HW_WORDS];
    for (uint8_t i = 0; i < EXTRACT_HW_WORDS; i++) hw[i] = DeviceInfo::hwRandom();
    s_sha.write((const uint8_t*)hw, sizeof(hw));
    uint8_t* h = s_sha.result();
    size_t take = n > 32 ? 32 : n;
    memcpy(out, h, take);
    out += take;
    n -= take;
    for (uint8_t i = 0; i < 8; i++) s_pool[i] ^= ((uint32_t*)h)[i];
  }
}

uint32_t GRNG::fast_uint32() {
  static uint8_t buf[32];
  static uint8_t pos = sizeof(buf);
  if (pos >= sizeof(buf)) {
    extract(buf, sizeof(buf));
    pos = 0;
  }
  uint32_t r;
  memcpy(&r, buf + pos, sizeof(r));
  pos += sizeof(r);
  return r;
}

void GRNG::extract_fast(uint8_t* out, size_t n) {
  static uint32_t s_xs = 0;
  uint32_t x = s_xs ^ fast_uint32();
  if (x == 0) x = 0x6b8b4567;
  while (n > 0) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    size_t take = n > 4 ? 4 : n;
    memcpy(out, &x, take);
    out += take;
    n -= take;
  }
  s_xs = x;
}
