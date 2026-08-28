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

#define FS_MOUNT_TRIES 3

static bool s_tried   = false;
static bool s_mounted = false;

bool Fs::begin() {
  if (s_tried) return s_mounted;
  s_tried = true;
#ifdef ESP8266
  LittleFS.setConfig(LittleFSConfig(false));   // no auto format
#endif
  for (uint8_t i = 0; i < FS_MOUNT_TRIES && !s_mounted; i++) {
    if (i) delay(50);
    s_mounted = LittleFS.begin();
  }
  if (!s_mounted && LittleFS.format()) s_mounted = LittleFS.begin();
  return s_mounted;
}

File Fs::open_r(const char* path) {
  if (!begin() || !LittleFS.exists(path)) return File();
  return LittleFS.open(path, "r");
}

bool Fs::remove(const char* path) {
  return begin() && LittleFS.exists(path) && LittleFS.remove(path);
}

void Fs::touch(const char* path) {
  if (!begin()) return;
  File f = LittleFS.open(path, "w");
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
  if (!p.s[0] || !begin()) return File();
  return LittleFS.open(p.s, "w");
}

bool Fs::remove(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && remove(p.s);
}

bool Fs::exists(const __FlashStringHelper* path) {
  RamPath p(path);
  return p.s[0] && begin() && LittleFS.exists(p.s);
}

bool Fs::rename(const __FlashStringHelper* from, const __FlashStringHelper* to) {
  RamPath a(from), b(to);
  return a.s[0] && b.s[0] && begin() && LittleFS.exists(a.s) && LittleFS.rename(a.s, b.s);
}
