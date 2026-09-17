/*
  test_serialparse - SerialFormat wire parsers.

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

#include "GeigerInput/SerialParse.h"

using SerialFormat::common_validate;
using SerialFormat::parse_gc10;
using SerialFormat::parse_mightyohm;
using SerialFormat::parse_template;
using SerialFormat::parse_espgeiger;
using SerialFormat::SERIAL_MAX_COUNT;

static const int NOPE = -424242;   // parser must not have written

void setUp(void)    {}
void tearDown(void) {}

static void validate_ok(void) {
  TEST_ASSERT_TRUE(common_validate("CPS, 1, CPM, 20\r\n", 17));
}

static void validate_empty(void) {
  TEST_ASSERT_FALSE(common_validate("", 0));
  TEST_ASSERT_FALSE(common_validate(nullptr, 4));
}

static void validate_ctrl(void) {
  TEST_ASSERT_FALSE(common_validate("12\x01\x02", 4));
  const char high[] = { '1', '2', (char)0xFF, '\0' };
  TEST_ASSERT_FALSE(common_validate(high, 3));
}

static void gc10_parses_digits(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_gc10("123\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(123, cpm);
}

static void gc10_accepts_zero(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_gc10("0\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(0, cpm);
}

static void gc10_non_digits(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("12A\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("CPM: 12\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("-5\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10(" 12\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void gc10_bare_end(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void gc10_bound(void) {
  int cpm = NOPE;
  char line[32];
  snprintf(line, sizeof(line), "%d\r\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_gc10(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);

  snprintf(line, sizeof(line), "%d\r\n", SERIAL_MAX_COUNT);
  TEST_ASSERT_TRUE(parse_gc10(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(SERIAL_MAX_COUNT, cpm);
}

static void mightyohm_ok(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, 3, CPM, 41, uSv/hr, 0.23, SLOW\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(41, cpm);
  TEST_ASSERT_EQUAL_INT(3, cps);
}

static void mightyohm_shape(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_FALSE(parse_mightyohm("CPM, 41\n", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("nonsense\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void mightyohm_bound(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPS, 1, CPM, %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_mightyohm(line, &cpm, &cps));
}

static void mightyohm_cps_bound(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPS, %d, CPM, 10\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_TRUE(parse_mightyohm(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);

  snprintf(line, sizeof(line), "CPS, %d, CPM, 10\n", SERIAL_MAX_COUNT);
  TEST_ASSERT_TRUE(parse_mightyohm(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(SERIAL_MAX_COUNT, cps);
}

static void mightyohm_neg_cps(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, -1, CPM, 10\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);
}

// as sscanf "%d" took them
static void gc10_scanf(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_gc10("\r\n12\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(12, cpm);
  TEST_ASSERT_TRUE(parse_gc10("007\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(7, cpm);
  TEST_ASSERT_TRUE(parse_gc10("12\r\n34", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(12, cpm);
  cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("99999999999\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

// 2^32 + 101, no wrap
static void gc10_overflow(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("4294967397\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

// as sscanf "CPS, %d, CPM, %d" took them
static void mightyohm_scanf(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS,3,CPM,41", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(41, cpm);
  TEST_ASSERT_EQUAL_INT(3, cps);
  TEST_ASSERT_TRUE(parse_mightyohm("CPS,  \t4,\r\n CPM,   +42 junk", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(42, cpm);
  TEST_ASSERT_EQUAL_INT(4, cps);
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, 99999999999, CPM, 10\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(4, cps);   // out of range, left
  cpm = NOPE;
  TEST_ASSERT_FALSE(parse_mightyohm(" CPS, 3, CPM, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS , 3, CPM, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3 , CPM, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3xCPM, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("cps, 3, cpm, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3, CPM, x", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, x, CPM, 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3, CPM 41", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3, CPM, ", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 3, CPM, 99999999999", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, - 3, CPM, 41", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void mightyohm_null_cps(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, 3, CPM, 41\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(41, cpm);
}

static void template_label(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_template("CPM: 77\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(77, cpm);
  TEST_ASSERT_TRUE(parse_template("CPM=78\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(78, cpm);
  TEST_ASSERT_TRUE(parse_template("CPM,79\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(79, cpm);
}

static void template_both(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_template("CPM: 60 CPS: 1\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(60, cpm);
  TEST_ASSERT_EQUAL_INT(1, cps);
}

static void espgeiger_ok(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_espgeiger("CPM: 42\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(42, cpm);
  TEST_ASSERT_TRUE(parse_espgeiger("CPM: 60 CPS: 1 uSv/h: 0.34\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(60, cpm);
  TEST_ASSERT_EQUAL_INT(1, cps);
}

static void espgeiger_label(void) {
  const char* lines[] = {
    "12:34:56 Prefs: Initialised (18 groups)\n",
    "Uptime: 500\n",
    "total garbage 42 more\n",
    "CPS: 4\n",
  };
  for (const char* l : lines) {
    int cpm = NOPE;
    TEST_ASSERT_FALSE_MESSAGE(parse_espgeiger(l, &cpm, nullptr), l);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NOPE, cpm, l);
  }
}

// no tag takes first number
static void template_first_num(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_template("total garbage 42 more\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(42, cpm);
}

static void template_no_num(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_template("no numbers here\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void template_bound(void) {
  int cpm = NOPE;
  char line[48];
  snprintf(line, sizeof(line), "CPM: %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_template(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void template_negative(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_template("CPM: -5\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void template_overflow(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_FALSE(parse_template("CPM: 4294967306\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_espgeiger("CPM: 99999999999999999999\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_template("x 4294967306 y\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_template("uSv 5 CPM: 4294967306\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
  TEST_ASSERT_TRUE(parse_template("CPM: 10 CPS: 4294967297\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);
}

static void template_bounds_cps(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPM: 10 CPS: %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_TRUE(parse_template(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);
}

static void null_args(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_mightyohm(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_template(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("1\n", nullptr, nullptr));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 1, CPM, 2\n", nullptr, nullptr));
  TEST_ASSERT_FALSE(parse_template("CPM: 1\n", nullptr, nullptr));
}

// needs -fno-signed-char

static void validate_high(void) {
  const char line[] = { 'C', 'P', 'M', ':', ' ', '1', (char)0x92, '\0' };
  TEST_ASSERT_FALSE(common_validate(line, strlen(line)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(validate_ok);
  RUN_TEST(validate_empty);
  RUN_TEST(validate_ctrl);
  RUN_TEST(gc10_parses_digits);
  RUN_TEST(gc10_accepts_zero);
  RUN_TEST(gc10_non_digits);
  RUN_TEST(gc10_bare_end);
  RUN_TEST(gc10_bound);
  RUN_TEST(mightyohm_ok);
  RUN_TEST(mightyohm_shape);
  RUN_TEST(mightyohm_bound);
  RUN_TEST(mightyohm_cps_bound);
  RUN_TEST(mightyohm_neg_cps);
  RUN_TEST(mightyohm_null_cps);
  RUN_TEST(gc10_scanf);
  RUN_TEST(gc10_overflow);
  RUN_TEST(mightyohm_scanf);
  RUN_TEST(template_label);
  RUN_TEST(template_both);
  RUN_TEST(espgeiger_ok);
  RUN_TEST(espgeiger_label);
  RUN_TEST(template_first_num);
  RUN_TEST(template_no_num);
  RUN_TEST(template_bound);
  RUN_TEST(template_negative);
  RUN_TEST(template_overflow);
  RUN_TEST(template_bounds_cps);
  RUN_TEST(null_args);
  RUN_TEST(validate_high);
  return UNITY_END();
}
