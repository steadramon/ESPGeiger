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

// wire format set by StationsAPI

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

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

static void map_widths(void) {
  { auto mp = w(); mp.map(0);  ASSERT_BYTES(mp, 0x80); }
  { auto mp = w(); mp.map(1);  ASSERT_BYTES(mp, 0x81); }
  { auto mp = w(); mp.map(15); ASSERT_BYTES(mp, 0x8f); }
  // 16: first map16
  { auto mp = w(); mp.map(16); ASSERT_BYTES(mp, 0xde, 0x00, 0x10); }
  { auto mp = w(); mp.map(65535); ASSERT_BYTES(mp, 0xde, 0xff, 0xff); }
}

static void array_widths(void) {
  { auto mp = w(); mp.array(0);  ASSERT_BYTES(mp, 0x90); }
  { auto mp = w(); mp.array(15); ASSERT_BYTES(mp, 0x9f); }
  { auto mp = w(); mp.array(16); ASSERT_BYTES(mp, 0xdc, 0x00, 0x10); }
}

static void nil_and_bool(void) {
  { auto mp = w(); mp.nil();      ASSERT_BYTES(mp, 0xc0); }
  { auto mp = w(); mp.b(true);    ASSERT_BYTES(mp, 0xc3); }
  { auto mp = w(); mp.b(false);   ASSERT_BYTES(mp, 0xc2); }
}

static void uint_narrowing(void) {
  { auto mp = w(); mp.uint(0);          ASSERT_BYTES(mp, 0x00); }
  { auto mp = w(); mp.uint(0x7f);       ASSERT_BYTES(mp, 0x7f); }
  { auto mp = w(); mp.uint(0x80);       ASSERT_BYTES(mp, 0xcc, 0x80); }
  { auto mp = w(); mp.uint(0x100);      ASSERT_BYTES(mp, 0xcd, 0x01, 0x00); }
  { auto mp = w(); mp.uint(0x10000);    ASSERT_BYTES(mp, 0xce, 0x00, 0x01, 0x00, 0x00); }
  { auto mp = w(); mp.uint(0xffffffff); ASSERT_BYTES(mp, 0xce, 0xff, 0xff, 0xff, 0xff); }
}

static void sint_narrowing(void) {
  // >= 0 goes through uint()
  { auto mp = w(); mp.sint(0);   ASSERT_BYTES(mp, 0x00); }
  { auto mp = w(); mp.sint(128); ASSERT_BYTES(mp, 0xcc, 0x80); }

  { auto mp = w(); mp.sint(-1);      ASSERT_BYTES(mp, 0xff); }
  { auto mp = w(); mp.sint(-32);     ASSERT_BYTES(mp, 0xe0); }
  { auto mp = w(); mp.sint(-33);     ASSERT_BYTES(mp, 0xd0, 0xdf); }
  { auto mp = w(); mp.sint(-129);    ASSERT_BYTES(mp, 0xd1, 0xff, 0x7f); }
  { auto mp = w(); mp.sint(-32769);  ASSERT_BYTES(mp, 0xd2, 0xff, 0xff, 0x7f, 0xff); }
  { auto mp = w(); mp.sint(INT32_MIN); ASSERT_BYTES(mp, 0xd2, 0x80, 0x00, 0x00, 0x00); }
}

static void f32_bits(void) {
  { auto mp = w(); mp.f32(1.0f);   ASSERT_BYTES(mp, 0xca, 0x3f, 0x80, 0x00, 0x00); }
  { auto mp = w(); mp.f32(-2.5f);  ASSERT_BYTES(mp, 0xca, 0xc0, 0x20, 0x00, 0x00); }
  { auto mp = w(); mp.f32(0.21f);  ASSERT_BYTES(mp, 0xca, 0x3e, 0x57, 0x0a, 0x3d); }
}

