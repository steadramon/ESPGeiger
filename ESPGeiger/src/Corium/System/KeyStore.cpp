/*
  KeyStore.cpp - one small key kept outside LittleFS, so a format keeps it.

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

#include "KeyStore.h"
#include "App/AppConfig.h"
#include <string.h>

// app sets these, a change loses the key
#ifndef KEYSTORE_NS
#define KEYSTORE_NS "corium"
#endif
#ifndef KEYSTORE_MAGIC
#define KEYSTORE_MAGIC 0x4B524F43   // "CORK"
#endif

#if defined(ESP32)
#include <nvs.h>

static const char NVS_NS[]  = KEYSTORE_NS;
static const char NVS_KEY[] = "key";

bool KeyStore::read(uint8_t* out, size_t len) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t n = 0;
  bool ok = nvs_get_blob(h, NVS_KEY, nullptr, &n) == ESP_OK && n == len &&
            nvs_get_blob(h, NVS_KEY, out, &n) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool KeyStore::write(const uint8_t* in, size_t len) {
  if (len > MAX) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_blob(h, NVS_KEY, in, len) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

void KeyStore::clear() {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, NVS_KEY);
  nvs_commit(h);
  nvs_close(h);
}

#elif defined(ESP8266)
#include <Arduino.h>
#include <coredecls.h>

// the core's eeprom sector, under the sdk's own
extern "C" uint32_t _EEPROM_start;

namespace {
constexpr uint32_t MAGIC = KEYSTORE_MAGIC;

struct Rec {
  uint32_t magic;
  uint32_t len;
  uint8_t  data[KeyStore::MAX];
  uint32_t crc;   // len + data
};
static_assert(sizeof(Rec) % 4 == 0, "flash takes whole words");

uint32_t rec_addr() { return (uint32_t)(uintptr_t)&_EEPROM_start - 0x40200000; }

uint32_t rec_crc(const Rec& r) {
  return crc32(&r.len, sizeof(r.len) + sizeof(r.data));
}
}  // namespace

bool KeyStore::read(uint8_t* out, size_t len) {
  if (len > MAX) return false;
  Rec r;
  if (!ESP.flashRead(rec_addr(), (uint32_t*)&r, sizeof(r))) return false;
  if (r.magic != MAGIC || r.len != len || r.crc != rec_crc(r)) return false;
  memcpy(out, r.data, len);
  return true;
}

bool KeyStore::write(const uint8_t* in, size_t len) {
  if (len > MAX) return false;
  Rec r;
  memset(&r, 0, sizeof(r));
  r.magic = MAGIC;
  r.len = len;
  memcpy(r.data, in, len);
  r.crc = rec_crc(r);
  return ESP.flashEraseSector(rec_addr() / SPI_FLASH_SEC_SIZE) &&
         ESP.flashWrite(rec_addr(), (uint32_t*)&r, sizeof(r));
}

void KeyStore::clear() {
  ESP.flashEraseSector(rec_addr() / SPI_FLASH_SEC_SIZE);
}
#endif
