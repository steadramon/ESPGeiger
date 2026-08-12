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

// Untrusted input off a wire. A value escaping a bound here reaches
// Counter::on_pulse_batch, so the rejection cases matter more than the
// happy path.

#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "GeigerInput/SerialParse.h"

using SerialFormat::common_validate;
using SerialFormat::parse_gc10;
using SerialFormat::parse_mightyohm;
using SerialFormat::parse_template;
using SerialFormat::SERIAL_MAX_COUNT;

static const int NOPE = -424242;   // sentinel: parser must not have written

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// common_validate
// ---------------------------------------------------------------------------

static void test_validate_accepts_a_normal_line(void) {
  TEST_ASSERT_TRUE(common_validate("CPS, 1, CPM, 20\r\n", 17));
}

static void test_validate_rejects_empty_and_null(void) {
  TEST_ASSERT_FALSE(common_validate("", 0));
  TEST_ASSERT_FALSE(common_validate(nullptr, 4));
}

static void test_validate_rejects_control_and_high_bytes(void) {
  TEST_ASSERT_FALSE(common_validate("12\x01\x02", 4));
  const char high[] = { '1', '2', (char)0xFF, '\0' };
  TEST_ASSERT_FALSE(common_validate(high, 3));
}

// ---------------------------------------------------------------------------
// GC10 / GC10Next: digits only
// ---------------------------------------------------------------------------

