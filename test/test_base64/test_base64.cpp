/*
  test_base64 - EGBase64 codec.

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
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../lib/EGBase64/src/EGBase64.h"

void setUp(void)    {}
void tearDown(void) {}

static void alphabet(void) {
  for (unsigned v = 0; v < 64; v++) {
    unsigned char c = binary_to_base64((unsigned char)v);
    TEST_ASSERT_EQUAL_UINT8(v, base64_to_binary(c));
  }
  TEST_ASSERT_EQUAL_UINT8('A', binary_to_base64(0));
  TEST_ASSERT_EQUAL_UINT8('a', binary_to_base64(26));
  TEST_ASSERT_EQUAL_UINT8('0', binary_to_base64(52));
  TEST_ASSERT_EQUAL_UINT8('+', binary_to_base64(62));
  TEST_ASSERT_EQUAL_UINT8('/', binary_to_base64(63));
}

static void invalid_chars(void) {
  const char* bad = "-_ \t\n!@#=";
  for (const char* p = bad; *p; p++) {
    TEST_ASSERT_EQUAL_UINT8(255, base64_to_binary((unsigned char)*p));
  }
}

static void encode_length(void) {
  struct { unsigned in, out; } cases[] = {
    {0, 0}, {1, 4}, {2, 4}, {3, 4}, {4, 8}, {48, 64},
  };
  for (auto& c : cases) TEST_ASSERT_EQUAL_UINT(c.out, encode_base64_length(c.in));
  TEST_ASSERT_EQUAL_UINT(64, EGB64_LEN(48));
}

static void decode_length(void) {
  struct { const char* in; unsigned out; } cases[] = {
    {"",         0},
    {"TQ==",     1},
    {"TWE=",     2},
    {"TWFu",     3},
    {"TWFuTQ==", 4},
  };
  for (auto& c : cases) {
    unsigned char buf[32];
    snprintf((char*)buf, sizeof(buf), "%s", c.in);
    TEST_ASSERT_EQUAL_UINT(c.out, decode_base64_length(buf, (unsigned)strlen(c.in)));
  }
}

static void rfc4648(void) {
  struct { const char* in; const char* out; } cases[] = {
    {"",       ""},
    {"f",      "Zg=="},
    {"fo",     "Zm8="},
    {"foo",    "Zm9v"},
    {"foob",   "Zm9vYg=="},
    {"fooba",  "Zm9vYmE="},
    {"foobar", "Zm9vYmFy"},
  };
  for (auto& c : cases) {
    unsigned char out[16];
    memset(out, 'x', sizeof(out));
    unsigned n = encode_base64((unsigned char*)c.in, (unsigned)strlen(c.in), out);
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(c.out), n);
    TEST_ASSERT_EQUAL_STRING(c.out, (const char*)out);
  }
}

static void round_trip(void) {
  unsigned char in[256];
  for (unsigned i = 0; i < 256; i++) in[i] = (unsigned char)i;

  unsigned char enc[400];
  unsigned n = encode_base64(in, sizeof(in), enc);
  TEST_ASSERT_EQUAL_UINT(344, n);

  unsigned char dec[256];
  TEST_ASSERT_EQUAL_UINT(256, decode_base64(enc, n, dec));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, 256);
}

static void decode_stops(void) {
  unsigned char in[] = "TWFu!!!!";
  unsigned char out[16];
  memset(out, 'x', sizeof(out));
  TEST_ASSERT_EQUAL_UINT(3, decode_base64(in, (unsigned)strlen((char*)in), out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY("Man", out, 3);
  TEST_ASSERT_EQUAL_UINT8('x', out[3]);
}

static void decode_empty(void) {
  unsigned char in[] = "";
  unsigned char out[4];
  memset(out, 'x', sizeof(out));
  TEST_ASSERT_EQUAL_UINT(0, decode_base64(in, 0, out));
  TEST_ASSERT_EQUAL_UINT8('x', out[0]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(alphabet);
  RUN_TEST(invalid_chars);
  RUN_TEST(encode_length);
  RUN_TEST(decode_length);
  RUN_TEST(rfc4648);
  RUN_TEST(round_trip);
  RUN_TEST(decode_stops);
  RUN_TEST(decode_empty);
  return UNITY_END();
}
