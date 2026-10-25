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

#define FS_MOUNT_TRIES 3

static bool s_tried   = false;
static bool s_mounted = false;

bool Fs::begin() {
  if (s_tried) return s_mounted;
  s_tried = true;
#ifdef ESP8266
  LittleFS.setConfig(LittleFSConfig(false));   // no auto format
#endif
#ifdef ESP32
  esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
  esp_log_level_set("vfs_api",      ESP_LOG_ERROR);
  esp_log_level_set("LITTLEFS",     ESP_LOG_ERROR);
#endif
  for (uint8_t i = 0; i < FS_MOUNT_TRIES && !s_mounted; i++) {
    if (i) delay(50);
    s_mounted = LittleFS.begin();
  }
  if (!s_mounted) {
    Log::console(PSTR("FS: mount failed, formatting"));
    if (LittleFS.format()) s_mounted = LittleFS.begin();
  }
  return s_mounted;
}

File Fs::open_r(const char* path) {
  if (!begin() || !LittleFS.exists(path)) return File();
  return LittleFS.open(path, "r");
}

File Fs::open_w(const char* path) {
  if (!begin()) return File();
  return LittleFS.open(path, "w");
}

bool Fs::remove(const char* path) {
  return begin() && LittleFS.exists(path) && LittleFS.remove(path);
}

bool Fs::exists(const char* path) {
  return begin() && LittleFS.exists(path);
}

bool Fs::rename(const char* from, const char* to) {
  return begin() && LittleFS.exists(from) && LittleFS.rename(from, to);
}

bool Fs::mkdir(const char* path) {
  return begin() && LittleFS.mkdir(path);
}

void Fs::touch(const char* path) {
  File f = open_w(path);
  if (f) f.close();
}

// on the stack for the call, "" if too long
struct RamPath {
  char s[Fs::PATH_P_MAX + 1];
  explicit RamPath(const __FlashStringHelper* p) {
    s[0] = '\0';
    size_t n = strlen_P((PGM_P)p);
    if (n <= Fs::PATH_P_MAX) memcpy_P(s, p, n + 1);
  }
};

File Fs::open_r(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] ? open_r(p.s) : File();
}

File Fs::open_w(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] ? open_w(p.s) : File();
}

bool Fs::remove(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && remove(p.s);
}

bool Fs::exists(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && exists(p.s);
}

bool Fs::rename(const __FlashStringHelper* from, const __FlashStringHelper* to) {
  RamPath a(from), b(to);
  return a.s[0] && b.s[0] && rename(a.s, b.s);
}
