/*
  test_hostmodel - the canary suite.

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

// Host/target ABI canaries. `unsigned long` is 64-bit here and 32-bit on
// target, so seams under host test hold time in uint32_t (EG_ASSERT_32BIT).

#include <unity.h>
#include <Arduino.h>
#include <stdint.h>

// libstdc++ uses these as parameter names; a macro breaks <algorithm>.
#ifdef min
#error "min must not be a macro in the shim"
#endif
#ifdef max
#error "max must not be a macro in the shim"
#endif

// Use on the expression, not the declaration, so a promotion is caught too.
#define EG_ASSERT_32BIT(expr) \
  static_assert(sizeof(expr) == 4, "seam must be 32-bit to reproduce target wrap")

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// --- fixed-width sanity -----------------------------------------------------

static void test_fixed_widths(void) {
  TEST_ASSERT_EQUAL_size_t(4, sizeof(uint32_t));
  TEST_ASSERT_EQUAL_size_t(4, sizeof(int32_t));
  TEST_ASSERT_EQUAL_size_t(2, sizeof(uint16_t));
  TEST_ASSERT_EQUAL_size_t(1, sizeof(uint8_t));
  // int is 32-bit on both.
  TEST_ASSERT_EQUAL_size_t(4, sizeof(int));
}

// Holds on any LP64 host; flips under -m32.
static void test_unsigned_long_is_not_uint32_on_host(void) {
  if (sizeof(unsigned long) == sizeof(uint32_t)) {
    TEST_MESSAGE("32-bit host: unsigned long already matches the target");
  } else {
    TEST_ASSERT_EQUAL_size_t(8, sizeof(unsigned long));
  }
}

// Every ESP toolchain defaults to unsigned char; test.ini passes
// -fno-signed-char and this fails if that is dropped.
static void test_char_is_unsigned_like_the_target(void) {
  char c = (char)0x80;
  TEST_ASSERT_EQUAL_INT_MESSAGE(128, (int)c,
    "host char is signed; test.ini must pass -fno-signed-char to match the ESP toolchains");
  TEST_ASSERT_FALSE((char)-1 < 0);
}

// --- wrap arithmetic --------------------------------------------------------

static void test_uint32_wraps(void) {
  uint32_t v = 0xFFFFFFFFu;
  EG_ASSERT_32BIT(v + 1u);
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)(v + 1u));

  // Elapsed-time subtraction across the wrap.
  uint32_t before = 0xFFFFF000u;
  uint32_t after  = 0x00000FFFu;   // 0x1FFF ticks later
  TEST_ASSERT_EQUAL_UINT32(0x1FFFu, after - before);
}

// The `(int32_t)(now - due) < 0` idiom EGModuleRegistry uses.
static void test_signed_due_compare_survives_wrap(void) {
  struct { uint32_t now, due; bool due_yet; } cases[] = {
    { 1000u,       2000u,       false },  // ordinary: not yet
    { 2000u,       2000u,       true  },  // exactly due
    { 3000u,       2000u,       true  },  // ordinary: overdue
    { 0xFFFFFF00u, 0xFFFFFF10u, false },  // just below the wrap, not yet
    { 0x00000010u, 0xFFFFFFF0u, true  },  // clock wrapped past due, IS due
    { 0xFFFFFFF0u, 0x00000010u, false },  // due is past the wrap, not yet
  };
  for (auto& c : cases) {
    EG_ASSERT_32BIT(c.now - c.due);
    bool due_yet = (int32_t)(c.now - c.due) >= 0;
    TEST_ASSERT_EQUAL_INT(c.due_yet, due_yet);

    // Naive compare is wrong on the wrap cases.
    bool naive = c.now >= c.due;
    if (c.now == 0x00000010u || c.now == 0xFFFFFFF0u) {
      TEST_ASSERT_NOT_EQUAL(due_yet, naive);
    }
  }
}

// `seconds * 1000` overflows uint32 at 49.71 days, the same instant millis()
// wraps.
static void test_seconds_to_millis_overflow_boundary(void) {
  const uint32_t last_ok = 4294967u;          // 49.71 days, in seconds
  EG_ASSERT_32BIT(last_ok * 1000u);
  TEST_ASSERT_EQUAL_UINT32(4294967000u, last_ok * 1000u);

  // Same instant millis() wraps, to within a second.
  TEST_ASSERT_EQUAL_UINT32(4294967u, (uint32_t)(0xFFFFFFFFu / 1000u));

  const uint32_t first_bad = last_ok + 1u;
  TEST_ASSERT_NOT_EQUAL(4294968000u, first_bad * 1000u);  // wrapped
  TEST_ASSERT_EQUAL_UINT32(704u, first_bad * 1000u);

  // Widen the multiply, never the type.

  TEST_ASSERT_EQUAL_UINT64(4294968000ULL, (uint64_t)first_bad * 1000ULL);
}

// --- the fake clock ---------------------------------------------------------

static void test_fake_clock_only_moves_when_told(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, millis());
  TEST_ASSERT_EQUAL_UINT32(0u, micros());

  eg_clock_advance_ms(1500);
  TEST_ASSERT_EQUAL_UINT32(1500u, millis());
  TEST_ASSERT_EQUAL_UINT32(1500000u, micros());

  // Reading the clock never advances it.
  TEST_ASSERT_EQUAL_UINT32(1500u, millis());

  delay(500);
  TEST_ASSERT_EQUAL_UINT32(2000u, millis());
}

static void test_fake_clock_wraps_like_the_target(void) {
  // Park 5 ms below the millis() wrap and step across.
  eg_clock_set_us((uint64_t)0xFFFFFFFBu * 1000ULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFBu, millis());

  eg_clock_advance_ms(10);
  TEST_ASSERT_EQUAL_UINT32(5u, millis());

  // micros() wraps on its own, much sooner, and independently of millis().
  eg_clock_set_us(0xFFFFFFFFULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, micros());
  eg_clock_advance_us(1);
  TEST_ASSERT_EQUAL_UINT32(0u, micros());
}

static void test_cycle_count_is_deterministic(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, ESP.getCycleCount());
  eg_clock_advance_us(1000);
  TEST_ASSERT_EQUAL_UINT32(80000u, ESP.getCycleCount());   // 80 MHz nominal
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_widths);
  RUN_TEST(test_unsigned_long_is_not_uint32_on_host);
  RUN_TEST(test_char_is_unsigned_like_the_target);
  RUN_TEST(test_uint32_wraps);
  RUN_TEST(test_signed_due_compare_survives_wrap);
  RUN_TEST(test_seconds_to_millis_overflow_boundary);
  RUN_TEST(test_fake_clock_only_moves_when_told);
  RUN_TEST(test_fake_clock_wraps_like_the_target);
  RUN_TEST(test_cycle_count_is_deterministic);
  return UNITY_END();
}
