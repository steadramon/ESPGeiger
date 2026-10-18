/*
  test_prefsmove - moved keys on a build without the OLED: env hands over.

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

// begin() once per process: env stored, no display group, i2c new

#include <unity.h>
#include <string.h>
#include <initializer_list>
#include <string>

#include <LittleFS.h>
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Logger/Logger.h"

static int s_logs = 0;
void Log::console(const char*, ...) { s_logs++; }
void Log::debug(const char*, ...) {}

#define ROW(id, def, mn, mx) {id, "", "", def, nullptr, mn, mx, 0, EGP_UINT, 0}
#define GROUP(id, rows) {id, "", 1, rows, sizeof(rows) / sizeof(rows[0]), 0, nullptr}

static const EGPref I2C_PREFS[] PROGMEM = {
  ROW("sda", "21", 0, 48),
  ROW("scl", "22", 0, 48),
};
static const EGPref ENV_PREFS[] PROGMEM = {
  ROW("unit", "0", 0, 2),
};
static const EGPrefGroup G_I2C PROGMEM = GROUP("i2c", I2C_PREFS);
static const EGPrefGroup G_ENV PROGMEM = GROUP("env", ENV_PREFS);

struct TestModule : EGModule {
  const EGPrefGroup* group;
  explicit TestModule(const EGPrefGroup* g) : group(g) {}
  const char* name() override { return group->module_id; }
  const EGPrefGroup* prefs_group() override { return group; }
};

static TestModule m_i2c(&G_I2C), m_env(&G_ENV);
static TestModule* const MODULES[] = { &m_i2c, &m_env };

uint8_t EGModuleRegistry::count() { return 2; }
EGModule* EGModuleRegistry::get(uint8_t i) { return i < 2 ? MODULES[i] : nullptr; }
bool EGModuleRegistry::quarantined(EGModule*) { return false; }

static std::string rec(const char* key, const std::string& val) {
  std::string r(1, (char)strlen(key));
  r += key;
  r += (char)(val.size() & 0xFF);
  r += (char)(val.size() >> 8);
  return r + val;
}

static std::string egp1(std::initializer_list<std::string> recs) {
  std::string s = "EGP1";
  for (const std::string& r : recs) s += r;
  return s;
}

void setUp(void) {}
void tearDown(void) {}

static void from_env(void) {
  TEST_ASSERT_EQUAL_UINT32(34, EGPrefs::getUInt("i2c", "scl"));
  TEST_ASSERT_EQUAL_UINT32(1, EGPrefs::getUInt("env", "unit"));
  TEST_ASSERT_TRUE(LittleFS.exists("/prefs/i2c.bin"));
}

// a bad stored pin isn't carried, the default stays
static void validated(void) {
  TEST_ASSERT_EQUAL_UINT32(21, EGPrefs::getUInt("i2c", "sda"));
}

// no display group here: a forwarded key still lands
static void forward(void) {
  TEST_ASSERT_TRUE(EGPrefs::put("display", "sda", "17"));
  TEST_ASSERT_EQUAL_UINT32(17, EGPrefs::getUInt("i2c", "sda"));
}

// serial get/set gate on find_pref
static void forward_read(void) {
  EGPref p;
  TEST_ASSERT_TRUE(EGPrefs::find_pref("display", "sda", &p));
  TEST_ASSERT_EQUAL_STRING("sda", p.id);
  TEST_ASSERT_TRUE(EGPrefs::find_pref("env", "scl", nullptr));
  TEST_ASSERT_TRUE(EGPrefs::put("i2c", "scl", "5"));
  TEST_ASSERT_EQUAL_UINT32(5, EGPrefs::getUInt("env", "scl"));
  TEST_ASSERT_EQUAL_UINT32(5, EGPrefs::getUInt("display", "scl"));
  TEST_ASSERT_EQUAL_UINT32(1, EGPrefs::getUInt("env", "unit"));
  TEST_ASSERT_FALSE(EGPrefs::find_pref("display", "nope", nullptr));
  TEST_ASSERT_FALSE(EGPrefs::find_pref("i2c", "unit", nullptr));
  TEST_ASSERT_EQUAL_STRING("", EGPrefs::getString("i2c", "unit"));
}

int main(int, char**) {
  LittleFS.files["/prefs/env.bin"] =
      egp1({ rec("sda", "99"), rec("scl", "34"), rec("unit", "1") });
  EGPrefs::begin();

  UNITY_BEGIN();
  RUN_TEST(from_env);
  RUN_TEST(validated);
  RUN_TEST(forward);
  RUN_TEST(forward_read);
  return UNITY_END();
}
