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

// Europe/Kyiv resolved to UTC0 with no log for two years: the table knew only
// Europe/Kiev, the hash missed, and the miss was silent. The table stores no
// names, so nothing in the lookup can notice a table that breaks its own
// preconditions. Those preconditions are asserted here.
//
// The generator enforces the same rules at build time. This suite asserts them
// against the committed artifact, which is what the device actually runs.

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#include "../../lib/EGTimeZone/src/EGTimeZone.h"

// A second copy of the generated data, private to this TU. The production one
// is deliberately visible only to EGTimeZone.cpp, and these tests walk it.
#include "../../lib/EGTimeZone/src/EGTimeZoneTable.h"

static const uint32_t NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

void setUp(void)    {}
void tearDown(void) {}

// --- the generated table ----------------------------------------------------

// posixFor() binary searches. Unsorted, it silently misses zones that are
// present. Strictly increasing also proves no two zones share a hash, which
// at 21 bits is not free.
static void test_table_is_sorted_and_collision_free(void) {
  for (uint32_t i = 1; i < NUM_ZONES; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "entry %u hash %u", i, (unsigned)zones[i].hash);
    TEST_ASSERT_TRUE_MESSAGE(zones[i - 1].hash < zones[i].hash, msg);
  }
}

// offset is 11 bits, so a blob past 2048 bytes truncates into a wrong rule
// rather than failing to build.
static void test_rules_blob_fits_the_offset_field(void) {
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2048, (uint32_t)sizeof(TZ_RULES));
}

// Every offset must name the first byte of a rule, not land mid-string.
static void test_offsets_land_on_rule_starts(void) {
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

// The blob is deduplicated by hand-free construction; a rule nothing points at
// is flash spent on nothing.
static void test_every_rule_is_referenced(void) {
  const uint32_t blob = sizeof(TZ_RULES);
  bool used[sizeof(TZ_RULES)] = { false };
  for (uint32_t i = 0; i < NUM_ZONES; i++) used[zones[i].offset] = true;

  for (uint32_t off = 0; off < blob - 1; off = off + strlen(TZ_RULES + off) + 1) {
    char msg[80];
    snprintf(msg, sizeof(msg), "unreferenced rule at %u: %s", off, TZ_RULES + off);
    TEST_ASSERT_TRUE_MESSAGE(used[off], msg);
  }
}

// --- lookup -----------------------------------------------------------------

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

static void test_known_zones(void) {
  static const Zone golden[] = {
    { "Europe/London",    "GMT0BST,M3.5.0/1,M10.5.0" },
    // Apple's zoneinfo gives GMT0IST here at the same tzdb version. The
    // generator refuses the system tree so Ireland cannot drift.
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

// The three upstream renames the hand-maintained table had missed, plus the
// old names, which stay reachable so a stored config keeps working.
static void test_renamed_zones_resolve(void) {
  static const Zone renamed[] = {
    { "Europe/Kyiv",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kiev",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "America/Ciudad_Juarez", "MST7MDT,M3.2.0,M11.1.0" },
    { "Pacific/Kanton",        "<+13>-13" },
    { "Pacific/Enderbury",     "<+13>-13" },
  };
  assertResolves(renamed, sizeof(renamed) / sizeof(renamed[0]));
}

// Aliases share one rule rather than a copy each.
static void test_aliases_share_one_rule(void) {
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Europe/London"),
                        EGTimeZone::posixFor("Europe/Jersey"));
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Pacific/Kanton"),
                        EGTimeZone::posixFor("Pacific/Enderbury"));
  TEST_ASSERT_EQUAL_PTR(EGTimeZone::posixFor("Etc/UTC"),
                        EGTimeZone::posixFor("UTC"));
}

// A miss must be nullptr. Returning UTC0 instead is the bug this API replaced.
//
// Names are not stored, so a miss is proven only to 21 bits: an unknown name
// colliding with a real hash gets that zone's rule. These cases are checked
// clear of the current table; a tzdb bump could in principle claim one.
static void test_unknown_returns_null(void) {
  static const char *const unknown[] = {
    "",
    "Mars/Olympus_Mons",
    "Europe/Atlantis",
    "Etc/GMT+99",
  };
  for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++)
    TEST_ASSERT_NULL_MESSAGE(EGTimeZone::posixFor(unknown[i]), unknown[i]);
}

// The stored name is compared whole. A prefix or a stray space is a different
// zone, not a near miss to be forgiven.
static void test_lookup_is_exact(void) {
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

// Drives the search down many paths, including both ends of the table. Under
// native_asan this is also the check that a miss never probes out of range.
static void test_misses_stay_inside_the_table(void) {
  for (int i = 0; i < 1000; i++) {
    char name[16];
    snprintf(name, sizeof(name), "Zone/%03d", i);
    TEST_ASSERT_NULL_MESSAGE(EGTimeZone::posixFor(name), name);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_table_is_sorted_and_collision_free);
  RUN_TEST(test_rules_blob_fits_the_offset_field);
  RUN_TEST(test_offsets_land_on_rule_starts);
  RUN_TEST(test_every_rule_is_referenced);
  RUN_TEST(test_known_zones);
  RUN_TEST(test_renamed_zones_resolve);
  RUN_TEST(test_aliases_share_one_rule);
  RUN_TEST(test_unknown_returns_null);
  RUN_TEST(test_lookup_is_exact);
  RUN_TEST(test_misses_stay_inside_the_table);
  return UNITY_END();
}
