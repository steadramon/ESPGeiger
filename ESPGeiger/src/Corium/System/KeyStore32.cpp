/*
  KeyStore32.cpp - the key in NVS, the esp32's


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
#include "KeyStore.h"
#include "App/AppConfig.h"
#include <string.h>

// The app sets it, changing it loses the key
#ifndef CORIUM_KEYSTORE_NS
#define CORIUM_KEYSTORE_NS "corium"
#endif

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

// Nothing there counts as cleared too
bool corium::KeyStore::clear(uint8_t slot) {
  if (slot >= SLOTS) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_erase_key(h, NVS_KEYS[slot]);
  bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
#endif
