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
#include <string.h>
#include <stdint.h>

#include "../../lib/EGBase64/src/EGBase64.h"

void setUp(void)    {}
void tearDown(void) {}

// Poisoned buffer so an over-write past the contract is visible. 0x5A is not
// a base64 character, so a stray one cannot be mistaken for output.
static const unsigned char POISON = 0x5A;

struct EncBuf {
  unsigned char b[128];
  EncBuf() { memset(b, POISON, sizeof(b)); }
};

// --- alphabet ---------------------------------------------------------------

static void test_alphabet_round_trips(void) {
  for (unsigned v = 0; v < 64; v++) {
    unsigned char c = binary_to_base64((unsigned char)v);
    TEST_ASSERT_EQUAL_UINT8(v, base64_to_binary(c));
  }
  TEST_ASSERT_EQUAL_UINT8('A', binary_to_base64(0));
  TEST_ASSERT_EQUAL_UINT8('Z', binary_to_base64(25));
  TEST_ASSERT_EQUAL_UINT8('a', binary_to_base64(26));
  TEST_ASSERT_EQUAL_UINT8('z', binary_to_base64(51));
  TEST_ASSERT_EQUAL_UINT8('0', binary_to_base64(52));
  TEST_ASSERT_EQUAL_UINT8('9', binary_to_base64(61));
  TEST_ASSERT_EQUAL_UINT8('+', binary_to_base64(62));
  TEST_ASSERT_EQUAL_UINT8('/', binary_to_base64(63));
  // Out of range in, sentinel out.
  TEST_ASSERT_EQUAL_UINT8(64, binary_to_base64(64));
}

static void test_invalid_decode_chars_are_255(void) {
  const char* bad = "-_ \t\n!@#$%^&*()[]{}";
  for (const char* p = bad; *p; p++) {
    TEST_ASSERT_EQUAL_UINT8(255, base64_to_binary((unsigned char)*p));
  }
  // '=' is padding, not data, and decodes to the same sentinel.
  TEST_ASSERT_EQUAL_UINT8(255, base64_to_binary('='));
}

// --- length -----------------------------------------------------------------

static void test_encode_length(void) {
  struct { unsigned in, out; } cases[] = {
    {0, 0}, {1, 4}, {2, 4}, {3, 4}, {4, 8}, {5, 8}, {6, 8},
    {32, 44}, {48, 64}, {64, 88}, {65, 88}, {192, 256},
  };
  for (auto& c : cases) TEST_ASSERT_EQUAL_UINT(c.out, encode_base64_length(c.in));
}

static void test_decode_length(void) {
  struct { const char* in; unsigned out; } cases[] = {
    {"",         0},
    {"TQ==",     1},
    {"TWE=",     2},
    {"TWFu",     3},
    {"TWFuTQ==", 4},
    {"Zm9vYmFy", 6},
  };
  for (auto& c : cases) {
    unsigned char buf[32];
    strcpy((char*)buf, c.in);
    TEST_ASSERT_EQUAL_UINT(c.out, decode_base64_length(buf, (unsigned)strlen(c.in)));
  }
}

// --- RFC 4648 vectors -------------------------------------------------------

static void test_rfc4648_vectors(void) {
  struct { const char* in; const char* out; } cases[] = {
    {"",       ""},
    {"f",      "Zg=="},
    {"fo",     "Zm8="},
    {"foo",    "Zm9v"},
    {"foob",   "Zm9vYg=="},
    {"fooba",  "Zm9vYmE="},
    {"foobar", "Zm9vYmFy"},
    {"Man",    "TWFu"},
    {"Ma",     "TWE="},
    {"M",      "TQ=="},
  };
  for (auto& c : cases) {
    EncBuf e;
    unsigned n = encode_base64((unsigned char*)c.in, (unsigned)strlen(c.in), e.b);
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(c.out), n);
    TEST_ASSERT_EQUAL_STRING(c.out, (const char*)e.b);
  }
}

// --- round trip -------------------------------------------------------------

static void test_round_trip_all_lengths(void) {
  unsigned char in[64];
  for (unsigned i = 0; i < sizeof(in); i++) in[i] = (unsigned char)(i * 7 + 3);

  for (unsigned len = 0; len <= sizeof(in); len++) {
    EncBuf e;
    unsigned enc = encode_base64(in, len, e.b);
    TEST_ASSERT_EQUAL_UINT(encode_base64_length(len), enc);

    unsigned char dec[80];
    memset(dec, POISON, sizeof(dec));
    unsigned n = decode_base64(e.b, enc, dec);
    TEST_ASSERT_EQUAL_UINT(len, n);
    if (len) TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, len);
    TEST_ASSERT_EQUAL_UINT8(POISON, dec[len]);
  }
}