static void str_widths(void) {
  { auto mp = w(); mp.str("");    ASSERT_BYTES(mp, 0xa0); }
  { auto mp = w(); mp.str("cpm"); ASSERT_BYTES(mp, 0xa3, 'c', 'p', 'm'); }

  char s[300];
  memset(s, 'x', sizeof(s));

  // 31 fixstr, 32 str8
  {
    auto mp = w(); mp.str(s, 31);
    TEST_ASSERT_EQUAL_UINT8(0xbf, g_buf[0]);
    TEST_ASSERT_EQUAL_size_t(32, mp.length());
  }
  {
    auto mp = w(); mp.str(s, 32);
    TEST_ASSERT_EQUAL_UINT8(0xd9, g_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(32, g_buf[1]);
    TEST_ASSERT_EQUAL_size_t(34, mp.length());
  }
  {
    auto mp = w(); mp.str(s, 256);
    TEST_ASSERT_EQUAL_UINT8(0xda, g_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, g_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, g_buf[2]);
    TEST_ASSERT_EQUAL_size_t(259, mp.length());
  }
}

// kv() nil, str() empty
static void null_string(void) {
  { auto mp = w(); mp.str((const char*)nullptr); ASSERT_BYTES(mp, 0xa0); }
  { auto mp = w(); mp.kv("k", (const char*)nullptr); ASSERT_BYTES(mp, 0xa1, 'k', 0xc0); }
}

static void bin_widths(void) {
  uint8_t d[300];
  for (size_t i = 0; i < sizeof(d); i++) d[i] = (uint8_t)i;

  { auto mp = w(); mp.bin(d, 0); ASSERT_BYTES(mp, 0xc4, 0x00); }
  { auto mp = w(); mp.bin(d, 3); ASSERT_BYTES(mp, 0xc4, 0x03, 0x00, 0x01, 0x02); }
  {
    auto mp = w(); mp.bin(d, 256);
    TEST_ASSERT_EQUAL_UINT8(0xc5, g_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, g_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, g_buf[2]);
    TEST_ASSERT_EQUAL_size_t(259, mp.length());
  }
}

static void payload_golden(void) {
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

static void counted_map(void) {
  static const uint16_t SIZES[] = {0, 1, 15, 16, 20};
  for (uint16_t n : SIZES) {
    uint8_t ref[256];
    MsgPack::Writer r(ref, sizeof(ref));
    r.map(n);
    for (uint16_t i = 0; i < n; i++) r.kv("k", (uint32_t)i);

    auto mp = w();
    mp.map_begin();
    for (uint16_t i = 0; i < n; i++) mp.kv("k", (uint32_t)i);
    mp.map_end();

    char msg[16];
    snprintf(msg, sizeof(msg), "n=%u", n);
    TEST_ASSERT_FALSE_MESSAGE(mp.overflow, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(r.length(), mp.length(), msg);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(ref, g_buf, r.length(), msg);
  }
}

static void counted_full(void) {
  auto mp = w(1 + 16 * 3);   // fixmap header + 16 x {a1 'k' n}
  mp.map_begin();
  for (uint32_t i = 0; i < 16; i++) mp.kv("k", i);
  TEST_ASSERT_FALSE(mp.overflow);
  mp.map_end();
  TEST_ASSERT_TRUE(mp.overflow);

  auto mp2 = w(4);
  mp2.map_begin();
  mp2.kv("k", (uint32_t)1);
  mp2.kv("k", (uint32_t)2);
  mp2.map_end();
  TEST_ASSERT_TRUE(mp2.overflow);
  TEST_ASSERT_EQUAL_UINT8(0x80, g_buf[0]);
}

static void overflow_flags(void) {
  {
    auto mp = w(3);
    mp.uint(0x10000);                 // wants 5 bytes
    TEST_ASSERT_TRUE(mp.overflow);
    TEST_ASSERT_EQUAL_size_t(1, mp.length());   // tag only
  }
  {
    auto mp = w(4);
    mp.str("abcdef");
    TEST_ASSERT_TRUE(mp.overflow);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(4, mp.length());
  }
  { // exact fit
    auto mp = w(4);
    mp.str("abc");
    TEST_ASSERT_FALSE(mp.overflow);
    TEST_ASSERT_EQUAL_size_t(4, mp.length());
  }
  { // sticky
    auto mp = w(2);
    mp.uint(0x10000);
    TEST_ASSERT_TRUE(mp.overflow);
    mp.nil();
    TEST_ASSERT_TRUE(mp.overflow);
  }
}

static void reader_scalars(void) {
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

static void find_key_nested(void) {
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

static void find_key_miss(void) {
  auto mp = w();
  mp.map(2);
  mp.kv("a", (int32_t)1);
  mp.kv("b", (int32_t)2);

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_FALSE(r.find_key("zz"));
  TEST_ASSERT_FALSE(r.error);
}

static void find_key_exact(void) {
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

static void reader_mismatch(void) {
  auto mp = w();
  mp.map(1);
  mp.kv("a", "not a number");

  MsgPack::Reader r(g_buf, mp.length());
  TEST_ASSERT_TRUE(r.find_key("a"));
  float f;
  TEST_ASSERT_FALSE(r.read_float(&f));
  TEST_ASSERT_TRUE(r.error);
}

static void reader_truncated(void) {
  auto mp = w();
  mp.map(2);
  mp.kv("aaa", (uint32_t)0x11223344);
  mp.kv("bbb", (uint32_t)0x55667788);
  size_t full = mp.length();

  {
    MsgPack::Reader r(g_buf, full - 1);
    uint32_t v = 0;
    TEST_ASSERT_TRUE(r.find_key("bbb"));
    TEST_ASSERT_FALSE(r.read_uint(&v));
    TEST_ASSERT_EQUAL_UINT32(0, v);
  }
  {
    MsgPack::Reader r(g_buf, 6);   // inside the first value
    TEST_ASSERT_FALSE(r.find_key("bbb"));
  }

  MsgPack::Reader r(g_buf, full);
  uint32_t v = 0;
  TEST_ASSERT_TRUE(r.find_key("bbb"));
  TEST_ASSERT_TRUE(r.read_uint(&v));
  TEST_ASSERT_EQUAL_UINT32(0x55667788u, v);
}

static void uint_negative(void) {
  int32_t negatives[] = { -1, -33, -129, -32769 };
  for (int32_t n : negatives) {
    auto mp = w();
    mp.map(1);
    mp.kv("n", n);

    MsgPack::Reader r(g_buf, mp.length());
    uint32_t out = 7;
    TEST_ASSERT_TRUE(r.find_key("n"));
    TEST_ASSERT_FALSE(r.read_uint(&out));
    TEST_ASSERT_TRUE(r.error);
    TEST_ASSERT_EQUAL_UINT32(7, out);
  }
}

static void uint_range(void) {
  uint32_t values[] = { 0u, 128u, 256u, 65536u, 0xFFFFFFFFu };
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

static void read_str_cap(void) {
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

// rybakit default, float64
static void find_key_f64(void) {
  const uint8_t b[] = {0x82, 0xa1, 'v', 0xcb, 0x3f, 0xf8, 0, 0, 0, 0, 0, 0,
                       0xa2, 'i', 'd', 0x07};
  MsgPack::Reader r(b, sizeof(b));
  TEST_ASSERT_TRUE(r.find_key("id"));
  uint32_t id = 0;
  TEST_ASSERT_TRUE(r.read_uint(&id));
  TEST_ASSERT_EQUAL_UINT32(7, id);
}

static void deep_nesting(void) {
  uint8_t b[40];
  memset(b, 0x91, sizeof(b));   // fixarray of one, nested 40 deep
  b[sizeof(b) - 1] = 0x00;
  MsgPack::Reader r(b, sizeof(b));
  TEST_ASSERT_FALSE(r.skip());
  TEST_ASSERT_TRUE(r.error);
}

static void huge_lengths(void) {
  const uint8_t str32[] = {0xdb, 0xff, 0xff, 0xff, 0xff, 'x'};
  const uint8_t ext32[] = {0xc9, 0xff, 0xff, 0xff, 0xff, 0x01, 'x'};
  const uint8_t arr32[] = {0xdd, 0xff, 0xff, 0xff, 0xff, 0x00};
  char out[8];
  MsgPack::Reader a(str32, sizeof(str32));
  TEST_ASSERT_FALSE(a.read_str(out, sizeof(out)));
  MsgPack::Reader b(ext32, sizeof(ext32));
  TEST_ASSERT_FALSE(b.skip());
  MsgPack::Reader c(arr32, sizeof(arr32));
  TEST_ASSERT_FALSE(c.skip());
  const uint8_t map[] = {0x81, 0xdb, 0xff, 0xff, 0xff, 0xff, 'k'};
  MsgPack::Reader d(map, sizeof(map));
  TEST_ASSERT_FALSE(d.find_key("k"));
  TEST_ASSERT_TRUE(d.error);
}

static void int_over_max(void) {
  const uint8_t big[] = {0xce, 0xff, 0xff, 0xff, 0xff};
  const uint8_t max[] = {0xce, 0x7f, 0xff, 0xff, 0xff};
  int32_t v = 0;
  MsgPack::Reader a(big, sizeof(big));
  TEST_ASSERT_FALSE(a.read_int(&v));
  MsgPack::Reader b(max, sizeof(max));
  TEST_ASSERT_TRUE(b.read_int(&v));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, v);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(map_widths);
  RUN_TEST(array_widths);
  RUN_TEST(nil_and_bool);
  RUN_TEST(uint_narrowing);
  RUN_TEST(sint_narrowing);
  RUN_TEST(f32_bits);
  RUN_TEST(str_widths);
  RUN_TEST(null_string);
  RUN_TEST(bin_widths);
  RUN_TEST(payload_golden);
  RUN_TEST(counted_map);
  RUN_TEST(counted_full);
  RUN_TEST(overflow_flags);
  RUN_TEST(reader_scalars);
  RUN_TEST(find_key_nested);
  RUN_TEST(find_key_miss);
  RUN_TEST(find_key_exact);
  RUN_TEST(reader_mismatch);
  RUN_TEST(reader_truncated);
  RUN_TEST(uint_negative);
  RUN_TEST(uint_range);
  RUN_TEST(read_str_cap);
  RUN_TEST(find_key_f64);
  RUN_TEST(deep_nesting);
  RUN_TEST(huge_lengths);
  RUN_TEST(int_over_max);
  return UNITY_END();
}
