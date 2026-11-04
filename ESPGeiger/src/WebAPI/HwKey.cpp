/*
  HwKey.cpp - the chip's own signing key (DS peripheral), blob kept in nvs.

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

#include "HwKey.h"
#include <stdlib.h>
#include <string.h>

// DigestInfo for SHA-256
static const uint8_t SHA256_DI[] = {
  0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
  0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20,
};

// 00 01 ff.. 00 DI hash, written back to front
void HwKey::pad(const uint8_t hash[HASH], uint8_t out[SIG]) {
  for (size_t i = 0; i < HASH; i++) out[i] = hash[HASH - 1 - i];
  for (size_t i = 0; i < sizeof(SHA256_DI); i++) out[HASH + i] = SHA256_DI[sizeof(SHA256_DI) - 1 - i];
  size_t at = HASH + sizeof(SHA256_DI);
  out[at++] = 0x00;
  while (at < SIG - 2) out[at++] = 0xFF;
  out[SIG - 2] = 0x01;
  out[SIG - 1] = 0x00;
}

#if defined(ESP32)
#include <soc/soc_caps.h>
#endif

#if defined(ESP32) && SOC_DIG_SIGN_SUPPORTED
#include <driver/periph_ctrl.h>
#include <esp32c3/rom/digital_signature.h>
#include <esp32c3/rom/efuse.h>
#include <esp32c3/rom/hmac.h>
#include <esp_crypto_lock.h>
#include <nvs.h>

// hwprov writes these, KEY5 holds the eFuse half
static const char NVS_NS[] = "hwkey";
static const char NVS_KEY[] = "ds";
static const ets_efuse_block_t KEY_BLOCK = ETS_EFUSE_BLOCK_KEY5;
static const uint32_t RSA_2048 = 2048 / 32 - 1;   // rsa_length
static_assert(sizeof(ets_ds_data_t) == HwKey::BLOB, "blob size");

namespace {
struct Work {
  ets_ds_data_t data;
  uint8_t msg[HwKey::SIG];
  uint8_t sig[HwKey::SIG];
};

// ROM routines, not IDF's esp_ds (~1 KB of hal). The lock also keeps WiFi's mbedtls off SHA/AES/RSA,
// periph_module_* keeps its clock counts right
bool rom_sign(Work* w) {
  esp_crypto_ds_lock_acquire();
  periph_module_enable(PERIPH_HMAC_MODULE);
  periph_module_enable(PERIPH_SHA_MODULE);
  periph_module_enable(PERIPH_DS_MODULE);
  bool ok = ets_hmac_calculate_downstream(KEY_BLOCK, ETS_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE) == 0 &&
            ets_ds_start_sign(w->msg, &w->data) == ETS_DS_OK;
  if (ok) {
    while (ets_ds_is_busy()) {}
    ok = ets_ds_finish_sign(w->sig, &w->data) == ETS_DS_OK;
  }
  ets_hmac_invalidate_downstream(ETS_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE);
  periph_module_disable(PERIPH_DS_MODULE);
  periph_module_disable(PERIPH_SHA_MODULE);
  periph_module_disable(PERIPH_HMAC_MODULE);
  esp_crypto_ds_lock_release();
  return ok;
}

bool load(Work* w) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t n = sizeof(w->data);
  bool ok = nvs_get_blob(h, NVS_KEY, &w->data, &n) == ESP_OK && n == sizeof(w->data);
  nvs_close(h);
  return ok;
}

// another chip's or a damaged blob fails the DS digest check
bool run(Work* w, const uint8_t hash[HwKey::HASH]) {
  if (w->data.rsa_length != RSA_2048) return false;
  HwKey::pad(hash, w->msg);
  return rom_sign(w);
}
}  // namespace

bool HwKey::present() {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t n = 0;
  bool ok = nvs_get_blob(h, NVS_KEY, nullptr, &n) == ESP_OK && n == sizeof(ets_ds_data_t);
  nvs_close(h);
  return ok;
}

bool HwKey::burned() {
  return ets_efuse_get_key_purpose(KEY_BLOCK) == ETS_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE;
}

bool HwKey::sign(const uint8_t hash[HASH], uint8_t sig[SIG]) {
  Work* w = (Work*)malloc(sizeof(Work));
  if (!w) return false;
  bool ok = load(w) && run(w, hash);
  // DS gives little endian words
  if (ok) for (size_t i = 0; i < SIG; i++) sig[i] = w->sig[SIG - 1 - i];
  memset(w, 0, sizeof(Work));
  free(w);
  return ok;
}

// a wiped nvs gets the blob back from the api
bool HwKey::put(const uint8_t blob[BLOB]) {
  Work* w = (Work*)malloc(sizeof(Work));
  if (!w) return false;
  memcpy(&w->data, blob, BLOB);
  const uint8_t test[HASH] = {};
  bool ok = run(w, test);
  memset(w, 0, sizeof(Work));
  free(w);
  nvs_handle_t h;
  if (!ok || nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  ok = nvs_set_blob(h, NVS_KEY, blob, BLOB) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

#else
bool HwKey::present() { return false; }
bool HwKey::burned() { return false; }
bool HwKey::sign(const uint8_t*, uint8_t*) { return false; }
bool HwKey::put(const uint8_t*) { return false; }
#endif