static void test_gc10_parses_digits(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_gc10("123\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(123, cpm);
}

static void test_gc10_accepts_zero(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_gc10("0\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(0, cpm);
}

static void test_gc10_rejects_anything_but_digits(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("12A\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("CPM: 12\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("-5\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10(" 12\r\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

// A bare terminator is all the device sends when it has nothing to say. It
// must not read as zero counts.
static void test_gc10_rejects_a_line_with_no_digits(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10("\r\n", &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void test_gc10_rejects_above_the_bound(void) {
  int cpm = NOPE;
  char line[32];
  snprintf(line, sizeof(line), "%d\r\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_gc10(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);

  snprintf(line, sizeof(line), "%d\r\n", SERIAL_MAX_COUNT);
  TEST_ASSERT_TRUE(parse_gc10(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(SERIAL_MAX_COUNT, cpm);
}

// ---------------------------------------------------------------------------
// MightyOhm
// ---------------------------------------------------------------------------

static void test_mightyohm_parses_cps_and_cpm(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, 3, CPM, 41, uSv/hr, 0.23, SLOW\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(41, cpm);
  TEST_ASSERT_EQUAL_INT(3, cps);
}

static void test_mightyohm_rejects_a_wrong_shape(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_FALSE(parse_mightyohm("CPM, 41\n", &cpm, &cps));
  TEST_ASSERT_FALSE(parse_mightyohm("nonsense\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void test_mightyohm_rejects_cpm_above_the_bound(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPS, 1, CPM, %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_mightyohm(line, &cpm, &cps));
}

// cps is added straight to partial_clicks and handed to
// Counter::on_pulse_batch, so it needs the same bound cpm has. It did not
// have one: parse_template bounded both and this bounded only cpm.
static void test_mightyohm_bounds_cps_like_cpm(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPS, %d, CPM, 10\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_TRUE(parse_mightyohm(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);      // out of range, left untouched

  snprintf(line, sizeof(line), "CPS, %d, CPM, 10\n", SERIAL_MAX_COUNT);
  TEST_ASSERT_TRUE(parse_mightyohm(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(SERIAL_MAX_COUNT, cps);
}

static void test_mightyohm_ignores_negative_cps(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, -1, CPM, 10\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);
}

static void test_mightyohm_tolerates_a_null_cps_out(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_mightyohm("CPS, 3, CPM, 41\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(41, cpm);
}

// ---------------------------------------------------------------------------
// Template / ESPGeiger
// ---------------------------------------------------------------------------

static void test_template_reads_a_labelled_value(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_template("CPM: 77\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(77, cpm);
  TEST_ASSERT_TRUE(parse_template("CPM=78\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(78, cpm);
  TEST_ASSERT_TRUE(parse_template("CPM,79\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(79, cpm);
}

static void test_template_reads_both_labels(void) {
  int cpm = NOPE, cps = NOPE;
  TEST_ASSERT_TRUE(parse_template("CPM: 60 CPS: 1\n", &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(60, cpm);
  TEST_ASSERT_EQUAL_INT(1, cps);
}

// CHARACTERISATION. With no CPM tag it takes the first number on the line,
// whatever surrounds it. That is deliberate, because the user template is
// arbitrary, but it means almost nothing is rejected: _bad_streak barely
// moves and drainPort effectively never fires for this protocol.
static void test_template_falls_back_to_the_first_number(void) {
  int cpm = NOPE;
  TEST_ASSERT_TRUE(parse_template("total garbage 42 more\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(42, cpm);
}

static void test_template_rejects_a_line_with_no_number(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_template("no numbers here\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void test_template_rejects_above_the_bound(void) {
  int cpm = NOPE;
  char line[48];
  snprintf(line, sizeof(line), "CPM: %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_FALSE(parse_template(line, &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

// A negative label value returns -1 from the label reader, which is the same
// signal as "tag absent", so it must not slip through as a count.
static void test_template_rejects_a_negative_count(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_template("CPM: -5\n", &cpm, nullptr));
  TEST_ASSERT_EQUAL_INT(NOPE, cpm);
}

static void test_template_bounds_cps(void) {
  int cpm = NOPE, cps = NOPE;
  char line[64];
  snprintf(line, sizeof(line), "CPM: 10 CPS: %d\n", SERIAL_MAX_COUNT + 1);
  TEST_ASSERT_TRUE(parse_template(line, &cpm, &cps));
  TEST_ASSERT_EQUAL_INT(10, cpm);
  TEST_ASSERT_EQUAL_INT(NOPE, cps);
}

// ---------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------

static void test_null_arguments_are_refused(void) {
  int cpm = NOPE;
  TEST_ASSERT_FALSE(parse_gc10(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_mightyohm(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_template(nullptr, &cpm, nullptr));
  TEST_ASSERT_FALSE(parse_gc10("1\n", nullptr, nullptr));
  TEST_ASSERT_FALSE(parse_mightyohm("CPS, 1, CPM, 2\n", nullptr, nullptr));
  TEST_ASSERT_FALSE(parse_template("CPM: 1\n", nullptr, nullptr));
}

// A byte >= 0x80 held in a plain char widens differently on host and target.
// -fno-signed-char makes the host match; this pins it.
static void test_high_bytes_do_not_pass_validation(void) {
  const char line[] = { 'C', 'P', 'M', ':', ' ', '1', (char)0x92, '\0' };
  TEST_ASSERT_FALSE(common_validate(line, strlen(line)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_validate_accepts_a_normal_line);
  RUN_TEST(test_validate_rejects_empty_and_null);
  RUN_TEST(test_validate_rejects_control_and_high_bytes);
  RUN_TEST(test_gc10_parses_digits);
  RUN_TEST(test_gc10_accepts_zero);
  RUN_TEST(test_gc10_rejects_anything_but_digits);
  RUN_TEST(test_gc10_rejects_a_line_with_no_digits);
  RUN_TEST(test_gc10_rejects_above_the_bound);
  RUN_TEST(test_mightyohm_parses_cps_and_cpm);
  RUN_TEST(test_mightyohm_rejects_a_wrong_shape);
  RUN_TEST(test_mightyohm_rejects_cpm_above_the_bound);
  RUN_TEST(test_mightyohm_bounds_cps_like_cpm);
  RUN_TEST(test_mightyohm_ignores_negative_cps);
  RUN_TEST(test_mightyohm_tolerates_a_null_cps_out);
  RUN_TEST(test_template_reads_a_labelled_value);
  RUN_TEST(test_template_reads_both_labels);
  RUN_TEST(test_template_falls_back_to_the_first_number);
  RUN_TEST(test_template_rejects_a_line_with_no_number);
  RUN_TEST(test_template_rejects_above_the_bound);
  RUN_TEST(test_template_rejects_a_negative_count);
  RUN_TEST(test_template_bounds_cps);
  RUN_TEST(test_null_arguments_are_refused);
  RUN_TEST(test_high_bytes_do_not_pass_validation);
  return UNITY_END();
}
