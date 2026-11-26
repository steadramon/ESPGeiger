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
#include "Corium/Util/Bytes.h"
#ifdef ESP32
#include "esp_log.h"
#include "Layout.h"
#endif

#define FS_MOUNT_TRIES 6

static bool s_tried   = false;
static bool s_mounted = false;

// Erases from the writes done here. Only an estimate as LittleFS can't say, counts a file's blocks plus its directory
static uint32_t s_wear_base  = 0;   // /wear at mount
static uint32_t s_wear_boot  = 0;
static uint32_t s_wear_saved = 0;
static bool     s_wear_busy  = false;
static constexpr uint32_t WEAR_CHECK = 0x57454152;
static constexpr uint32_t WEAR_STEP  = 32;      // saved every 32 so a crash or power cut loses less
static const char WEAR_PATH[] PROGMEM = "/wear";

static uint32_t fs_block() {
#ifdef ESP8266
  return FS_PHYS_BLOCK;
#else
  return 4096;
#endif
}

static void wear_save() {
  uint32_t total = s_wear_base + s_wear_boot + 2;   // plus this save's own write, a block and the directory
  uint8_t b[8];
  wrLe32(b, total);
  wrLe32(b + 4, total ^ WEAR_CHECK);
  s_wear_busy = true;
  (void)corium::Fs::writeWhole(FPSTR(WEAR_PATH), b, sizeof(b));
  s_wear_busy = false;
  s_wear_saved = total;   // A failed save waits for the next step, it never retries on every write
}

static void wear_add(uint32_t n) {
  s_wear_boot += n;
  if (!s_wear_busy && s_wear_base + s_wear_boot - s_wear_saved >= WEAR_STEP) wear_save();
}

static void wear_load() {
  File f = corium::Fs::openR(FPSTR(WEAR_PATH));
  if (!f) return;
  uint8_t b[8];
  bool ok = f.read(b, sizeof(b)) == sizeof(b) && rdLe32(b + 4) == (rdLe32(b) ^ WEAR_CHECK);
  f.close();
  if (ok) s_wear_base = s_wear_saved = rdLe32(b);
  else corium::Log::console(PSTR("FS: Wear count damaged, from 0"));
}

bool corium::Fs::begin() {
  if (s_tried) return s_mounted;
  s_tried = true;
#ifdef ESP8266
  LittleFS.setConfig(LittleFSConfig(false));   // no auto format
#endif
#ifdef ESP32
  // Errors only, else every missing file gets logged
  esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
  esp_log_level_set("vfs_api",      ESP_LOG_ERROR);
  esp_log_level_set("LITTLEFS",     ESP_LOG_ERROR);
#endif
  for (uint8_t i = 0; i < FS_MOUNT_TRIES && !s_mounted; i++) {
    if (i) delay(50 << (i - 1));   // 50 ms doubling, ~1.5 s before a format
    s_mounted = LittleFS.begin();
  }
  if (!s_mounted) {
#ifdef ESP32
    corium::Layout::rescue();
#endif
    corium::Log::console(PSTR("FS: Mount failed, formatting"));
    if (LittleFS.format()) s_mounted = LittleFS.begin();
  }
  if (s_mounted) wear_load();
  return s_mounted;
}

void corium::Fs::end() {
  if (s_mounted) LittleFS.end();
  s_tried = true;
  s_mounted = false;
}

#ifndef ESP8266
// the wear count comes back with the next mount
void corium::Fs::unmount() {
  if (s_mounted) LittleFS.end();
  s_tried = false;
  s_mounted = false;
  s_wear_base = s_wear_boot = s_wear_saved = 0;
}

// esp32: format() asserts on the label only begin() sets, mounted or not
bool corium::Fs::format() {
  unmount();
  (void)LittleFS.begin();
  LittleFS.end();
  return LittleFS.format();
}
#endif

File corium::Fs::openR(const char* path) {
  if (!begin() || !LittleFS.exists(path)) return File();
  return LittleFS.open(path, "r");
}

File corium::Fs::openW(const char* path) {
  if (!begin()) return File();
  return LittleFS.open(path, "w");
}

// No mount returns false, the file may still be there
bool corium::Fs::remove(const char* path) {
  if (!begin()) return false;
  if (!LittleFS.exists(path)) return true;
  if (!LittleFS.remove(path)) return false;
  wear_add(1);
  return true;
}

bool corium::Fs::exists(const char* path) {
  return begin() && LittleFS.exists(path);
}

bool corium::Fs::rename(const char* from, const char* to) {
  bool ok = begin() && LittleFS.exists(from) && LittleFS.rename(from, to);
  if (ok) wear_add(1);
  return ok;
}

bool corium::Fs::mkdir(const char* path) {
  bool ok = begin() && LittleFS.mkdir(path);
  if (ok) wear_add(1);
  return ok;
}

bool corium::Fs::usage(uint32_t* used, uint32_t* total) {
  if (!begin()) return false;
#if defined(ESP8266)
  FSInfo i;
  if (!LittleFS.info(i)) return false;
  *used = i.usedBytes;
  *total = i.totalBytes;
#elif defined(ESP32)
  *used = LittleFS.usedBytes();
  *total = LittleFS.totalBytes();
#else
  return false;
#endif
  return *total > 0;
}

void corium::Fs::touch(const char* path) {
  File f = openW(path);
  if (!f) return;
  f.close();
  wear_add(1);
}

// close() returns nothing and its flush can still fail, so the size is read back
bool corium::Fs::writeWhole(const char* path, const void* buf, size_t len) {
  File f = openW(path);
  if (!f) return false;
  size_t n = f.write((const uint8_t*)buf, len);
  f.close();
  File r = openR(path);
  bool ok = n == len && r && r.size() == len;
  r.close();
  if (!ok) (void)remove(path);   // already returning false
  wear_add((uint32_t)((len + fs_block() - 1) / fs_block()) + 1);
  return ok;
}

uint32_t corium::Fs::wear()     { return s_wear_base + s_wear_boot; }
uint32_t corium::Fs::wearBoot() { return s_wear_boot; }

void corium::Fs::saveWear() {
  if (s_mounted && wear() != s_wear_saved) wear_save();
}

bool corium::Fs::wearPct100(uint32_t* x100) {
  if (!begin()) return false;
#if defined(ESP8266)
  uint32_t blocks = FS_PHYS_SIZE / fs_block();
#elif defined(ESP32)
  uint32_t blocks = LittleFS.totalBytes() / fs_block();
#else
  uint32_t blocks = 0;   // a host has no flash to wear
#endif
  if (!blocks) return false;
  // Rated 10k erases a block at the low end, so hundredths of a % = erases / blocks
  *x100 = wear() / blocks;
  return true;
}

// RAM copy on the stack for the call, "" if too long
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
