/*
  test_stringutil - advance_pos, format_f, parse_f, parseTime.

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

// advance_pos exists because snprintf returns what it WOULD have written.
// Getting it wrong walks the cursor past the buffer and the next snprintf
// scribbles the stack. The truncation cases are the ones that matter.

#include <unity.h>
#include <Arduino.h>
#include <string.h>

#include "Util/StringUtil.h"

void setUp(void)    {}
void tearDown(void) {}

// --- advance_pos ------------------------------------------------------------

static void test_advance_pos_normal(void) {
  size_t pos = 0;
  advance_pos(pos, 5, 32);
  TEST_ASSERT_EQUAL_size_t(5, pos);
  advance_pos(pos, 7, 32);
  TEST_ASSERT_EQUAL_size_t(12, pos);
}

// snprintf returns a negative on encoding error and 0 for an empty write.
// Neither may move the cursor.
static void test_advance_pos_ignores_non_positive(void) {
  size_t pos = 4;
  advance_pos(pos, 0, 32);
  TEST_ASSERT_EQUAL_size_t(4, pos);
  advance_pos(pos, -1, 32);
  TEST_ASSERT_EQUAL_size_t(4, pos);
}

// The whole point: a truncated write must leave pos where the NUL is, never
// where snprintf claims it would have finished.
static void test_advance_pos_clamps_on_truncation(void) {
  size_t pos = 0;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);        // bufsz - 1, room for the NUL

  pos = 6;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);

  pos = 7;                                  // already at the NUL slot
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);
}

// A cursor at or past the end must be idempotent, not wrap or run away.
static void test_advance_pos_saturated_cursor(void) {
  size_t pos = 8;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(8, pos);

  pos = 99;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(99, pos);

  pos = 0;
  advance_pos(pos, 100, 0);
  TEST_ASSERT_EQUAL_size_t(0, pos);
}

// The exact boundary between "fits" and "truncated". want == room is already
// truncation, because the NUL takes the last byte.
static void test_advance_pos_fit_boundary(void) {
  size_t pos = 0;
  advance_pos(pos, 6, 8);        // room = 7, fits
  TEST_ASSERT_EQUAL_size_t(6, pos);

  pos = 0;
  advance_pos(pos, 7, 8);        // room = 7, want == room
  TEST_ASSERT_EQUAL_size_t(7, pos);

  pos = 0;
  advance_pos(pos, 8, 8);        // one over
  TEST_ASSERT_EQUAL_size_t(7, pos);
}

// The usage that motivates it, driven until the buffer fills. Every write
// stays in bounds and the result stays NUL-terminated.
static void test_advance_pos_loop_never_leaves_the_buffer(void) {
  char buf[16];
  memset(buf, 0x7F, sizeof(buf));
  size_t pos = 0;

  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf + pos, sizeof(buf) - pos, "%d,", i);
    advance_pos(pos, n, sizeof(buf));
    TEST_ASSERT_LESS_THAN_size_t(sizeof(buf), pos);
  }
  TEST_ASSERT_EQUAL_CHAR('\0', buf[pos]);
  TEST_ASSERT_EQUAL_STRING("0,1,2,3,4,5,6,7", buf);
}

// --- format_f ---------------------------------------------------------------

static void test_format_f_basics(void) {
  char b[32];
  format_f(b, sizeof(b), 0.0f);        TEST_ASSERT_EQUAL_STRING("0.00", b);
  format_f(b, sizeof(b), 1.5f);        TEST_ASSERT_EQUAL_STRING("1.50", b);
  format_f(b, sizeof(b), 0.21f);       TEST_ASSERT_EQUAL_STRING("0.21", b);
  format_f(b, sizeof(b), 123.456f);    TEST_ASSERT_EQUAL_STRING("123.46", b);
  format_f(b, sizeof(b), 12.3f, 1);    TEST_ASSERT_EQUAL_STRING("12.3", b);
  format_f(b, sizeof(b), 1.23456f, 4); TEST_ASSERT_EQUAL_STRING("1.2346", b);
}

// Fractional digits are zero-padded, so 1.05 does not print as "1.5".
static void test_format_f_pads_fraction(void) {
  char b[32];
  format_f(b, sizeof(b), 1.05f);   TEST_ASSERT_EQUAL_STRING("1.05", b);
  format_f(b, sizeof(b), 1.005f, 3); TEST_ASSERT_EQUAL_STRING("1.005", b);
  format_f(b, sizeof(b), 10.0f);   TEST_ASSERT_EQUAL_STRING("10.00", b);
}

static void test_format_f_rounds_half_up(void) {
  char b[32];
  format_f(b, sizeof(b), 0.005f);  TEST_ASSERT_EQUAL_STRING("0.01", b);
  format_f(b, sizeof(b), 0.004f);  TEST_ASSERT_EQUAL_STRING("0.00", b);
  format_f(b, sizeof(b), 0.999f);  TEST_ASSERT_EQUAL_STRING("1.00", b);
  format_f(b, sizeof(b), 9.995f);  TEST_ASSERT_EQUAL_STRING("10.00", b);
}

// Negatives are clamped to zero, not printed. Every caller feeds a rate, a
// dose or a temperature delta that cannot legitimately go below zero.
static void test_format_f_clamps_negative_to_zero(void) {
  char b[32];
  format_f(b, sizeof(b), -1.0f);   TEST_ASSERT_EQUAL_STRING("0.00", b);
  format_f(b, sizeof(b), -0.001f); TEST_ASSERT_EQUAL_STRING("0.00", b);
}

// OUT OF CONTRACT. format_f documents decimals >= 1; at 0 the "%0*ld" width
// collapses and the output still carries a point and a zero. Not fixed on
// purpose: the fix needs a branch inside a function kept out-of-line so GCC
// does not clone it per call site, and no caller passes 0. Recorded so the
// behaviour of an out-of-contract input is visible if it ever changes.
static void test_format_f_zero_decimals_is_out_of_contract(void) {
  char b[32];
  format_f(b, sizeof(b), 42.4f, 0);
  TEST_ASSERT_EQUAL_STRING("42.0", b);
}

// snprintf semantics all the way down: truncation is silent, and the return
// is the would-have-written length. Callers must run it through advance_pos.
static void test_format_f_truncates_silently(void) {
  char b[4];
  memset(b, 0x7F, sizeof(b));
  int n = format_f(b, sizeof(b), 123.456f);
  TEST_ASSERT_EQUAL_INT(6, n);                 // "123.46" would need 6
  TEST_ASSERT_EQUAL_STRING("123", b);          // 3 chars plus the NUL
  TEST_ASSERT_GREATER_THAN_INT((int)sizeof(b) - 1, n);
}

// --- parse_f ----------------------------------------------------------------

static void test_parse_f_basics(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,   parse_f("0"));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,   parse_f("1"));
  TEST_ASSERT_EQUAL_FLOAT(1.5f,   parse_f("1.5"));
  TEST_ASSERT_EQUAL_FLOAT(0.21f,  parse_f("0.21"));
  TEST_ASSERT_EQUAL_FLOAT(123.456f, parse_f("123.456"));
  TEST_ASSERT_EQUAL_FLOAT(-2.25f, parse_f("-2.25"));
  TEST_ASSERT_EQUAL_FLOAT(3.0f,   parse_f("+3"));
  TEST_ASSERT_EQUAL_FLOAT(0.5f,   parse_f(".5"));       // no integer part
  TEST_ASSERT_EQUAL_FLOAT(7.0f,   parse_f("7."));       // no fraction digits
}

static void test_parse_f_round_trips_with_format_f(void) {
  const float vals[] = { 0.0f, 0.21f, 1.5f, 12.34f, 99.99f, 1000.5f };
  for (float v : vals) {
    char b[32];
    format_f(b, sizeof(b), v, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, v, parse_f(b));
  }
}

// Non-numeric input yields 0 with endptr parked at the start, which is how a
// caller distinguishes "the config said 0" from "the config was garbage".
static void test_parse_f_rejects_non_numeric(void) {
  const char* s = "abc";
  char* end = nullptr;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(s, &end));
  TEST_ASSERT_EQUAL_PTR(s, end);

  const char* e = "";
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(e, &end));
  TEST_ASSERT_EQUAL_PTR(e, end);

  // A lone sign is not a number either.
  const char* m = "-";
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(m, &end));
  TEST_ASSERT_EQUAL_PTR(m, end);
}

// Exponents, hex floats and leading whitespace are not supported. Parsing
// stops at the first character it does not understand and endptr says where.
static void test_parse_f_stops_at_unsupported_syntax(void) {
  char* end = nullptr;

  TEST_ASSERT_EQUAL_FLOAT(1.5f, parse_f("1.5e3", &end));
  TEST_ASSERT_EQUAL_CHAR('e', *end);

  TEST_ASSERT_EQUAL_FLOAT(12.0f, parse_f("12abc", &end));
  TEST_ASSERT_EQUAL_CHAR('a', *end);

  const char* ws = " 1.5";
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(ws, &end));
  TEST_ASSERT_EQUAL_PTR(ws, end);
}

// Fraction digits past the ninth are consumed but do not contribute, which
// keeps the scaling integer from overflowing.
static void test_parse_f_long_fraction_does_not_overflow(void) {
  char* end = nullptr;
  float v = parse_f("1.123456789987654321", &end);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.123456789f, v);
  TEST_ASSERT_EQUAL_CHAR('\0', *end);
}

// --- parseTime --------------------------------------------------------------

static void test_parse_time_valid(void) {
  struct { const char* in; int h, m; } cases[] = {
    {"00:00",  0,  0},
    {"09:05",  9,  5},
    {"12:34", 12, 34},
    {"23:59", 23, 59},
  };
  for (auto& c : cases) {
    ParsedTime t = parseTime(c.in);
    TEST_ASSERT_TRUE_MESSAGE(t.isValid, c.in);
    TEST_ASSERT_EQUAL_INT(c.h, t.hour);
    TEST_ASSERT_EQUAL_INT(c.m, t.minute);
  }
}

static void test_parse_time_rejects_bad_shape(void) {
  const char* bad[] = {
    nullptr, "", "1:34", "123:4", "12:345", "12-34", "1234", "12:3",
    "24:00", "12:60", "99:99",
  };
  for (auto s : bad) {
    ParsedTime t = parseTime(s);
    TEST_ASSERT_FALSE_MESSAGE(t.isValid, s ? s : "(null)");
    TEST_ASSERT_EQUAL_INT(-1, t.hour);
    TEST_ASSERT_EQUAL_INT(-1, t.minute);
  }
}

// REGRESSION. The fields were parsed with atoi, which maps anything
// non-numeric to 0, so a well-shaped string of letters returned a valid
// midnight. A garbage quiet-hours pref silently became a 00:00-00:00 window
// instead of being rejected. Every position around the colon must be a digit.
static void test_parse_time_rejects_non_digits(void) {
  const char* bad[] = {
    "ab:cd", "1a:00", "a1:00", "00:1a", "00:a1", "  :  ", "+1:00",
    "-1:00", "1 :00", "00: 0", "0x:00",
  };
  for (auto s : bad) {
    ParsedTime t = parseTime(s);
    TEST_ASSERT_FALSE_MESSAGE(t.isValid, s);
    TEST_ASSERT_EQUAL_INT(-1, t.hour);
    TEST_ASSERT_EQUAL_INT(-1, t.minute);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_advance_pos_normal);
  RUN_TEST(test_advance_pos_ignores_non_positive);
  RUN_TEST(test_advance_pos_clamps_on_truncation);
  RUN_TEST(test_advance_pos_saturated_cursor);
  RUN_TEST(test_advance_pos_fit_boundary);
  RUN_TEST(test_advance_pos_loop_never_leaves_the_buffer);
  RUN_TEST(test_format_f_basics);
  RUN_TEST(test_format_f_pads_fraction);
  RUN_TEST(test_format_f_rounds_half_up);
  RUN_TEST(test_format_f_clamps_negative_to_zero);
  RUN_TEST(test_format_f_zero_decimals_is_out_of_contract);
  RUN_TEST(test_format_f_truncates_silently);
  RUN_TEST(test_parse_f_basics);
  RUN_TEST(test_parse_f_round_trips_with_format_f);
  RUN_TEST(test_parse_f_rejects_non_numeric);
  RUN_TEST(test_parse_f_stops_at_unsupported_syntax);
  RUN_TEST(test_parse_f_long_fraction_does_not_overflow);
  RUN_TEST(test_parse_time_valid);
  RUN_TEST(test_parse_time_rejects_bad_shape);
  RUN_TEST(test_parse_time_rejects_non_digits);
  return UNITY_END();
}
