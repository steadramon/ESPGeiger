/*
  fakes.cpp - what the uploaders call outside their own file.

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

#include "up_fakes.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/TickProfile.h"
#include "GeigerInput/SerialFormat.h"

FakeReading reading = {};
FakeMore more = {};
bool test_mode = false;
Counter gcounter;
FakeEnv env = {};
EnvSensor envsensor;
int16_t Wifi::rssi = -61;

const char EG_K_INTERVAL[] = "interval";
const char EG_K_PASSWORD[] = "password";
const char EG_K_SEND[]     = "send";
const char EG_K_USER[]     = "user";
const char EG_L_EN[]       = "Enable";
const char EG_L_INT[]      = "Interval";
const char EG_L_PWD[]      = "Password";

const char* DeviceInfo::chipid() { return "c0ffee"; }
const char* DeviceInfo::hostname() { return "ESPGeiger-c0ffee"; }
const char* DeviceInfo::friendlyName() { return ""; }
unsigned long DeviceInfo::uptime() { return 3600; }
uint32_t DeviceInfo::freeHeap() { return 24000; }
uint8_t  DeviceInfo::heapFrag() { return 0; }
uint32_t DeviceInfo::largestFreeBlock() { return 0; }
uint32_t DeviceInfo::largestFreeBlockLow() { return 0; }

namespace TickProfile {
  uint32_t tick_us, tick_max_us, lps, lps_raw;
}
const char* SerialFormat::mohm_mode(int) { return "SLOW"; }

void EGHttpPoster::loop(unsigned long) {}
void EGHttpPoster::set_enabled(bool on) { _send_enabled = on; }

char last_log[256];

// as Net/EGHttpPoster.cpp
bool EGHttpPoster::url_fits(int n, size_t cap) {
  if (n >= 0 && (size_t)n < cap) return true;
  Log::console(PSTR("%s: URL too long, not sent"), log_tag());
  return false;
}
void Log::console(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(last_log, sizeof(last_log), fmt, ap);
  va_end(ap);
}
void Log::debug(const char*, ...) {}
bool EGModuleRegistry::add(EGModule*) { return true; }
uint8_t EGModuleRegistry::count() { return 0; }
EGModule* EGModuleRegistry::get(uint8_t) { return nullptr; }
bool EGModuleRegistry::quarantined(EGModule*) { return false; }

namespace {
struct Row { char group[16]; char key[16]; char value[256]; };
Row rows[8];
uint8_t n_rows = 0;

const Row* find(const char* group, const char* key) {
  for (uint8_t i = 0; i < n_rows; i++) {
    if (strcmp(rows[i].group, group) == 0 && strcmp(rows[i].key, key) == 0) return &rows[i];
  }
  return nullptr;
}
}

namespace FakePrefs {
  void set(const char* group, const char* key, const char* value) {
    Row& r = rows[n_rows++];
    strncpy(r.group, group, sizeof(r.group) - 1);
    strncpy(r.key, key, sizeof(r.key) - 1);
    strncpy(r.value, value, sizeof(r.value) - 1);
  }
  void reset() {
    memset(rows, 0, sizeof(rows));
    n_rows = 0;
  }
}

const char* EGPrefs::getString(const char* group, const char* key) {
  const Row* r = find(group, key);
  return r ? r->value : "";
}
uint32_t EGPrefs::getUInt(const char* group, const char* key) {
  return (uint32_t)strtoul(getString(group, key), nullptr, 10);
}
bool EGPrefs::getBool(const char* group, const char* key) {
  return getUInt(group, key) != 0;
}
