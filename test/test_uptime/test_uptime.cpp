/*
  test_uptime - UptimeCounter, the millis() wrap reconstruction.

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

// OldUptimeCounter is the previous formula and must keep failing here.

#include <unity.h>
#include <stdint.h>

#include "Util/UptimeCounter.h"

void setUp(void)    {}
void tearDown(void) {}

// A wrap is weighted 4294967 s, not 4294967.296, so one second is lost per wrap.
static const uint32_t WRAP_S   = 4294967u;
static const uint64_t WRAP_MS  = 4294967296ULL;

// millis() as the target reports it, from a 64-bit true elapsed time.
static uint32_t millis_at(uint64_t true_ms) { return (uint32_t)true_ms; }

// --- basics -----------------------------------------------------------------

static void test_before_any_wrap_is_exact(void) {
  UptimeCounter u;
  const uint64_t points[] = { 0, 1, 999, 1000, 1001, 59999, 60000, 86400000ULL };
  for (uint64_t ms : points) {
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(ms / 1000ULL), u.tick(millis_at(ms)));
  }
  TEST_ASSERT_EQUAL_UINT16(0, u.wraps());
}

static void test_seconds_truncate_not_round(void) {
  UptimeCounter u;
  TEST_ASSERT_EQUAL_UINT32(0, u.tick(999));
  TEST_ASSERT_EQUAL_UINT32(1, u.tick(1000));
  TEST_ASSERT_EQUAL_UINT32(1, u.tick(1999));
  TEST_ASSERT_EQUAL_UINT32(2, u.tick(2000));
}

// --- the wrap ---------------------------------------------------------------

static void test_single_wrap_is_counted_once(void) {
  UptimeCounter u;
  u.tick(millis_at(WRAP_MS - 2000));            // 2 s before the wrap
  TEST_ASSERT_EQUAL_UINT16(0, u.wraps());

  uint32_t after = u.tick(millis_at(WRAP_MS + 1000));   // 1 s after
  TEST_ASSERT_EQUAL_UINT16(1, u.wraps());
  TEST_ASSERT_EQUAL_UINT32(WRAP_S + 1, after);
}

// Extra calls inside the sub-second window after a wrap must be no-ops.
static void test_repeated_calls_after_wrap_do_not_re_trigger(void) {
  UptimeCounter u;
  u.tick(millis_at(WRAP_MS - 500));

  uint32_t first = u.tick(millis_at(WRAP_MS + 10));
  for (int i = 0; i < 50; i++) {
    uint32_t again = u.tick(millis_at(WRAP_MS + 10 + i));
    TEST_ASSERT_EQUAL_UINT16(1, u.wraps());
    TEST_ASSERT_EQUAL_UINT32(first, again);
  }
}

// Contract is at least one tick per wrap; four per period here.
static void test_many_wraps_accumulate(void) {
  UptimeCounter u;
  const uint64_t STEP = WRAP_MS / 4;

  for (int w = 0; w <= 10; w++) {
    for (int q = 0; q < 4; q++) {
      u.tick(millis_at((uint64_t)w * WRAP_MS + q * STEP));
    }
    uint32_t got = u.tick(millis_at((uint64_t)w * WRAP_MS + 3 * STEP + 5000ULL));
    TEST_ASSERT_EQUAL_UINT16(w, u.wraps());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)w * WRAP_S + (uint32_t)((3 * STEP + 5000ULL) / 1000ULL), got);
  }
}

// CONTRACT: a tick exactly once per wrap period sees no wrap.
static void test_sampling_at_exactly_one_wrap_period_sees_nothing(void) {
  UptimeCounter u;
  for (int w = 0; w <= 5; w++) {
    u.tick(millis_at((uint64_t)w * WRAP_MS + 5000ULL));
  }
  TEST_ASSERT_EQUAL_UINT16(0, u.wraps());
}

// --- properties over a long run ---------------------------------------------

// Two years hourly: never backwards, never high, low by at most 1 s per wrap.
static void test_monotonic_and_bounded_drift_over_two_years(void) {
  UptimeCounter u;
  uint32_t prev = 0;
  for (uint64_t t = 0; t <= 730ULL * 86400ULL; t += 3600ULL) {
    uint32_t got = u.tick(millis_at(t * 1000ULL));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(prev, got);
    prev = got;

    // Only ever low, never high, and bounded by one second per wrap elapsed.
    uint64_t slack = (uint64_t)u.wraps() + 1;
    uint64_t floor_s = (t > slack) ? (t - slack) : 0;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32((uint32_t)t, got);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)floor_s, got);
  }
  TEST_ASSERT_EQUAL_UINT16(14, u.wraps());     // 730 d / 49.71 d
}

// --- the previous formula, asserted broken ----------------------------------
class OldUptimeCounter {
public:
  uint32_t tick(uint32_t now_ms) {
    if (_uptime * 1000u > now_ms) _wraps++;
    _uptime = now_ms / 1000u + (uint32_t)_wraps * 4294967u;
    return _uptime;
  }
  uint16_t wraps() const { return _wraps; }
private:
  uint32_t _uptime = 0;
  uint16_t _wraps  = 0;
};

// Both agree up to the wrap.
static void test_old_formula_agrees_before_the_wrap(void) {
  UptimeCounter neu;
  OldUptimeCounter old;
  for (uint64_t t = 0; t < 4294000ULL; t += 997) {
    uint32_t ms = millis_at(t * 1000ULL);
    TEST_ASSERT_EQUAL_UINT32(neu.tick(ms), old.tick(ms));
  }
}

// Old is wrong from the second call after the wrap.
static void test_old_formula_is_wrong_at_the_first_wrap(void) {
  UptimeCounter neu;
  OldUptimeCounter old;

  const uint64_t before = WRAP_MS - 500;
  neu.tick(millis_at(before));
  old.tick(millis_at(before));

  const uint64_t after = WRAP_MS + 200;
  uint32_t n = neu.tick(millis_at(after));
  uint32_t o = old.tick(millis_at(after));

  const uint32_t truth = (uint32_t)(after / 1000ULL);
  TEST_ASSERT_UINT32_WITHIN(2, truth, n);          // new: right
  TEST_ASSERT_EQUAL_UINT32(truth, o);              // old: also right, ONCE

  // Next call in the same second counts a second wrap.
  uint32_t n2 = neu.tick(millis_at(after + 100));
  uint32_t o2 = old.tick(millis_at(after + 100));
  TEST_ASSERT_UINT32_WITHIN(2, truth, n2);
  TEST_ASSERT_GREATER_THAN_UINT32(truth + 86400u, o2);   // out by >1 day
}

// Old error scales with calls inside the window: three calls is 149 days.
static void test_old_formula_jump_scales_with_call_count(void) {
  for (int calls = 2; calls <= 4; calls++) {
    OldUptimeCounter old;
    old.tick(millis_at(WRAP_MS - 500));

    uint32_t last = 0;
    for (int c = 0; c < calls; c++) last = old.tick(millis_at(WRAP_MS + 10 + c));

    // Each extra call in the window adds one whole wrap period.
    TEST_ASSERT_EQUAL_UINT16(calls, old.wraps());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)calls * WRAP_S, last);
  }

  // Three calls.

  OldUptimeCounter three;
  three.tick(millis_at(WRAP_MS - 500));
  three.tick(millis_at(WRAP_MS + 10));
  three.tick(millis_at(WRAP_MS + 11));
  uint32_t reported = three.tick(millis_at(WRAP_MS + 12));
  TEST_ASSERT_EQUAL_UINT32(3u * WRAP_S, reported);
  TEST_ASSERT_EQUAL_UINT32(149, reported / 86400u);      // 149 days
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_before_any_wrap_is_exact);
  RUN_TEST(test_seconds_truncate_not_round);
  RUN_TEST(test_single_wrap_is_counted_once);
  RUN_TEST(test_repeated_calls_after_wrap_do_not_re_trigger);
  RUN_TEST(test_many_wraps_accumulate);
  RUN_TEST(test_sampling_at_exactly_one_wrap_period_sees_nothing);
  RUN_TEST(test_monotonic_and_bounded_drift_over_two_years);
  RUN_TEST(test_old_formula_agrees_before_the_wrap);
  RUN_TEST(test_old_formula_is_wrong_at_the_first_wrap);
  RUN_TEST(test_old_formula_jump_scales_with_call_count);
  return UNITY_END();
}