// Every byte value survives, not just the friendly ASCII ones.
static void test_round_trip_full_byte_range(void) {
  unsigned char in[256];
  for (unsigned i = 0; i < 256; i++) in[i] = (unsigned char)i;

  unsigned char enc[512];
  memset(enc, POISON, sizeof(enc));
  unsigned n = encode_base64(in, sizeof(in), enc);
  TEST_ASSERT_EQUAL_UINT(344, n);

  unsigned char dec[256];
  TEST_ASSERT_EQUAL_UINT(256, decode_base64(enc, n, dec));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, 256);
}

// --- the NUL-terminator regression ------------------------------------------

// encode_base64 returns 4*ceil(N/3) but WRITES that many bytes plus a NUL.
// A streaming caller that reserves only the return value, then encodes the
// next chunk at buf + return, has the NUL land on the previous chunk's last
// character. The contract is return + 1; this pins it for every remainder
// class, including the 1-and-2-mod-3 tails where the tail block writes 5 bytes
// from the block start rather than 4.
static void test_encode_writes_exactly_length_plus_nul(void) {
  unsigned char in[16];
  for (unsigned i = 0; i < sizeof(in); i++) in[i] = (unsigned char)(0xA0 + i);

  for (unsigned len = 0; len <= sizeof(in); len++) {
    EncBuf e;
    unsigned n = encode_base64(in, len, e.b);

    TEST_ASSERT_EQUAL_UINT8('\0', e.b[n]);       // the +1 byte
    TEST_ASSERT_EQUAL_UINT8(POISON, e.b[n + 1]); // and not one byte more

    for (unsigned i = 0; i < n; i++) {
      TEST_ASSERT_NOT_EQUAL_UINT8(POISON, e.b[i]);
    }
  }
}

// The concrete shape of the bug: back-to-back encodes into one rolling buffer
// at buf + return_value corrupt the tail of the previous chunk.
static void test_streaming_encode_needs_scratch_or_room_for_nul(void) {
  unsigned char in[3] = { 'M', 'a', 'n' };

  unsigned char rolling[16];
  memset(rolling, POISON, sizeof(rolling));
  unsigned p = 0;
  p += encode_base64(in, 3, rolling + p);   // "TWFu\0", p = 4
  p += encode_base64(in, 3, rolling + p);   // starts at 4, overwrites the NUL
  TEST_ASSERT_EQUAL_UINT(8, p);
  TEST_ASSERT_EQUAL_STRING("TWFuTWFu", (const char*)rolling);

  // The failing shape is a partial tail: its NUL lands inside the next chunk's
  // first character if the caller advances by the return value alone.
  unsigned char one[1] = { 'M' };
  memset(rolling, POISON, sizeof(rolling));
  p = 0;
  p += encode_base64(one, 1, rolling + p);  // "TQ==\0", p = 4
  TEST_ASSERT_EQUAL_UINT8('\0', rolling[4]);
  encode_base64(in, 3, rolling + p);        // clobbers rolling[4] onwards
  TEST_ASSERT_EQUAL_STRING("TQ==TWFu", (const char*)rolling);
}

// --- decode edges -----------------------------------------------------------

static void test_decode_stops_at_first_invalid_char(void) {
  unsigned char in[] = "TWFu!!!!";
  unsigned char out[16];
  memset(out, POISON, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(3, decode_base64(in, (unsigned)strlen((char*)in), out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY("Man", out, 3);
  TEST_ASSERT_EQUAL_UINT8(POISON, out[3]);
}

static void test_decode_empty(void) {
  unsigned char in[] = "";
  unsigned char out[4];
  memset(out, POISON, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(0, decode_base64(in, 0, out));
  TEST_ASSERT_EQUAL_UINT8(POISON, out[0]);
}

// A NUL-terminated buffer decodes without an explicit length: the single-arg
// overload scans to the first non-alphabet byte, which the terminator is.
static void test_decode_without_explicit_length(void) {
  unsigned char in[] = "Zm9vYmFy";
  unsigned char out[16];
  memset(out, POISON, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(6, decode_base64(in, out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY("foobar", out, 6);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_alphabet_round_trips);
  RUN_TEST(test_invalid_decode_chars_are_255);
  RUN_TEST(test_encode_length);
  RUN_TEST(test_decode_length);
  RUN_TEST(test_rfc4648_vectors);
  RUN_TEST(test_round_trip_all_lengths);
  RUN_TEST(test_round_trip_full_byte_range);
  RUN_TEST(test_encode_writes_exactly_length_plus_nul);
  RUN_TEST(test_streaming_encode_needs_scratch_or_room_for_nul);
  RUN_TEST(test_decode_stops_at_first_invalid_char);
  RUN_TEST(test_decode_empty);
  RUN_TEST(test_decode_without_explicit_length);
  return UNITY_END();
}
