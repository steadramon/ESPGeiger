/*
  Fs.cpp - LittleFS, mounted once and left mounted

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
#include "Fs.h"
#include "Corium/Logger/Logger.h"
#ifdef ESP32
#include "esp_log.h"
#endif

#define FS_MOUNT_TRIES 6

static bool s_tried   = false;
static bool s_mounted = false;

bool corium::Fs::begin() {
  if (s_tried) return s_mounted;
  s_tried = true;
#ifdef ESP8266
  LittleFS.setConfig(LittleFSConfig(false));   // no auto format
#endif
#ifdef ESP32
  // errors only, else a missing file logs
  esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
  esp_log_level_set("vfs_api",      ESP_LOG_ERROR);
  esp_log_level_set("LITTLEFS",     ESP_LOG_ERROR);
#endif
  for (uint8_t i = 0; i < FS_MOUNT_TRIES && !s_mounted; i++) {
    if (i) delay(50 << (i - 1));   // 50 ms doubling, ~1.5 s before a format
    s_mounted = LittleFS.begin();
  }
  if (!s_mounted) {
    corium::Log::console(PSTR("FS: mount failed, formatting"));
    if (LittleFS.format()) s_mounted = LittleFS.begin();
  }
  return s_mounted;
}

void corium::Fs::end() {
  if (s_mounted) LittleFS.end();
  s_tried = true;
  s_mounted = false;
}

File corium::Fs::openR(const char* path) {
  if (!begin() || !LittleFS.exists(path)) return File();
  return LittleFS.open(path, "r");
}

File corium::Fs::openW(const char* path) {
  if (!begin()) return File();
  return LittleFS.open(path, "w");
}

// no mount = may still be there
bool corium::Fs::remove(const char* path) {
  return begin() && (!LittleFS.exists(path) || LittleFS.remove(path));
}

bool corium::Fs::exists(const char* path) {
  return begin() && LittleFS.exists(path);
}

bool corium::Fs::rename(const char* from, const char* to) {
  return begin() && LittleFS.exists(from) && LittleFS.rename(from, to);
}

bool corium::Fs::mkdir(const char* path) {
  return begin() && LittleFS.mkdir(path);
}

void corium::Fs::touch(const char* path) {
  File f = openW(path);
  if (f) f.close();
}

// close() says nothing, its flush can still fail
bool corium::Fs::writeWhole(const char* path, const void* buf, size_t len) {
  File f = openW(path);
  if (!f) return false;
  size_t n = f.write((const uint8_t*)buf, len);
  f.close();
  File r = openR(path);
  bool ok = n == len && r && r.size() == len;
  r.close();
  if (!ok) (void)remove(path);   // false said already
  return ok;
}

// on the stack for the call, "" if too long
struct RamPath {
  char s[corium::Fs::PATH_P_MAX + 1];
  explicit RamPath(const __FlashStringHelper* p) {
    s[0] = '\0';
    size_t n = strlen_P((PGM_P)p);
    if (n <= corium::Fs::PATH_P_MAX) memcpy_P(s, p, n + 1);
  }
};

File corium::Fs::openR(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] ? openR(p.s) : File();
}

File corium::Fs::openW(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] ? openW(p.s) : File();
}

bool corium::Fs::remove(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && remove(p.s);
}

bool corium::Fs::exists(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && exists(p.s);
}

bool corium::Fs::rename(const __FlashStringHelper* from, const __FlashStringHelper* to) {
  RamPath a(from), b(to);
  return a.s[0] && b.s[0] && rename(a.s, b.s);
}

bool corium::Fs::writeWhole(const __FlashStringHelper* path, const void* buf, size_t len) {
  RamPath p(path);
  return p.s[0] && writeWhole(p.s, buf, len);
}
