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
#include "EGPrefs.h"
#include "../Logger/Logger.h"
#include "../System/Fs.h"

#define EGPS_STAGING_SUFFIX   ".new"
#define EGPS_PATH_BUFSZ       96
#define EGPS_KEY_BUFSZ        64
#define EGPS_VAL_BUFSZ        512   // stack, boot only

namespace {

void group_path(char* out, size_t sz, const char* base, const char* mid) {
  snprintf_P(out, sz, PSTR("%s%s.bin"), base, mid);
}

bool read_magic(File& f) {
  uint8_t magic[4];
  return f.read(magic, 4) == 4 &&
         magic[0] == 'E' && magic[1] == 'G' && magic[2] == 'P' && magic[3] == '1';
}

// u8 klen, key, u16le vlen, value
bool read_record(File& f, char* key, uint8_t* klen, char* value, uint16_t* vlen) {
  if (f.read(klen, 1) != 1) return false;
  if (*klen == 0 || *klen >= EGPS_KEY_BUFSZ) return false;
  if (f.read((uint8_t*)key, *klen) != *klen) return false;
  uint8_t vlen_bytes[2];
  if (f.read(vlen_bytes, 2) != 2) return false;
  *vlen = (uint16_t)vlen_bytes[0] | ((uint16_t)vlen_bytes[1] << 8);
  if (*vlen >= EGPS_VAL_BUFSZ) return false;
  if (*vlen > 0 && f.read((uint8_t*)value, *vlen) != *vlen) return false;
  return true;
}

} // anonymous namespace

EGPrefsStorage::EGPrefsStorage()
  : _path{}, _started(false), _readOnly(false)
{}

EGPrefsStorage::~EGPrefsStorage() {
  end();
}

bool EGPrefsStorage::begin(const char* name, bool readOnly) {
  if (_started || !name || !*name || strlen(name) > DIR_NAME_MAX) return false;
  if (!Fs::begin()) return false;   // prefs run on defaults
  _readOnly = readOnly;
  char dir[DIR_NAME_MAX + 2];
  snprintf_P(dir, sizeof(dir), PSTR("/%s"), name);
  if (!_readOnly) {
    if (LittleFS.exists(dir)) {
      File chk = LittleFS.open(dir, "r");
      if (chk && !chk.isDirectory()) {
        chk.close();
        LittleFS.remove(dir);
        Log::console(PSTR("prefs: cleared rogue file at %s"), dir);
      } else if (chk) {
        chk.close();
      }
    }
    if (!LittleFS.exists(dir)) {
      LittleFS.mkdir(dir);
    }
  }
  snprintf_P(_path, sizeof(_path), PSTR("%s/"), dir);
  _started = true;
  Log::debug(PSTR("prefs: storage mounted at %s"), _path);
  return true;
}

void EGPrefsStorage::end() {
  if (!_started) return;
  _path[0] = '\0';
  _started = false;
}

bool EGPrefsStorage::has(const char* module_id) {
  if (!_started || !module_id) return false;
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path, module_id);
  return LittleFS.exists(path);
}

bool EGPrefsStorage::readGroup(const char* module_id, ReadCb cb, void* ctx) {
  if (!_started || !module_id || !cb) return false;
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path, module_id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 4 || sz > EGPREFS_MAX_GROUP_SZ) { f.close(); return false; }

  if (!read_magic(f)) {
    f.close();
    return false;
  }

  // stream it, no ~1 KB heap
  char key[EGPS_KEY_BUFSZ];
  char value[EGPS_VAL_BUFSZ];
  bool ok = true;
  while (f.available() > 0) {
    uint8_t klen;
    uint16_t vlen;
    if (!read_record(f, key, &klen, value, &vlen)) { ok = false; break; }
    cb(ctx, key, klen, value, vlen);
  }
  f.close();
  return ok;
}

// stage and rename, never a partial write
bool EGPrefsStorage::writeGroup(const char* module_id, const void* buf, size_t len) {
  if (!_started || _readOnly || !module_id || !buf) return false;
  if (len < 4 || len > EGPREFS_MAX_GROUP_SZ) return false;
  char final_path[EGPS_PATH_BUFSZ];
  group_path(final_path, sizeof(final_path), _path, module_id);
  char staging[EGPS_PATH_BUFSZ];
  int sn = snprintf_P(staging, sizeof(staging), PSTR("%s%s"), final_path, EGPS_STAGING_SUFFIX);
  if (sn < 0 || sn >= (int)sizeof(staging)) return false;
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
  // don't remove first, rename replaces in one step
  if (!LittleFS.rename(staging, final_path)) {
    LittleFS.remove(staging);
    return false;
  }
  return true;
}

bool EGPrefsStorage::removeGroup(const char* module_id) {
  if (!_started || _readOnly || !module_id) return false;
  char path[EGPS_PATH_BUFSZ];
  group_path(path, sizeof(path), _path, module_id);
  return LittleFS.remove(path);
}
