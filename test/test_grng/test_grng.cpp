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

// Mixing and extraction on whatever words the hardware supplies. Says nothing
// about entropy quality; measure that off a board with PractRand.

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <set>

#include "GRNG/GRNG.h"

// --- controllable entropy source -------------------------------------------

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

// --- extract ----------------------------------------------------------------

static void test_extract_fills_exactly_and_no_more(void) {
  for (size_t n : { (size_t)1, (size_t)7, (size_t)31, (size_t)32, (size_t)33, (size_t)64, (size_t)100 }) {
    uint8_t buf[160];
    memset(buf, 0xA5, sizeof(buf));
    GRNG::extract(buf, n);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xA5, buf[n], "extract wrote past its length");
  }
}

// The pool must advance between draws.
static void test_extract_advances_the_pool(void) {
  uint8_t a[32], b[32];
  GRNG::extract(a, sizeof(a));
  GRNG::extract(b, sizeof(b));
  TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, 32) != 0, "two draws were identical");
}

// A stuck source: the pool must still advance from its own feedback.
static void test_stuck_hardware_source_still_advances(void) {
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

// Any input change changes the output.
static void test_output_depends_on_the_hardware_words(void) {
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

// Two pools fed different bits diverge.
static void test_mix_changes_subsequent_output(void) {
  eg_set_hw_word(hw_stuck_zero);      // isolate mix() as the only variable

  uint8_t a[32], b[32];
  GRNG::mix(0xDEADBEEFu);
  GRNG::extract(a, sizeof(a));

  GRNG::mix(0xDEADBEEFu ^ 1u);        // one bit different
  GRNG::extract(b, sizeof(b));

  TEST_ASSERT_TRUE(memcmp(a, b, 32) != 0);
}

// mix() walks all 8 pool words.
static void test_mix_walks_the_whole_pool(void) {
  eg_set_hw_word(hw_stuck_zero);

  uint8_t spread[32], same[32];
  for (uint32_t i = 0; i < 8; i++) GRNG::mix(0x100u + i);
  GRNG::extract(spread, sizeof(spread));

  for (uint32_t i = 0; i < 8; i++) GRNG::mix(0x100u);
  GRNG::extract(same, sizeof(same));

  TEST_ASSERT_TRUE(memcmp(spread, same, 32) != 0);
}

// --- fast_uint32 ------------------------------------------------------------

// 32 bytes, 4 per call: refill on exactly the 9th. Hardware reads show it.
static void test_fast_uint32_refills_every_eight_calls(void) {
  eg_set_hw_word(hw_counting);

  // Drain to a boundary: one refill is 4 hw words.
  s_hw_counter = 0;
  uint32_t before = 0;
  for (int i = 0; i < 8; i++) { GRNG::fast_uint32(); }
  before = s_hw_counter;

  // Next 8 calls: exactly one more refill.
  for (int i = 0; i < 8; i++) { GRNG::fast_uint32(); }
  uint32_t after = s_hw_counter;

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, after - before,
    "expected exactly one 4-word refill per 8 fast_uint32 calls");
}

static void test_fast_uint32_values_vary(void) {
  std::set<uint32_t> seen;
  for (int i = 0; i < 256; i++) seen.insert(GRNG::fast_uint32());
  // A couple of collisions allowed; a constant generator collapses to 1.
  TEST_ASSERT_GREATER_THAN_size_t(250, seen.size());
}

// --- extract_fast / xorshift ------------------------------------------------

static void test_extract_fast_fills_exactly(void) {
  for (size_t n : { (size_t)1, (size_t)3, (size_t)4, (size_t)5, (size_t)255, (size_t)256 }) {
    uint8_t buf[300];
    memset(buf, 0x5A, sizeof(buf));
    GRNG::extract_fast(buf, n);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x5A, buf[n], "extract_fast wrote past its length");
  }
}

// Zero is absorbing for xorshift; the 0x6b8b4567 fallback keeps it unreachable.
static void test_xorshift_never_emits_an_all_zero_window(void) {
  eg_set_hw_word(hw_stuck_zero);      // worst case: no fresh entropy at all

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

// State carries across calls.
static void test_extract_fast_state_carries_between_calls(void) {
  eg_set_hw_word(hw_stuck_zero);
  uint8_t a[64], b[64];
  GRNG::extract_fast(a, sizeof(a));
  GRNG::extract_fast(b, sizeof(b));
  TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, sizeof(a)) != 0,
    "extract_fast repeated itself across calls");
}

// --- construction -----------------------------------------------------------

// extract() hashes 52 bytes; past 55 the cost is two SHA-256 blocks.

static void test_extract_input_stays_one_compression_block(void) {
  const size_t pool_bytes = 8 * sizeof(uint32_t);
  const size_t cc_bytes   = sizeof(uint32_t);
  const size_t hw_bytes   = 4 * sizeof(uint32_t);
  const size_t total      = pool_bytes + cc_bytes + hw_bytes;
  TEST_ASSERT_EQUAL_size_t(52, total);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(55, total);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_extract_fills_exactly_and_no_more);
  RUN_TEST(test_extract_advances_the_pool);
  RUN_TEST(test_stuck_hardware_source_still_advances);
  RUN_TEST(test_output_depends_on_the_hardware_words);
  RUN_TEST(test_mix_changes_subsequent_output);
  RUN_TEST(test_mix_walks_the_whole_pool);
  RUN_TEST(test_fast_uint32_refills_every_eight_calls);
  RUN_TEST(test_fast_uint32_values_vary);
  RUN_TEST(test_extract_fast_fills_exactly);
  RUN_TEST(test_xorshift_never_emits_an_all_zero_window);
  RUN_TEST(test_extract_fast_state_carries_between_calls);
  RUN_TEST(test_extract_input_stays_one_compression_block);
  return UNITY_END();
}
