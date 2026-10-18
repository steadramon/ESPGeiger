/*
  test_grng - the GRNG entropy pool and its extraction paths.

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

// not entropy, use PractRand on a board

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <set>

#include "GRNG/GRNG.h"

static uint32_t s_hw_counter = 0;
static uint32_t hw_counting()  { return ++s_hw_counter; }
static uint32_t hw_stuck_zero() { return 0; }
static uint32_t hw_stuck_ones() { return 0xFFFFFFFFu; }

void setUp(void) {
  eg_clock_reset();
  s_hw_counter = 0;
  eg_set_hw_word(hw_counting);
}
void tearDown(void) { eg_set_hw_word(nullptr); }

static bool all_zero(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) if (p[i]) return false;
  return true;
}

static void extract_fills(void) {
  for (size_t n : { (size_t)1, (size_t)7, (size_t)31, (size_t)32, (size_t)33, (size_t)64, (size_t)100 }) {
    uint8_t buf[160];
    memset(buf, 0xA5, sizeof(buf));
    GRNG::extract(buf, n);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xA5, buf[n], "extract wrote past its length");
  }
}

static void extract_advances(void) {
  uint8_t a[32], b[32];
  GRNG::extract(a, sizeof(a));
  GRNG::extract(b, sizeof(b));
  TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, 32) != 0, "two draws were identical");
}

static void stuck_hw(void) {
  eg_set_hw_word(hw_stuck_zero);
  uint8_t a[32], b[32], c[32];
  GRNG::extract(a, sizeof(a));
  GRNG::extract(b, sizeof(b));
  GRNG::extract(c, sizeof(c));
  TEST_ASSERT_TRUE(memcmp(a, b, 32) != 0);
  TEST_ASSERT_TRUE(memcmp(b, c, 32) != 0);
  TEST_ASSERT_FALSE(all_zero(a, 32));

  eg_set_hw_word(hw_stuck_ones);
  uint8_t d[32], e[32];
  GRNG::extract(d, sizeof(d));
  GRNG::extract(e, sizeof(e));
  TEST_ASSERT_TRUE(memcmp(d, e, 32) != 0);
}

static void hw_words(void) {
  uint8_t with_counter[32], with_zero[32];

  eg_set_hw_word(hw_counting);
  s_hw_counter = 0;
  GRNG::mix(0x11111111u);
  GRNG::extract(with_counter, sizeof(with_counter));

  eg_set_hw_word(hw_stuck_zero);
  GRNG::mix(0x11111111u);
  GRNG::extract(with_zero, sizeof(with_zero));

  TEST_ASSERT_TRUE(memcmp(with_counter, with_zero, 32) != 0);
}

static void mix_changes(void) {
  eg_set_hw_word(hw_stuck_zero);

  uint8_t a[32], b[32];
  GRNG::mix(0xDEADBEEFu);
  GRNG::extract(a, sizeof(a));

  GRNG::mix(0xDEADBEEFu ^ 1u);        // one bit
  GRNG::extract(b, sizeof(b));

  TEST_ASSERT_TRUE(memcmp(a, b, 32) != 0);
}

static void mix_pool(void) {
  eg_set_hw_word(hw_stuck_zero);

  uint8_t spread[32], same[32];
  for (uint32_t i = 0; i < 8; i++) GRNG::mix(0x100u + i);
  GRNG::extract(spread, sizeof(spread));

  for (uint32_t i = 0; i < 8; i++) GRNG::mix(0x100u);
  GRNG::extract(same, sizeof(same));

  TEST_ASSERT_TRUE(memcmp(spread, same, 32) != 0);
}

// 32 bytes, 4 a call: refill on 9th
static void fast_refills(void) {
  eg_set_hw_word(hw_counting);

  s_hw_counter = 0;
  uint32_t before = 0;
  for (int i = 0; i < 8; i++) { GRNG::fast_uint32(); }
  before = s_hw_counter;

  for (int i = 0; i < 8; i++) { GRNG::fast_uint32(); }
  uint32_t after = s_hw_counter;

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, after - before,
    "expected exactly one 4-word refill per 8 fast_uint32 calls");
}

static void fast_vary(void) {
  std::set<uint32_t> seen;
  for (int i = 0; i < 256; i++) seen.insert(GRNG::fast_uint32());
  TEST_ASSERT_GREATER_THAN_size_t(250, seen.size());
}

static void fast_fills(void) {
  for (size_t n : { (size_t)1, (size_t)3, (size_t)4, (size_t)5, (size_t)255, (size_t)256 }) {
    uint8_t buf[300];
    memset(buf, 0x5A, sizeof(buf));
    GRNG::extract_fast(buf, n);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x5A, buf[n], "extract_fast wrote past its length");
  }
}

// xorshift sticks at 0
static void xorshift_zero(void) {
  eg_set_hw_word(hw_stuck_zero);

  uint8_t buf[256];
  for (int round = 0; round < 64; round++) {
    memset(buf, 0, sizeof(buf));
    GRNG::extract_fast(buf, sizeof(buf));
    for (size_t off = 0; off + 32 <= sizeof(buf); off += 32) {
      TEST_ASSERT_FALSE_MESSAGE(all_zero(buf + off, 32),
        "all-zero 32-byte window in extract_fast output");
    }
  }
}

static void extract_fast_state(void) {
  eg_set_hw_word(hw_stuck_zero);
  uint8_t a[64], b[64];
  GRNG::extract_fast(a, sizeof(a));
  GRNG::extract_fast(b, sizeof(b));
  TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, sizeof(a)) != 0,
    "extract_fast repeated itself across calls");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(extract_fills);
  RUN_TEST(extract_advances);
  RUN_TEST(stuck_hw);
  RUN_TEST(hw_words);
  RUN_TEST(mix_changes);
  RUN_TEST(mix_pool);
  RUN_TEST(fast_refills);
  RUN_TEST(fast_vary);
  RUN_TEST(fast_fills);
  RUN_TEST(xorshift_zero);
  RUN_TEST(extract_fast_state);
  return UNITY_END();
}
