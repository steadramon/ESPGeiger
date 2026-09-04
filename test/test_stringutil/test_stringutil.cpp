/*
  test_stringutil - advance_pos, format_f, parse_f, parse_i32, parseTime.

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

#include "Corium/Util/Fnv.h"
#include "Corium/Util/StringUtil.h"
#include <EGFormat.h>

void setUp(void)    {}
void tearDown(void) {}

static void advance_pos_ok(void) {
  size_t pos = 0;
  advance_pos(pos, 5, 32);
  TEST_ASSERT_EQUAL_size_t(5, pos);
  advance_pos(pos, 7, 32);
  TEST_ASSERT_EQUAL_size_t(12, pos);

  advance_pos(pos, 0, 32);
  TEST_ASSERT_EQUAL_size_t(12, pos);
  advance_pos(pos, -1, 32);
  TEST_ASSERT_EQUAL_size_t(12, pos);
}

static void advance_trunc(void) {
  size_t pos = 0;
  advance_pos(pos, 7, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);

  pos = 0;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);        // bufsz - 1

  pos = 7;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(7, pos);

  pos = 99;
  advance_pos(pos, 100, 8);
  TEST_ASSERT_EQUAL_size_t(99, pos);
}

static void advance_loop(void) {
  char buf[16];
  size_t pos = 0;

  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf + pos, sizeof(buf) - pos, "%d,", i);
    advance_pos(pos, n, sizeof(buf));
  }
  TEST_ASSERT_EQUAL_STRING("0,1,2,3,4,5,6,7", buf);
}

static void format_basics(void) {
  char b[32];
  format_f(b, sizeof(b), 0.0f);        TEST_ASSERT_EQUAL_STRING("0.00", b);
  format_f(b, sizeof(b), 1.5f);        TEST_ASSERT_EQUAL_STRING("1.50", b);
  format_f(b, sizeof(b), 0.21f);       TEST_ASSERT_EQUAL_STRING("0.21", b);
  format_f(b, sizeof(b), 1.05f);       TEST_ASSERT_EQUAL_STRING("1.05", b);
  format_f(b, sizeof(b), 10.0f);       TEST_ASSERT_EQUAL_STRING("10.00", b);
  format_f(b, sizeof(b), 123.456f);    TEST_ASSERT_EQUAL_STRING("123.46", b);
  format_f(b, sizeof(b), 12.3f, 1);    TEST_ASSERT_EQUAL_STRING("12.3", b);
  format_f(b, sizeof(b), 1.23456f, 4); TEST_ASSERT_EQUAL_STRING("1.2346", b);
}

// 0.005f is 0.00499999...
static void format_rounds(void) {
  char b[32];
  format_f(b, sizeof(b), 0.005f);  TEST_ASSERT_EQUAL_STRING("0.00", b);
  format_f(b, sizeof(b), 0.125f);  TEST_ASSERT_EQUAL_STRING("0.12", b);
  format_f(b, sizeof(b), 0.375f);  TEST_ASSERT_EQUAL_STRING("0.38", b);
  format_f(b, sizeof(b), 0.999f);  TEST_ASSERT_EQUAL_STRING("1.00", b);
}

static void format_negative(void) {
  char b[32];
  format_f(b, sizeof(b), -1.0f);   TEST_ASSERT_EQUAL_STRING("-1.00", b);
  format_f(b, sizeof(b), -5.25f);  TEST_ASSERT_EQUAL_STRING("-5.25", b);
  format_f(b, sizeof(b), -0.001f); TEST_ASSERT_EQUAL_STRING("-0.00", b);
}

// no point, ties to an even whole
static void format_whole(void) {
  char b[32];
  TEST_ASSERT_EQUAL_INT(2, format_f(b, sizeof(b), 42.4f, 0));
  TEST_ASSERT_EQUAL_STRING("42", b);
  format_f(b, sizeof(b), 42.6f, 0);  TEST_ASSERT_EQUAL_STRING("43", b);
  format_f(b, sizeof(b), 2.5f, 0);   TEST_ASSERT_EQUAL_STRING("2", b);
  format_f(b, sizeof(b), 3.5f, 0);   TEST_ASSERT_EQUAL_STRING("4", b);
}

// past 9 prints 9
static void format_many_dp(void) {
  char b[32];
  format_f(b, sizeof(b), 1.5f, 12);
  TEST_ASSERT_EQUAL_STRING("1.500000000", b);
}

static void format_truncates(void) {
  char b[4];
  TEST_ASSERT_EQUAL_INT(6, format_f(b, sizeof(b), 123.456f));
  TEST_ASSERT_EQUAL_STRING("123", b);

  char one[1] = {'x'};
  TEST_ASSERT_EQUAL_INT(5, format_f(one, sizeof(one), -1.5f, 2));
  TEST_ASSERT_EQUAL_CHAR('\0', one[0]);
}

static void hex_encode_ok(void) {
  const uint8_t in[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x90, 0x0A};
  char out[2 * sizeof(in) + 1];
  memset(out, 'x', sizeof(out));
  hex_encode(out, in, sizeof(in));
  TEST_ASSERT_EQUAL_CHAR('x', out[2 * sizeof(in)]);   // no NUL written
  out[2 * sizeof(in)] = '\0';
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef900a", out);
}

static void hex_decode_ok(void) {
  uint8_t out[10];
  const uint8_t want[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x90, 0x0A};
  TEST_ASSERT_TRUE(hex_decode(out, "0123456789abcdef900a", sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, sizeof(want));
  TEST_ASSERT_TRUE(hex_decode(out, "0123456789ABCDEF900A", sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, sizeof(want));
}

// out stays as it was
static void hex_decode_bad(void) {
  uint8_t out[2] = {0x55, 0x55};
  TEST_ASSERT_FALSE(hex_decode(out, "12", 2));       // short
  TEST_ASSERT_FALSE(hex_decode(out, "12345", 2));    // long
  TEST_ASSERT_FALSE(hex_decode(out, "12g4", 2));
  TEST_ASSERT_FALSE(hex_decode(out, "", 2));
  TEST_ASSERT_EQUAL_HEX8(0x55, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x55, out[1]);
}

static void parse_f_ok(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,   parse_f("0"));
  TEST_ASSERT_EQUAL_FLOAT(1.5f,   parse_f("1.5"));
  TEST_ASSERT_EQUAL_FLOAT(0.21f,  parse_f("0.21"));
  TEST_ASSERT_EQUAL_FLOAT(123.456f, parse_f("123.456"));
  TEST_ASSERT_EQUAL_FLOAT(-2.25f, parse_f("-2.25"));
  TEST_ASSERT_EQUAL_FLOAT(3.0f,   parse_f("+3"));
  TEST_ASSERT_EQUAL_FLOAT(0.5f,   parse_f(".5"));
  TEST_ASSERT_EQUAL_FLOAT(7.0f,   parse_f("7."));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.123456789f, parse_f("1.123456789987654321"));
}

static void parse_f_bad(void) {
  char* end = nullptr;
  const char* s = "abc";
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(s, &end));
  TEST_ASSERT_EQUAL_PTR(s, end);

  const char* m = "-";
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse_f(m, &end));
  TEST_ASSERT_EQUAL_PTR(m, end);

  TEST_ASSERT_EQUAL_FLOAT(1.5f, parse_f("1.5e3", &end));
  TEST_ASSERT_EQUAL_CHAR('e', *end);

  TEST_ASSERT_EQUAL_FLOAT(12.0f, parse_f("12abc", &end));
  TEST_ASSERT_EQUAL_CHAR('a', *end);
}

static void parse_round_trip(void) {
  const float vals[] = { 0.0f, 0.21f, 1.5f, 99.99f, 1000.5f };
  for (float v : vals) {
    char b[32];
    format_f(b, sizeof(b), v, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, v, parse_f(b));
  }
}

static void parse_i32_ok(void) {
  struct { const char* in; int32_t want; } cases[] = {
    {"0", 0}, {"42", 42}, {"-17", -17}, {"+5", 5}, {"007", 7}, {" 9", 9},
    {"2147483647", INT32_MAX}, {"-2147483648", INT32_MIN},
  };
  for (auto& c : cases) {
    int32_t v = 7;
    TEST_ASSERT_TRUE_MESSAGE(parse_i32(c.in, &v), c.in);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(c.want, v, c.in);
  }
}

static void parse_i32_bad(void) {
  const char* bad[] = {
    "", "-", "abc", "--1", "12abc", "1.5", "0x10",
    "2147483648", "-2147483649", "99999999999999999999",
  };
  for (auto s : bad) {
    int32_t v = 7;
    TEST_ASSERT_FALSE_MESSAGE(parse_i32(s, &v), s);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(7, v, s);
  }
  int32_t v = 7;
  TEST_ASSERT_FALSE(parse_i32(nullptr, &v));
  TEST_ASSERT_EQUAL_INT32(7, v);
}

static void parse_i32_end(void) {
  int32_t v = 0;
  const char* end = nullptr;
  const char* s = "123, CPS: 4";
  TEST_ASSERT_TRUE(parse_i32(s, &v, &end));
  TEST_ASSERT_EQUAL_INT32(123, v);
  TEST_ASSERT_EQUAL_PTR(s + 3, end);

  end = nullptr;
  TEST_ASSERT_FALSE(parse_i32("2147483648 CPM", &v, &end));
  TEST_ASSERT_NULL(end);
}

static void time_valid(void) {
  struct { const char* in; int h, m; } cases[] = {
    {"00:00",  0,  0},
    {"09:05",  9,  5},
    {"23:59", 23, 59},
  };
  for (auto& c : cases) {
    ParsedTime t = parseTime(c.in);
    TEST_ASSERT_TRUE_MESSAGE(t.isValid, c.in);
    TEST_ASSERT_EQUAL_INT(c.h, t.hour);
    TEST_ASSERT_EQUAL_INT(c.m, t.minute);
  }
}

static void time_bad(void) {
  const char* bad[] = {
    nullptr, "", "1:34", "12:345", "12-34", "24:00", "12:60", "ab:cd", "+1:00",
  };
  for (auto s : bad) {
    ParsedTime t = parseTime(s);
    TEST_ASSERT_FALSE_MESSAGE(t.isValid, s ? s : "(null)");
    TEST_ASSERT_EQUAL_INT(-1, t.hour);
    TEST_ASSERT_EQUAL_INT(-1, t.minute);
  }
}

// as Wifi saveBackupCreds writes it
static void split_wifi(void) {
  char buf[] = "home\npass word \n";
  char* f[2];
  TEST_ASSERT_EQUAL_UINT8(2, split_lines(buf, f, 2));
  TEST_ASSERT_EQUAL_STRING("home", f[0]);
  TEST_ASSERT_EQUAL_STRING("pass word ", f[1]);
}

static void split_short(void) {
  char buf[] = "home\r\n";
  char* f[2];
  TEST_ASSERT_EQUAL_UINT8(1, split_lines(buf, f, 2));
  TEST_ASSERT_EQUAL_STRING("home", f[0]);
  TEST_ASSERT_EQUAL_STRING("", f[1]);
}

static void split_open(void) {
  char buf[] = "cafe\n\n";
  char* f[2];
  TEST_ASSERT_EQUAL_UINT8(2, split_lines(buf, f, 2));
  TEST_ASSERT_EQUAL_STRING("cafe", f[0]);
  TEST_ASSERT_EQUAL_STRING("", f[1]);
}

static void split_extra(void) {
  char buf[] = "a\nb\nc";
  char* f[2];
  TEST_ASSERT_EQUAL_UINT8(2, split_lines(buf, f, 2));
  TEST_ASSERT_EQUAL_STRING("b", f[1]);
}

// published FNV-1a 32 vectors
static void fnv_vectors(void) {
  TEST_ASSERT_EQUAL_HEX32(0x811c9dc5, fnv1a(""));
  TEST_ASSERT_EQUAL_HEX32(0xe40c292c, fnv1a("a"));
  TEST_ASSERT_EQUAL_HEX32(0xbf9cf968, fnv1a("foobar"));
  TEST_ASSERT_EQUAL_HEX32(0xbf9cf968, fnv1a("bar", fnv1a("foo")));
  TEST_ASSERT_EQUAL_HEX32(0xbf9cf968, fnv1a_P(PSTR("foobar")));
  TEST_ASSERT_EQUAL_HEX32(0x811c9dc5, fnv1a(nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(fnv_vectors);
  RUN_TEST(advance_pos_ok);
  RUN_TEST(advance_trunc);
  RUN_TEST(advance_loop);
  RUN_TEST(format_basics);
  RUN_TEST(format_rounds);
  RUN_TEST(format_negative);
  RUN_TEST(format_whole);
  RUN_TEST(format_many_dp);
  RUN_TEST(format_truncates);
  RUN_TEST(hex_encode_ok);
  RUN_TEST(hex_decode_ok);
  RUN_TEST(hex_decode_bad);
  RUN_TEST(parse_f_ok);
  RUN_TEST(parse_f_bad);
  RUN_TEST(parse_round_trip);
  RUN_TEST(parse_i32_ok);
  RUN_TEST(parse_i32_bad);
  RUN_TEST(parse_i32_end);
  RUN_TEST(time_valid);
  RUN_TEST(time_bad);
  RUN_TEST(split_wifi);
  RUN_TEST(split_short);
  RUN_TEST(split_open);
  RUN_TEST(split_extra);
  return UNITY_END();
}
