/*
  KeyStore.cpp - small keys kept outside LittleFS, so a format keeps them.

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
#ifndef CORIUM_KEYSTORE_NS
#define CORIUM_KEYSTORE_NS "corium"
#endif
#ifndef CORIUM_KEYSTORE_MAGIC
#define CORIUM_KEYSTORE_MAGIC 0x4B524F43   // "CORK"
#endif

#if defined(ESP32)
#include <nvs.h>

static const char NVS_NS[] = CORIUM_KEYSTORE_NS;
static const char* const NVS_KEYS[corium::KeyStore::SLOTS] = {"key", "key1"};

bool corium::KeyStore::read(uint8_t* out, size_t len, uint8_t slot) {
  if (slot >= SLOTS) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t n = 0;
  bool ok = nvs_get_blob(h, NVS_KEYS[slot], nullptr, &n) == ESP_OK && n == len &&
            nvs_get_blob(h, NVS_KEYS[slot], out, &n) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool corium::KeyStore::write(const uint8_t* in, size_t len, uint8_t slot) {
  if (len > MAX || slot >= SLOTS) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_blob(h, NVS_KEYS[slot], in, len) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

void corium::KeyStore::clear(uint8_t slot) {
  if (slot >= SLOTS) return;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, NVS_KEYS[slot]);
  nvs_commit(h);
  nvs_close(h);
}

#elif defined(ESP8266)
#include <Arduino.h>
#include <coredecls.h>

// the core's eeprom sector, under the sdk's own
extern "C" uint32_t _EEPROM_start;  // core linker symbol

namespace {
constexpr uint32_t MAGIC = CORIUM_KEYSTORE_MAGIC;

struct Slot {
  uint32_t len;
  uint8_t  bytes[corium::KeyStore::MAX];
  uint32_t crc;   // len + bytes
};

// slot 0 sits where the one key record had it, both ways read it
struct Rec {
  uint32_t magic;
  Slot     slot[corium::KeyStore::SLOTS];
};
static_assert(sizeof(Rec) % 4 == 0, "flash takes whole words");

uint32_t rec_addr() { return (uint32_t)(uintptr_t)&_EEPROM_start - 0x40200000; }

uint32_t slot_crc(const Slot& s) {
  return crc32(&s.len, sizeof(s.len) + sizeof(s.bytes));
}

// another magic = nothing stored
bool load(Rec& r) {
  if (!ESP.flashRead(rec_addr(), (uint32_t*)&r, sizeof(r))) return false;
  if (r.magic != MAGIC) memset(&r, 0, sizeof(r));
  return true;
}

// the other slots go back as they were
bool store(const Rec& r) {
  return ESP.flashEraseSector(rec_addr() / SPI_FLASH_SEC_SIZE) &&
         ESP.flashWrite(rec_addr(), (const uint32_t*)&r, sizeof(r));
}
}  // namespace

bool corium::KeyStore::read(uint8_t* out, size_t len, uint8_t slot) {
  if (len > MAX || slot >= SLOTS) return false;
  Rec r;
  if (!load(r)) return false;
  const Slot& s = r.slot[slot];
  if (s.len != len || s.crc != slot_crc(s)) return false;
  memcpy(out, s.bytes, len);
  return true;
}

bool corium::KeyStore::write(const uint8_t* in, size_t len, uint8_t slot) {
  if (len > MAX || slot >= SLOTS) return false;
  Rec r;
  if (!load(r)) return false;
  r.magic = MAGIC;
  Slot& s = r.slot[slot];
  memset(&s, 0, sizeof(s));
  s.len = len;
  memcpy(s.bytes, in, len);
  s.crc = slot_crc(s);
  return store(r);
}

// a zero slot fails its crc
void corium::KeyStore::clear(uint8_t slot) {
  Rec r;
  if (slot >= SLOTS || !load(r)) return;
  memset(&r.slot[slot], 0, sizeof(Slot));
  store(r);
}
#endif
