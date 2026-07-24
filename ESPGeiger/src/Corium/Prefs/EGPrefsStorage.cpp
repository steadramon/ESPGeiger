/*
  EGPrefsStorage.cpp - Per-module binary store. See EGPrefsStorage.h for format.

  Copyright (C) 2025 @steadramon

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

#include "EGPrefsStorage.h"
#include "../Logger/Logger.h"

#define EGPS_MAX_GROUP_BYTES  1024
#define EGPS_STAGING_SUFFIX   ".new"
#define EGPS_PATH_BUFSZ       96
#define EGPS_KEY_BUFSZ        64
#define EGPS_VAL_BUFSZ        512   // generous - stack is only used during boot readGroup

namespace {

// char[] version - avoids a String heap alloc per call.
void group_path(char* out, size_t sz, const char* base, const char* mid) {
  snprintf_P(out, sz, PSTR("%s%s.bin"), base, mid);
}

} // anonymous namespace

EGPrefsStorage::EGPrefsStorage()
  : _path(), _started(false), _readOnly(false)
{}

EGPrefsStorage::~EGPrefsStorage() {
  end();
}

void EGPrefsStorage::ensure_mounted() {
#ifdef ESP8266
  LittleFS.begin();
#else
  LittleFS.begin(true);
#endif
}

bool EGPrefsStorage::begin(const char* name, bool readOnly) {
  if (_started || !name || !*name) return false;
  _readOnly = readOnly;
  ensure_mounted();
  String dir = String("/") + name;
  if (!_readOnly) {
    if (LittleFS.exists(dir.c_str())) {
      File chk = LittleFS.open(dir.c_str(), "r");
      if (chk && !chk.isDirectory()) {
        chk.close();
        LittleFS.remove(dir.c_str());
        Log::console(PSTR("prefs: cleared rogue file at %s"), dir.c_str());
      } else if (chk) {
        chk.close();
      }
    }
    if (!LittleFS.exists(dir.c_str())) {
      LittleFS.mkdir(dir.c_str());
    }
  }
  _path = dir + "/";
  _started = true;
  Log::debug(PSTR("prefs: storage mounted at %s"), _path.c_str());
  return true;
}

void EGPrefsStorage::end() {
  if (!_started) return;
  LittleFS.end();
  _path = "";
  _started = false;
}

bool EGPrefsStorage::has(const char* module_id) {
  if (!_started || !module_id) return false;
  ensure_mounted();
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path.c_str(), module_id);
  return LittleFS.exists(path);
}

bool EGPrefsStorage::readGroup(const char* module_id, ReadCb cb, void* ctx) {
  if (!_started || !module_id || !cb) return false;
  ensure_mounted();
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path.c_str(), module_id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 4 || sz > EGPS_MAX_GROUP_BYTES) { f.close(); return false; }

  // Validate magic header.
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 ||
      magic[0] != 'E' || magic[1] != 'G' || magic[2] != 'P' || magic[3] != '1') {
    f.close();
    return false;
  }

  // Stream records directly from the File - avoids a transient ~1KB heap
  // allocation. LittleFS internally caches reads so small reads stay fast.
  char key[EGPS_KEY_BUFSZ];
  char val[EGPS_VAL_BUFSZ];
  bool ok = true;
  while (f.available() > 0) {
    uint8_t klen;
    if (f.read(&klen, 1) != 1) { ok = false; break; }
    if (klen == 0 || klen >= sizeof(key)) { ok = false; break; }
    if (f.read((uint8_t*)key, klen) != klen) { ok = false; break; }
    uint8_t vlen_bytes[2];
    if (f.read(vlen_bytes, 2) != 2) { ok = false; break; }
    uint16_t vlen = (uint16_t)vlen_bytes[0] | ((uint16_t)vlen_bytes[1] << 8);
    if (vlen >= sizeof(val)) { ok = false; break; }
    if (vlen > 0 && f.read((uint8_t*)val, vlen) != vlen) { ok = false; break; }
    cb(ctx, key, klen, val, vlen);
  }
  f.close();
  return ok;
}

bool EGPrefsStorage::writeGroup(const char* module_id, const void* buf, size_t len) {
  if (!_started || _readOnly || !module_id || !buf) return false;
  if (len < 4 || len > EGPS_MAX_GROUP_BYTES) return false;
  ensure_mounted();
  char final_path[EGPS_PATH_BUFSZ];
  group_path(final_path, sizeof(final_path), _path.c_str(), module_id);
  char staging[EGPS_PATH_BUFSZ];
  snprintf_P(staging, sizeof(staging), PSTR("%s%s"), final_path, EGPS_STAGING_SUFFIX);
  {
    File f = LittleFS.open(staging, "w");
    if (!f) return false;
    size_t n = f.write((const uint8_t*)buf, len);
    f.close();
    if (n != len) {
      LittleFS.remove(staging);
      return false;
    }
  }
  if (LittleFS.exists(final_path)) LittleFS.remove(final_path);
  if (!LittleFS.rename(staging, final_path)) {
    LittleFS.remove(staging);
    return false;
  }
  return true;
}

bool EGPrefsStorage::removeGroup(const char* module_id) {
  if (!_started || _readOnly || !module_id) return false;
  ensure_mounted();
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path.c_str(), module_id);
  return LittleFS.remove(path);
}
