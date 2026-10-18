/*
  test_timezones - EGTimeZone Olson to POSIX lookup and its generated table.

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

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#include "../../lib/EGTimeZone/src/EGTimeZone.h"

// prod copy is file-local
#include "../../lib/EGTimeZone/src/EGTimeZoneTable.h"

static const uint32_t NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

void setUp(void)    {}
void tearDown(void) {}

// sorted for the binary search
static void table_sorted(void) {
  for (uint32_t i = 1; i < NUM_ZONES; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "entry %u hash %u", i, (unsigned)zones[i].hash);
    TEST_ASSERT_TRUE_MESSAGE(zones[i - 1].hash < zones[i].hash, msg);
  }
}

// offset is 11 bits
static void rules_blob(void) {
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2048, (uint32_t)sizeof(TZ_RULES));
}

static void rule_starts(void) {
  const uint32_t blob = sizeof(TZ_RULES);
  for (uint32_t i = 0; i < NUM_ZONES; i++) {
    uint32_t off = zones[i].offset;
    char msg[64];
    snprintf(msg, sizeof(msg), "entry %u offset %u", i, off);
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(blob, off, msg);
    if (off) TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', TZ_RULES[off - 1], msg);
    TEST_ASSERT_NOT_EQUAL_MESSAGE('\0', TZ_RULES[off], msg);
  }
}

static void rules_used(void) {
  const uint32_t blob = sizeof(TZ_RULES);
  bool used[sizeof(TZ_RULES)] = { false };
  for (uint32_t i = 0; i < NUM_ZONES; i++) used[zones[i].offset] = true;

  for (uint32_t off = 0; off < blob - 1; off = off + strlen(TZ_RULES + off) + 1) {
    char msg[80];
    snprintf(msg, sizeof(msg), "unreferenced rule at %u: %s", off, TZ_RULES + off);
    TEST_ASSERT_TRUE_MESSAGE(used[off], msg);
  }
}

struct Zone {
  const char *olson;
  const char *posix;
};

static void assertResolves(const Zone *z, size_t n) {
  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_NOT_NULL_MESSAGE(EGTimeZone::posixFor(z[i].olson), z[i].olson);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(z[i].posix, EGTimeZone::posixFor(z[i].olson),
                                     z[i].olson);
  }
}

static void known_zones(void) {
  static const Zone golden[] = {
    { "Europe/London",    "GMT0BST,M3.5.0/1,M10.5.0" },
    // Apple's zoneinfo gives GMT0IST
    { "Europe/Dublin",    "IST-1GMT0,M10.5.0,M3.5.0/1" },
    { "America/New_York", "EST5EDT,M3.2.0,M11.1.0" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Asia/Kolkata",     "IST-5:30" },
    { "Asia/Kathmandu",   "<+0545>-5:45" },
    { "Antarctica/Troll", "<+00>0<+02>-2,M3.5.0/1,M10.5.0/3" },
    { "Etc/UTC",          "UTC0" },
  };
  assertResolves(golden, sizeof(golden) / sizeof(golden[0]));
}

// old names, for stored configs
static void renamed_zones(void) {
  static const Zone renamed[] = {
    { "Europe/Kyiv",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kiev",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "America/Ciudad_Juarez", "MST7MDT,M3.2.0,M11.1.0" },
    { "Pacific/Kanton",        "<+13>-13" },
    { "Pacific/Enderbury",     "<+13>-13" },
  };
  assertResolves(renamed, sizeof(renamed) / sizeof(renamed[0]));
}

static void alias_rules(void) {
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Europe/London"),
                        EGTimeZone::posixFor("Europe/Jersey"));
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Pacific/Kanton"),
                        EGTimeZone::posixFor("Pacific/Enderbury"));
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Etc/UTC"),
                        EGTimeZone::posixFor("UTC"));
}

// only proven to 21 bits
static void unknown_zone(void) {
  static const char *const unknown[] = {
    "",
    "Mars/Olympus_Mons",
    "Europe/Atlantis",
    "Etc/GMT+99",
  };
  for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++)
    TEST_ASSERT_NULL_MESSAGE(EGTimeZone::posixFor(unknown[i]), unknown[i]);
}

static void null_name(void) {
  TEST_ASSERT_NULL(EGTimeZone::posixFor(nullptr));
}

static void lookup_is_exact(void) {
  static const char *const wrong[] = {
    "europe/london",
    "EUROPE/LONDON",
    "Europe/Lond",
    "Europe/London/",
    "Europe/London ",
    " Europe/London",
  };
  for (size_t i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++)
    TEST_ASSERT_NULL_MESSAGE(EGTimeZone::posixFor(wrong[i]), wrong[i]);
}

static void misses_in_table(void) {
  for (int i = 0; i < 1000; i++) {
    char name[20];
    snprintf(name, sizeof(name), "Zone/%03d", i);
    TEST_ASSERT_NULL_MESSAGE(EGTimeZone::posixFor(name), name);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(table_sorted);
  RUN_TEST(rules_blob);
  RUN_TEST(rule_starts);
  RUN_TEST(rules_used);
  RUN_TEST(known_zones);
  RUN_TEST(renamed_zones);
  RUN_TEST(alias_rules);
  RUN_TEST(unknown_zone);
  RUN_TEST(null_name);
  RUN_TEST(lookup_is_exact);
  RUN_TEST(misses_in_table);
  return UNITY_END();
}
