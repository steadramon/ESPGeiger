/*
  test_prefsplit - sys and input hand their counter keys to counter.

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

// begin() once per process: sys, input, display and env stored, counter and i2c new

#include <unity.h>
#include <string.h>
#include <initializer_list>
#include <string>

#include <LittleFS.h>
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Logger/Logger.h"
#include "Corium/WebPortal/ImportText.h"

void Log::console(const char*, ...) {}
void Log::debug(const char*, ...) {}

#define ROW(id, def, mn, mx) {id, "", "", def, nullptr, mn, mx, 0, EGP_UINT, 0}
#define TEXT(id) {id, "", "", "", nullptr, 0, 0, 32, EGP_STRING, 0}
#define GROUP(id, rows) {id, "", 1, rows, sizeof(rows) / sizeof(rows[0]), 0, nullptr}

static const EGPref SYS_PREFS[] PROGMEM = {
  TEXT("name"),
  TEXT("web_pass"),
};
static const EGPref COUNTER_PREFS[] PROGMEM = {
  ROW("rx_pin", "4", 0, 48),
  ROW("cpm_mode", "3", 0, 4),
  ROW("ratio", "151", 1, 100000),
  ROW("warn", "50", 0, 9999),
  ROW("alert", "100", 0, 9999),
};
static const EGPref I2C_PREFS[] PROGMEM = {
  ROW("sda", "21", 0, 48),
  ROW("scl", "22", 0, 48),
};
static const EGPrefGroup G_SYS PROGMEM = GROUP("sys", SYS_PREFS);
static const EGPrefGroup G_COUNTER PROGMEM = GROUP("counter", COUNTER_PREFS);
static const EGPrefGroup G_I2C PROGMEM = GROUP("i2c", I2C_PREFS);

struct TestModule : EGModule {
  const EGPrefGroup* group;
  explicit TestModule(const EGPrefGroup* g) : group(g) {}
  const char* name() override { return group->module_id; }
  const EGPrefGroup* prefs_group() override { return group; }
};

static const EGLegacyAlias COUNTER_LEGACY[] PROGMEM = {
  {"geigerRatio", "ratio"},
  {},
};

struct CounterModule : TestModule {
  CounterModule() : TestModule(&G_COUNTER) {}
  EGLegacyRows legacy_aliases() override { return COUNTER_LEGACY; }
};

static TestModule m_sys(&G_SYS), m_i2c(&G_I2C);
static CounterModule m_counter;
static TestModule* const MODULES[] = { &m_sys, &m_counter, &m_i2c };

uint8_t EGModuleRegistry::count() { return 3; }
EGModule* EGModuleRegistry::get(uint8_t i) { return i < 3 ? MODULES[i] : nullptr; }
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

// both old groups land in one file, over a stale legacy one
static void merged(void) {
  TEST_ASSERT_EQUAL_UINT32(14, EGPrefs::getUInt("counter", "rx_pin"));
  TEST_ASSERT_EQUAL_UINT32(2, EGPrefs::getUInt("counter", "cpm_mode"));
  TEST_ASSERT_EQUAL_UINT32(175, EGPrefs::getUInt("counter", "ratio"));
  TEST_ASSERT_EQUAL_UINT32(80, EGPrefs::getUInt("counter", "warn"));
  TEST_ASSERT_EQUAL_UINT32(100, EGPrefs::getUInt("counter", "alert"));
  TEST_ASSERT_TRUE(LittleFS.exists("/prefs/counter.bin"));
}

static void sys_keeps(void) {
  TEST_ASSERT_EQUAL_STRING("dev", EGPrefs::getString("sys", "name"));
  TEST_ASSERT_EQUAL_STRING("pw", EGPrefs::getString("sys", "web_pass"));
  TEST_ASSERT_EQUAL_STRING("", EGPrefs::getString("counter", "name"));
}

// first listed wins a key both hold
static void first_wins(void) {
  TEST_ASSERT_EQUAL_UINT32(17, EGPrefs::getUInt("i2c", "sda"));
  TEST_ASSERT_EQUAL_UINT32(34, EGPrefs::getUInt("i2c", "scl"));
}

// old exports, serial set and saved forms
static void old_names(void) {
  TEST_ASSERT_EQUAL_UINT32(80, EGPrefs::getUInt("sys", "warn"));
  TEST_ASSERT_TRUE(EGPrefs::put("sys", "ratio", "200"));
  TEST_ASSERT_EQUAL_UINT32(200, EGPrefs::getUInt("counter", "ratio"));
  TEST_ASSERT_TRUE(EGPrefs::put("input", "rx_pin", "5"));
  TEST_ASSERT_EQUAL_UINT32(5, EGPrefs::getUInt("counter", "rx_pin"));
  TEST_ASSERT_TRUE(EGPrefs::find_pref("input", "cpm_mode", nullptr));
  TEST_ASSERT_TRUE(EGPrefs::find_pref("sys", "alert", nullptr));
  TEST_ASSERT_FALSE(EGPrefs::find_pref("sys", "nope", nullptr));
}

// a reset leaves no old file to hand back
static void reset_drops(void) {
  EGPrefs::reset_all(false);
  TEST_ASSERT_FALSE(LittleFS.exists("/prefs/input.bin"));
  TEST_ASSERT_FALSE(LittleFS.exists("/prefs/display.bin"));
  TEST_ASSERT_FALSE(LittleFS.exists("/prefs/env.bin"));
  TEST_ASSERT_FALSE(LittleFS.exists("/prefs/sys.bin"));
}

// blob records, u16 BE len
static std::string brec(const char* key, const char* val) {
  std::string r(1, (char)strlen(key));
  r += key;
  r += (char)(strlen(val) >> 8);
  r += (char)(strlen(val) & 0xFF);
  return r + val;
}

static std::string bgroup(const char* mid, int n, const std::string& recs) {
  std::string g(1, (char)strlen(mid));
  g += mid;
  g += (char)n;
  return g + recs;
}

static int import(const std::string& b) {
  return ImportText::apply_blob((const uint8_t*)b.data(), b.size(), EGPrefs::put);
}

// an 0.12 blob: display wins over env, as at boot
static void import_old(void) {
  std::string b = std::string(1, 2) + bgroup("display", 1, brec("sda", "17")) +
                  bgroup("env", 2, brec("sda", "12") + brec("scl", "34"));
  TEST_ASSERT_EQUAL_INT(3, import(b));
  TEST_ASSERT_EQUAL_UINT32(17, EGPrefs::getUInt("i2c", "sda"));
  TEST_ASSERT_EQUAL_UINT32(34, EGPrefs::getUInt("i2c", "scl"));
}

// the new group wins, wherever it sits
static void import_new(void) {
  std::string b = std::string(1, 3) + bgroup("i2c", 1, brec("sda", "5")) +
                  bgroup("display", 1, brec("sda", "17")) +
                  bgroup("env", 1, brec("sda", "12"));
  TEST_ASSERT_EQUAL_INT(3, import(b));
  TEST_ASSERT_EQUAL_UINT32(5, EGPrefs::getUInt("i2c", "sda"));
}

int main(int, char**) {
  LittleFS.files["/prefs/sys.bin"] =
      egp1({ rec("name", "dev"), rec("web_pass", "pw"), rec("ratio", "175"), rec("warn", "80") });
  LittleFS.files["/prefs/input.bin"] =
      egp1({ rec("rx_pin", "14"), rec("cpm_mode", "2") });
  LittleFS.files["/prefs/display.bin"] = egp1({ rec("sda", "17") });
  LittleFS.files["/prefs/env.bin"] = egp1({ rec("sda", "12"), rec("scl", "34") });
  LittleFS.files["/geigerconfig.json"] = "{\"geigerRatio\":\"999\"}";
  EGPrefs::begin();

  UNITY_BEGIN();
  RUN_TEST(merged);
  RUN_TEST(sys_keeps);
  RUN_TEST(first_wins);
  RUN_TEST(old_names);
  RUN_TEST(reset_drops);
  RUN_TEST(import_old);
  RUN_TEST(import_new);
  return UNITY_END();
}
