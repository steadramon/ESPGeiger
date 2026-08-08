/*
  test_msgpack - MsgPack::Writer / MsgPack::Reader.

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

// Golden bytes, not round trips. The encoder is half a contract with
// StationsAPI; a round trip against our own decoder stays green through a
// change that breaks the server.

#include <unity.h>
#include <string.h>
#include <stdint.h>

#include "WebAPI/MsgPack.h"

void setUp(void)    {}
void tearDown(void) {}

static uint8_t g_buf[1024];

static MsgPack::Writer w(size_t cap = sizeof(g_buf)) {
  memset(g_buf, 0xEE, sizeof(g_buf));
  return MsgPack::Writer(g_buf, cap);
}

#define ASSERT_BYTES(mp, ...) do {                                    \
    const uint8_t expect[] = { __VA_ARGS__ };                         \
    TEST_ASSERT_FALSE_MESSAGE((mp).overflow, "unexpected overflow");  \
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), (mp).length());          \
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, g_buf, sizeof(expect));     \
  } while (0)

// --- containers -------------------------------------------------------------

static void test_map_header_widths(void) {
  { auto mp = w(); mp.map(0);  ASSERT_BYTES(mp, 0x80); }
  { auto mp = w(); mp.map(1);  ASSERT_BYTES(mp, 0x81); }
  { auto mp = w(); mp.map(15); ASSERT_BYTES(mp, 0x8f); }
  // 16 is the fixmap boundary: map16 header, big-endian count.
  { auto mp = w(); mp.map(16); ASSERT_BYTES(mp, 0xde, 0x00, 0x10); }
  { auto mp = w(); mp.map(65535); ASSERT_BYTES(mp, 0xde, 0xff, 0xff); }
}

static void test_array_header_widths(void) {
  { auto mp = w(); mp.array(0);  ASSERT_BYTES(mp, 0x90); }
  { auto mp = w(); mp.array(15); ASSERT_BYTES(mp, 0x9f); }
  { auto mp = w(); mp.array(16); ASSERT_BYTES(mp, 0xdc, 0x00, 0x10); }
}

// --- scalars ----------------------------------------------------------------

static void test_nil_and_bool(void) {
  { auto mp = w(); mp.nil();      ASSERT_BYTES(mp, 0xc0); }
  { auto mp = w(); mp.b(true);    ASSERT_BYTES(mp, 0xc3); }
  { auto mp = w(); mp.b(false);   ASSERT_BYTES(mp, 0xc2); }
}

// Every narrowing boundary of uint(), on both sides.
static void test_uint_narrowing_boundaries(void) {
  { auto mp = w(); mp.uint(0);          ASSERT_BYTES(mp, 0x00); }
  { auto mp = w(); mp.uint(0x7f);       ASSERT_BYTES(mp, 0x7f); }
  { auto mp = w(); mp.uint(0x80);       ASSERT_BYTES(mp, 0xcc, 0x80); }
  { auto mp = w(); mp.uint(0xff);       ASSERT_BYTES(mp, 0xcc, 0xff); }
  { auto mp = w(); mp.uint(0x100);      ASSERT_BYTES(mp, 0xcd, 0x01, 0x00); }
  { auto mp = w(); mp.uint(0xffff);     ASSERT_BYTES(mp, 0xcd, 0xff, 0xff); }
  { auto mp = w(); mp.uint(0x10000);    ASSERT_BYTES(mp, 0xce, 0x00, 0x01, 0x00, 0x00); }
  { auto mp = w(); mp.uint(0xffffffff); ASSERT_BYTES(mp, 0xce, 0xff, 0xff, 0xff, 0xff); }
}

static void test_sint_narrowing_boundaries(void) {
  // Non-negative goes through uint(), so it never emits an int tag.
  { auto mp = w(); mp.sint(0);   ASSERT_BYTES(mp, 0x00); }
  { auto mp = w(); mp.sint(127); ASSERT_BYTES(mp, 0x7f); }
  { auto mp = w(); mp.sint(128); ASSERT_BYTES(mp, 0xcc, 0x80); }

  { auto mp = w(); mp.sint(-1);      ASSERT_BYTES(mp, 0xff); }
  { auto mp = w(); mp.sint(-32);     ASSERT_BYTES(mp, 0xe0); }
  { auto mp = w(); mp.sint(-33);     ASSERT_BYTES(mp, 0xd0, 0xdf); }
  { auto mp = w(); mp.sint(-128);    ASSERT_BYTES(mp, 0xd0, 0x80); }
  { auto mp = w(); mp.sint(-129);    ASSERT_BYTES(mp, 0xd1, 0xff, 0x7f); }
  { auto mp = w(); mp.sint(-32768);  ASSERT_BYTES(mp, 0xd1, 0x80, 0x00); }
  { auto mp = w(); mp.sint(-32769);  ASSERT_BYTES(mp, 0xd2, 0xff, 0xff, 0x7f, 0xff); }
  { auto mp = w(); mp.sint(INT32_MIN); ASSERT_BYTES(mp, 0xd2, 0x80, 0x00, 0x00, 0x00); }
}

// float32 goes out as the raw IEEE-754 bit pattern, big-endian. Pinning the
// bytes catches an endian or width slip that a float compare would not.
static void test_f32_bit_patterns(void) {
  { auto mp = w(); mp.f32(0.0f);   ASSERT_BYTES(mp, 0xca, 0x00, 0x00, 0x00, 0x00); }
  { auto mp = w(); mp.f32(1.0f);   ASSERT_BYTES(mp, 0xca, 0x3f, 0x80, 0x00, 0x00); }
  { auto mp = w(); mp.f32(-2.5f);  ASSERT_BYTES(mp, 0xca, 0xc0, 0x20, 0x00, 0x00); }
  { auto mp = w(); mp.f32(0.21f);  ASSERT_BYTES(mp, 0xca, 0x3e, 0x57, 0x0a, 0x3d); }
}

// --- strings and binary -----------------------------------------------------

static void test_str_widths(void) {
  { auto mp = w(); mp.str("");    ASSERT_BYTES(mp, 0xa0); }
  { auto mp = w(); mp.str("cpm"); ASSERT_BYTES(mp, 0xa3, 'c', 'p', 'm'); }

  char s[300];
  memset(s, 'x', sizeof(s));

  // 31 is the fixstr ceiling, 32 the first str8.
  { auto mp = w(); mp.str(s, 31); TEST_ASSERT_EQUAL_UINT8(0xbf, g_buf[0]); TEST_ASSERT_EQUAL_size_t(32, mp.length()); }
  { auto mp = w(); mp.str(s, 32); TEST_ASSERT_EQUAL_UINT8(0xd9, g_buf[0]); TEST_ASSERT_EQUAL_UINT8(32, g_buf[1]); TEST_ASSERT_EQUAL_size_t(34, mp.length()); }
  { auto mp = w(); mp.str(s, 255); TEST_ASSERT_EQUAL_UINT8(0xd9, g_buf[0]); TEST_ASSERT_EQUAL_UINT8(0xff, g_buf[1]); TEST_ASSERT_EQUAL_size_t(257, mp.length()); }
  { auto mp = w(); mp.str(s, 256); TEST_ASSERT_EQUAL_UINT8(0xda, g_buf[0]); TEST_ASSERT_EQUAL_UINT8(0x01, g_buf[1]); TEST_ASSERT_EQUAL_UINT8(0x00, g_buf[2]); TEST_ASSERT_EQUAL_size_t(259, mp.length()); }
}

// A null char* is written as nil by kv(), but as an empty string by str().
static void test_null_string_handling(void) {
  { auto mp = w(); mp.str((const char*)nullptr); ASSERT_BYTES(mp, 0xa0); }
  { auto mp = w(); mp.kv("k", (const char*)nullptr); ASSERT_BYTES(mp, 0xa1, 'k', 0xc0); }
}

static void test_bin_widths(void) {
  uint8_t d[300];
  for (size_t i = 0; i < sizeof(d); i++) d[i] = (uint8_t)i;

  // bin has no fix form: even an empty payload carries a bin8 header.
  { auto mp = w(); mp.bin(d, 0); ASSERT_BYTES(mp, 0xc4, 0x00); }
  { auto mp = w(); mp.bin(d, 3); ASSERT_BYTES(mp, 0xc4, 0x03, 0x00, 0x01, 0x02); }
  { auto mp = w(); mp.bin(d, 255); TEST_ASSERT_EQUAL_UINT8(0xc4, g_buf[0]); TEST_ASSERT_EQUAL_UINT8(0xff, g_buf[1]); TEST_ASSERT_EQUAL_size_t(257, mp.length()); }
  { auto mp = w(); mp.bin(d, 256); TEST_ASSERT_EQUAL_UINT8(0xc5, g_buf[0]); TEST_ASSERT_EQUAL_UINT8(0x01, g_buf[1]); TEST_ASSERT_EQUAL_UINT8(0x00, g_buf[2]); TEST_ASSERT_EQUAL_size_t(259, mp.length()); }
}

// --- a whole payload --------------------------------------------------------

// The shape a telemetry post takes: fixmap, short keys, narrowed ints, a
// float32 and a signature blob. Byte-for-byte so a key rename or a width
// change is a test failure rather than a server-side surprise.
static void test_payload_golden_bytes(void) {
  const uint8_t sig[4] = { 0xde, 0xad, 0xbe, 0xef };
  auto mp = w();
  mp.map(4);
  mp.kv("cid", (uint32_t)0x00c0ffee);
  mp.kv("cpm", (int32_t)42);
  mp.kv("usv", 0.21f);
  mp.kv("sig", sig, sizeof(sig));

  ASSERT_BYTES(mp,
    0x84,
    0xa3, 'c', 'i', 'd', 0xce, 0x00, 0xc0, 0xff, 0xee,
    0xa3, 'c', 'p', 'm', 0x2a,
    0xa3, 'u', 's', 'v', 0xca, 0x3e, 0x57, 0x0a, 0x3d,
    0xa3, 's', 'i', 'g', 0xc4, 0x04, 0xde, 0xad, 0xbe, 0xef);
}

// --- overflow ---------------------------------------------------------------

// On overflow the writer must stop, not truncate-and-advance: a caller that
// ignores the flag and sends length() bytes must never send more than cap.
static void test_overflow_stops_short_and_flags(void) {
  { // multi-byte scalar that does not fit at all
    auto mp = w(3);
    mp.uint(0x10000);                 // wants 5 bytes
    TEST_ASSERT_TRUE(mp.overflow);
    TEST_ASSERT_EQUAL_size_t(1, mp.length());   // tag written, payload refused
    TEST_ASSERT_LESS_OR_EQUAL_size_t(3, mp.length());
  }
  { // string body that does not fit
    auto mp = w(4);
    mp.str("abcdef");
    TEST_ASSERT_TRUE(mp.overflow);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(4, mp.length());
  }
  { // exact fit is not an overflow
    auto mp = w(4);
    mp.str("abc");
    TEST_ASSERT_FALSE(mp.overflow);
    TEST_ASSERT_EQUAL_size_t(4, mp.length());
  }
  { // once flagged, the flag is sticky across later successful writes
    auto mp = w(2);
    mp.uint(0x10000);
    TEST_ASSERT_TRUE(mp.overflow);
    mp.nil();
    TEST_ASSERT_TRUE(mp.overflow);
  }
}

// --- reader -----------------------------------------------------------------

static void test_reader_round_trips_scalars(void) {
  auto mp = w();
  mp.map(5);
  mp.kv("a", (int32_t)-40000);
  mp.kv("b", (uint32_t)0xFFFFFFFF);
  mp.kv("c", 1.5f);
  mp.kv("d", true);
  mp.kv("e", "hello");
  TEST_ASSERT_FALSE(mp.overflow);

  { MsgPack::Reader r(g_buf, mp.length()); int32_t v;
    TEST_ASSERT_TRUE(r.find_key("a")); TEST_ASSERT_TRUE(r.read_int(&v));
    TEST_ASSERT_EQUAL_INT32(-40000, v); }
  { MsgPack::Reader r(g_buf, mp.length()); uint32_t v;
    TEST_ASSERT_TRUE(r.find_key("b")); TEST_ASSERT_TRUE(r.read_uint(&v));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, v); }
  { MsgPack::Reader r(g_buf, mp.length()); float v;
    TEST_ASSERT_TRUE(r.find_key("c")); TEST_ASSERT_TRUE(r.read_float(&v));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, v); }
  { MsgPack::Reader r(g_buf, mp.length()); bool v = false;
    TEST_ASSERT_TRUE(r.find_key("d")); TEST_ASSERT_TRUE(r.read_bool(&v));
    TEST_ASSERT_TRUE(v); }
  { MsgPack::Reader r(g_buf, mp.length()); char s[16];
    TEST_ASSERT_TRUE(r.find_key("e")); TEST_ASSERT_TRUE(r.read_str(s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("hello", s); }
}

// find_key has to step over whatever type sits under the keys it rejects,
// including nested containers.
static void test_find_key_skips_nested_values(void) {
  auto mp = w();
  mp.map(3);
  mp.key("skipme"); mp.map(2); mp.kv("x", (int32_t)1); mp.kv("y", "deep");
  mp.key("arr");    mp.array(3); mp.uint(1); mp.f32(2.0f); mp.str("three");
  mp.kv("want", (int32_t)99);
  TEST_ASSERT_FALSE(mp.overflow);

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_TRUE(r.find_key("want"));
  int32_t v;
  TEST_ASSERT_TRUE(r.read_int(&v));
  TEST_ASSERT_EQUAL_INT32(99, v);
}

static void test_find_key_miss_is_not_an_error(void) {
  auto mp = w();
  mp.map(2);
  mp.kv("a", (int32_t)1);
  mp.kv("b", (int32_t)2);

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_FALSE(r.find_key("zz"));
  TEST_ASSERT_FALSE(r.error);
}

// A key whose name is a prefix of the wanted one must not match.
static void test_find_key_is_length_exact(void) {
  auto mp = w();
  mp.map(2);
  mp.kv("cp", (int32_t)1);
  mp.kv("cpm", (int32_t)2);

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_TRUE(r.find_key("cpm"));
  int32_t v;
  TEST_ASSERT_TRUE(r.read_int(&v));
  TEST_ASSERT_EQUAL_INT32(2, v);
}

static void test_reader_rejects_type_mismatch(void) {
  auto mp = w();
  mp.map(1);
  mp.kv("a", "not a number");

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_TRUE(r.find_key("a"));
  float f;
  TEST_ASSERT_FALSE(r.read_float(&f));
  TEST_ASSERT_TRUE(r.error);
}

// Truncation must fail closed. Every prefix short of the whole payload has to
// refuse, never hand back a half-read value.
static void test_reader_handles_truncated_input(void) {
  auto mp = w();
  mp.map(2);
  mp.kv("aaa", (uint32_t)0x11223344);
  mp.kv("bbb", (uint32_t)0x55667788);
  size_t full = mp.length();

  for (size_t len = 0; len < full; len++) {
    MsgPack::Reader r(g_buf, len);
    uint32_t v = 0xA5A5A5A5u;
    bool got = r.find_key("bbb") && r.read_uint(&v);
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_EQUAL_UINT32(0xA5A5A5A5u, v);
  }

  MsgPack::Reader r(g_buf, full);
  uint32_t v = 0;
  TEST_ASSERT_TRUE(r.find_key("bbb"));
  TEST_ASSERT_TRUE(r.read_uint(&v));
  TEST_ASSERT_EQUAL_UINT32(0x55667788u, v);
}

// REGRESSION. find_key used to add a declared key length to `pos` before
// bounds-checking it, so a failed lookup on a truncated buffer left the public
// `pos` past the end and `buf + r.pos` was an out-of-range pointer. The guard
// now runs before the add: pos must never leave the buffer, whatever the input.
static void test_find_key_pos_never_leaves_the_buffer(void) {
  auto mp = w();
  mp.map(1);
  mp.kv("aaa", (uint32_t)1);
  size_t full = mp.length();

  for (size_t len = 0; len <= full; len++) {
    MsgPack::Reader r(g_buf, len);
    r.find_key("bbb");
    TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(r.cap, r.pos, "pos ran past cap");
    MsgPack::Reader r2(g_buf, len);
    r2.find_key("aaa");
    TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(r2.cap, r2.pos, "pos ran past cap");
  }
}

// REGRESSION. read_uint used to route through read_int, so an encoded -1 came
// back as 4294967295 with no error. A peer could turn a negative into a huge
// unsigned. Negatives must now be refused.
static void test_read_uint_rejects_negatives(void) {
  struct { const char* label; int32_t v; } negatives[] = {
    { "neg fixint", -1 }, { "neg fixint low", -32 },
    { "int8",  -33 }, { "int8 min", -128 },
    { "int16", -129 }, { "int16 min", -32768 },
    { "int32", -32769 }, { "int32 min", INT32_MIN },
  };
  for (auto& n : negatives) {
    auto mp = w();
    mp.map(1);
    mp.kv("n", n.v);

    MsgPack::Reader r(g_buf, mp.length());
    uint32_t out = 0xA5A5A5A5u;
    TEST_ASSERT_TRUE(r.find_key("n"));
    TEST_ASSERT_FALSE_MESSAGE(r.read_uint(&out), n.label);
    TEST_ASSERT_TRUE_MESSAGE(r.error, n.label);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xA5A5A5A5u, out, n.label);
  }
}

// ...while genuinely unsigned values keep their full 32-bit range. This is the
// case a naive `if (s < 0) reject` after read_int would have broken.
static void test_read_uint_keeps_full_unsigned_range(void) {
  uint32_t values[] = { 0u, 1u, 127u, 128u, 255u, 256u, 65535u, 65536u,
                        0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };
  for (uint32_t v : values) {
    auto mp = w();
    mp.map(1);
    mp.kv("n", v);

    MsgPack::Reader r(g_buf, mp.length());
    uint32_t out = 0;
    TEST_ASSERT_TRUE(r.find_key("n"));
    TEST_ASSERT_TRUE(r.read_uint(&out));
    TEST_ASSERT_FALSE(r.error);
    TEST_ASSERT_EQUAL_UINT32(v, out);
  }
}

static void test_read_str_respects_output_capacity(void) {
  auto mp = w();
  mp.map(1);
  mp.kv("s", "abcdef");   // 6 chars, needs 7 bytes out

  { MsgPack::Reader r(g_buf, mp.length()); char s[7];
    TEST_ASSERT_TRUE(r.find_key("s"));
    TEST_ASSERT_TRUE(r.read_str(s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("abcdef", s); }
  { MsgPack::Reader r(g_buf, mp.length()); char s[6];
    TEST_ASSERT_TRUE(r.find_key("s"));
    TEST_ASSERT_FALSE(r.read_str(s, sizeof(s)));   // no room for the NUL
    TEST_ASSERT_TRUE(r.error); }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_map_header_widths);
  RUN_TEST(test_array_header_widths);
  RUN_TEST(test_nil_and_bool);
  RUN_TEST(test_uint_narrowing_boundaries);
  RUN_TEST(test_sint_narrowing_boundaries);
  RUN_TEST(test_f32_bit_patterns);
  RUN_TEST(test_str_widths);
  RUN_TEST(test_null_string_handling);
  RUN_TEST(test_bin_widths);
  RUN_TEST(test_payload_golden_bytes);
  RUN_TEST(test_overflow_stops_short_and_flags);
  RUN_TEST(test_reader_round_trips_scalars);
  RUN_TEST(test_find_key_skips_nested_values);
  RUN_TEST(test_find_key_miss_is_not_an_error);
  RUN_TEST(test_find_key_is_length_exact);
  RUN_TEST(test_reader_rejects_type_mismatch);
  RUN_TEST(test_reader_handles_truncated_input);
  RUN_TEST(test_find_key_pos_never_leaves_the_buffer);
  RUN_TEST(test_read_uint_rejects_negatives);
  RUN_TEST(test_read_uint_keeps_full_unsigned_range);
  RUN_TEST(test_read_str_respects_output_capacity);
  return UNITY_END();
}
