/*
  PrefsStorage.cpp - prefs files on LittleFS, one per module

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

#include "PrefsStorage.h"
#include "Prefs.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/Fs.h"
#include "Corium/Util/Bytes.h"

#define CORIUM_PS_STAGING_SUFFIX   ".new"
#define CORIUM_PS_PATH_BUFSZ       96
// a longer key or value = a bad record, the rest not read
#define CORIUM_PS_KEY_BUFSZ        64
#define CORIUM_PS_VAL_BUFSZ        512   // stack, boot only

namespace {

void group_path(char* out, size_t sz, const char* base, const char* id) {
  snprintf_P(out, sz, PSTR("%s%s.bin"), base, id);
}

bool staged_path(char* out, size_t sz, const char* base, const char* id) {
  int n = snprintf_P(out, sz, PSTR("%s%s.bin" CORIUM_PS_STAGING_SUFFIX), base, id);
  return n > 0 && n < (int)sz;
}

bool read_magic(File& f) {
  uint8_t magic[corium::PREFS_MAGIC_LEN];
  return f.read(magic, sizeof(magic)) == sizeof(magic) && corium::isMagic(magic);
}

// u8 klen, key, u16le vlen, value
bool read_record(File& f, char* key, uint8_t* klen, char* value, uint16_t* vlen) {
  if (f.read(klen, 1) != 1) return false;
  if (*klen == 0 || *klen >= CORIUM_PS_KEY_BUFSZ) return false;
  if (f.read((uint8_t*)key, *klen) != *klen) return false;
  uint8_t vlen_bytes[2];
  if (f.read(vlen_bytes, 2) != 2) return false;
  *vlen = rdLe16(vlen_bytes);
  if (*vlen >= CORIUM_PS_VAL_BUFSZ) return false;
  if (*vlen > 0 && f.read((uint8_t*)value, *vlen) != *vlen) return false;
  return true;
}

} // anonymous namespace

corium::PrefsStorage::PrefsStorage()
  : _dir{}, _started(false)
{}

corium::PrefsStorage::~PrefsStorage() {
  end();
}

bool corium::PrefsStorage::begin(const char* name) {
  if (_started || !name || !*name || strlen(name) > DIR_NAME_MAX) return false;
  if (!corium::Fs::begin()) return false;   // prefs run on defaults
  char dir[DIR_NAME_MAX + 2];
  snprintf_P(dir, sizeof(dir), PSTR("/%s"), name);
  // a file where the dir goes would stop every write
  File chk = corium::Fs::openR(dir);
  bool is_dir = chk && chk.isDirectory();
  if (chk && !is_dir) {
    chk.close();
    if (corium::Fs::remove(dir)) corium::Log::console(PSTR("Prefs: Cleared rogue file at %s"), dir);
  }
  // one that stayed fails here too
  if (!is_dir && !corium::Fs::mkdir(dir)) return false;
  snprintf_P(_dir, sizeof(_dir), PSTR("%s/"), dir);
  _started = true;
  corium::Log::debug(PSTR("prefs: storage mounted at %s"), _dir);
  return true;
}

void corium::PrefsStorage::end() {
  if (!_started) return;
  _dir[0] = '\0';
  _started = false;
}

bool corium::PrefsStorage::has(const char* module_id) {
  if (!_started || !module_id) return false;
  char path[CORIUM_PS_PATH_BUFSZ];
  group_path(path, sizeof(path), _dir, module_id);
  return corium::Fs::exists(path);
}

bool corium::PrefsStorage::readGroup(const char* module_id, ReadCb cb, void* ctx) {
  if (!_started || !module_id || !cb) return false;
  char path[CORIUM_PS_PATH_BUFSZ];
  group_path(path, sizeof(path), _dir, module_id);
  File f = corium::Fs::openR(path);
  if (!f) return false;
  size_t sz = f.size();
  if (sz < corium::PREFS_MAGIC_LEN || sz > corium::PREFS_FILE_MAX) { f.close(); return false; }

  if (!read_magic(f)) {
    f.close();
    return false;
  }

  // stream it, no ~1 KB heap
  char key[CORIUM_PS_KEY_BUFSZ];
  char value[CORIUM_PS_VAL_BUFSZ];
  bool ok = true;
  while (f.available() > 0) {
    uint8_t klen;
    uint16_t vlen;
    if (!read_record(f, key, &klen, value, &vlen)) {
      cb(ctx, nullptr, 0, nullptr, 0);
      ok = false;
      break;
    }
    cb(ctx, key, klen, value, vlen);
  }
  f.close();
  return ok;
}

// stage and rename, never a partial write
bool corium::PrefsStorage::writeGroup(const char* module_id, const void* buf, size_t len) {
  if (!stageGroup(module_id, buf, len)) return false;
  if (commitGroup(module_id)) return true;
  dropStaged(module_id);
  return false;
}

// <id>.bin.new, the old file stands till commitGroup
bool corium::PrefsStorage::stageGroup(const char* module_id, const void* buf, size_t len) {
  if (!_started || !module_id || !buf) return false;
  if (len < corium::PREFS_MAGIC_LEN || len > corium::PREFS_FILE_MAX) return false;
  char staging[CORIUM_PS_PATH_BUFSZ];
  if (!staged_path(staging, sizeof(staging), _dir, module_id)) return false;
  return corium::Fs::writeWhole(staging, buf, len);
}

// don't remove first, rename replaces in one step
bool corium::PrefsStorage::commitGroup(const char* module_id) {
  if (!_started || !module_id) return false;
  char staging[CORIUM_PS_PATH_BUFSZ];
  char path[CORIUM_PS_PATH_BUFSZ];
  if (!staged_path(staging, sizeof(staging), _dir, module_id)) return false;
  group_path(path, sizeof(path), _dir, module_id);
  return corium::Fs::rename(staging, path);
}

void corium::PrefsStorage::dropStaged(const char* module_id) {
  char staging[CORIUM_PS_PATH_BUFSZ];
  if (_started && module_id && staged_path(staging, sizeof(staging), _dir, module_id)) {
    (void)corium::Fs::remove(staging);   // the next stage truncates it
  }
}

bool corium::PrefsStorage::removeGroup(const char* module_id) {
  if (!_started || !module_id) return false;
  char path[CORIUM_PS_PATH_BUFSZ];
  group_path(path, sizeof(path), _dir, module_id);
  return corium::Fs::remove(path);
}
