/*
  test_ringavg - EGRingAvg and EGEma.

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

// EGRingAvg keeps a running sum rather than re-adding the window per read.
// The sum must shed the slot it overwrites, against the runtime window rather
// than the template capacity.

#include <unity.h>
#include <Arduino.h>

#include "Util/EGSmoothed.h"

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// --- empty state ------------------------------------------------------------

static void test_empty_reads_are_zero(void) {
  EGRingAvg<int32_t, 8> r;
  TEST_ASSERT_EQUAL_INT32(0, r.get());
  TEST_ASSERT_EQUAL_INT32(0, r.sum());
  TEST_ASSERT_EQUAL_INT32(0, r.last());
  TEST_ASSERT_EQUAL_INT32(0, r.at(0));
  TEST_ASSERT_EQUAL_UINT16(0, r.count());
  TEST_ASSERT_EQUAL_UINT16(8, r.window());
  TEST_ASSERT_FALSE(r.warm());
}

// --- partial fill -----------------------------------------------------------

// The mean is over populated slots only. Dividing by the window instead would
// drag every fresh average toward zero, which is exactly the low-count display
// smoothing bug shape.
static void test_partial_fill_averages_over_count_not_window(void) {
  EGRingAvg<int32_t, 8> r;
  r.add(10);
  TEST_ASSERT_EQUAL_UINT16(1, r.count());
  TEST_ASSERT_EQUAL_INT32(10, r.get());
  TEST_ASSERT_EQUAL_INT32(10, r.sum());
  TEST_ASSERT_FALSE(r.warm());

  r.add(20);
  TEST_ASSERT_EQUAL_INT32(15, r.get());
  TEST_ASSERT_EQUAL_INT32(30, r.sum());

  r.add(30);
  TEST_ASSERT_EQUAL_INT32(20, r.get());
  TEST_ASSERT_EQUAL_UINT16(3, r.count());
  TEST_ASSERT_FALSE(r.warm());
}

// --- wrap -------------------------------------------------------------------

static void test_wrap_evicts_oldest(void) {
  EGRingAvg<int32_t, 4> r;
  r.add(1); r.add(2); r.add(3); r.add(4);
  TEST_ASSERT_TRUE(r.warm());
  TEST_ASSERT_EQUAL_INT32(10, r.sum());
  TEST_ASSERT_EQUAL_INT32(2, r.get());        // 10/4 truncates

  r.add(5);                                    // evicts the 1
  TEST_ASSERT_EQUAL_INT32(14, r.sum());
  TEST_ASSERT_EQUAL_INT32(3, r.get());
  TEST_ASSERT_EQUAL_UINT16(4, r.count());

  r.add(6); r.add(7); r.add(8);                // full turn
  TEST_ASSERT_EQUAL_INT32(26, r.sum());        // 5+6+7+8
}

// The running sum must not drift away from a recomputed one over many wraps.
static void test_running_sum_matches_recompute_over_many_wraps(void) {
  const uint16_t W = 6;
  EGRingAvg<int32_t, 6> r;
  int32_t recent[6] = {0};
  uint16_t n = 0;

  for (int32_t i = 1; i <= 500; i++) {
    int32_t v = (i * 37) % 211 - 100;          // mixed signs, no pattern in W
    r.add(v);
    recent[(n) % W] = v;
    if (n < 60000) n++;

    uint16_t populated = (n < W) ? n : W;
    int32_t expect = 0;
    for (uint16_t k = 0; k < populated; k++) expect += recent[k];
    TEST_ASSERT_EQUAL_INT32(expect, r.sum());
    TEST_ASSERT_EQUAL_INT32(expect / (int32_t)populated, r.get());
  }
}

// --- chronological access ---------------------------------------------------

// at() is what the sparkline draws from, so 0 must be the oldest live sample
// both before and after the buffer has wrapped.
static void test_at_is_chronological(void) {
  EGRingAvg<int32_t, 4> r;

  r.add(1); r.add(2);
  TEST_ASSERT_EQUAL_INT32(1, r.at(0));
  TEST_ASSERT_EQUAL_INT32(2, r.at(1));

  r.add(3); r.add(4); r.add(5);                // 1 evicted
  TEST_ASSERT_EQUAL_INT32(2, r.at(0));
  TEST_ASSERT_EQUAL_INT32(3, r.at(1));
  TEST_ASSERT_EQUAL_INT32(4, r.at(2));
  TEST_ASSERT_EQUAL_INT32(5, r.at(3));

  // Past the end clamps to the newest rather than reading a stale slot.
  TEST_ASSERT_EQUAL_INT32(5, r.at(4));
  TEST_ASSERT_EQUAL_INT32(5, r.at(9999));
}

static void test_last_tracks_newest_across_wrap(void) {
  EGRingAvg<int32_t, 3> r;
  for (int32_t i = 1; i <= 10; i++) {
    r.add(i);
    TEST_ASSERT_EQUAL_INT32(i, r.last());
    TEST_ASSERT_EQUAL_INT32(i, r.at(r.count() - 1));
  }
}

// --- runtime window ---------------------------------------------------------

static void test_begin_clamps_window(void) {
  EGRingAvg<int32_t, 8> r;
  r.begin(0);   TEST_ASSERT_EQUAL_UINT16(1, r.window());
  r.begin(5);   TEST_ASSERT_EQUAL_UINT16(5, r.window());
  r.begin(8);   TEST_ASSERT_EQUAL_UINT16(8, r.window());
  r.begin(99);  TEST_ASSERT_EQUAL_UINT16(8, r.window());
}

// A window below the template capacity has to wrap at the window, not at
// MaxN, or the sum sheds a slot it never wrote.
static void test_runtime_window_shorter_than_capacity(void) {
  EGRingAvg<int32_t, 8> r;
  r.begin(3);

  r.add(1); r.add(2); r.add(3);
  TEST_ASSERT_TRUE(r.warm());
  TEST_ASSERT_EQUAL_UINT16(3, r.count());
  TEST_ASSERT_EQUAL_INT32(6, r.sum());
  TEST_ASSERT_EQUAL_INT32(2, r.get());

  r.add(4);
  TEST_ASSERT_EQUAL_INT32(9, r.sum());         // 2+3+4, not 1+2+3+4
  TEST_ASSERT_EQUAL_INT32(3, r.get());
  TEST_ASSERT_EQUAL_INT32(2, r.at(0));
  TEST_ASSERT_EQUAL_INT32(4, r.at(2));
}

static void test_begin_resets_running_state(void) {
  EGRingAvg<int32_t, 8> r;
  r.add(100); r.add(200);
  r.begin(4);
  TEST_ASSERT_EQUAL_UINT16(0, r.count());
  TEST_ASSERT_EQUAL_INT32(0, r.sum());
  TEST_ASSERT_EQUAL_INT32(0, r.get());
  r.add(1);
  TEST_ASSERT_EQUAL_INT32(1, r.sum());         // no stale 300 left behind
}

static void test_reset_clears_everything(void) {
  EGRingAvg<int32_t, 4> r;
  r.add(7); r.add(8); r.add(9); r.add(10); r.add(11);
  r.reset();
  TEST_ASSERT_EQUAL_UINT16(0, r.count());
  TEST_ASSERT_EQUAL_INT32(0, r.sum());
  TEST_ASSERT_EQUAL_INT32(0, r.last());
  TEST_ASSERT_EQUAL_UINT16(4, r.window());     // window survives a reset
}

// --- float instantiation ----------------------------------------------------

static void test_float_window(void) {
  EGRingAvg<float, 4> r;
  r.add(1.0f); r.add(2.0f); r.add(3.0f); r.add(4.0f);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, r.get());
  r.add(5.0f);
  TEST_ASSERT_EQUAL_FLOAT(3.5f, r.get());
}

// --- EGEma ------------------------------------------------------------------

static void test_ema_converges_and_never_overshoots(void) {
  EGEma<float> e;
  e.begin(4);                                   // alpha = 0.25
  TEST_ASSERT_EQUAL_FLOAT(0.0f, e.get());

  e.add(100.0f);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, e.get());
  e.add(100.0f);
  TEST_ASSERT_EQUAL_FLOAT(43.75f, e.get());

  for (int i = 0; i < 200; i++) {
    e.add(100.0f);
    TEST_ASSERT_TRUE(e.get() <= 100.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, e.get());
}

// factor 1 makes the EMA a passthrough; factor 0 is clamped to that rather
// than dividing by zero.
static void test_ema_factor_bounds(void) {
  EGEma<float> e;
  e.begin(1);
  e.add(42.0f);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, e.get());

  EGEma<float> z;
  z.begin(0);
  z.add(42.0f);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, z.get());
}

static void test_ema_begin_resets_value(void) {
  EGEma<float> e;
  e.begin(2);
  e.add(80.0f);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, e.get());
  e.begin(2);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, e.get());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_reads_are_zero);
  RUN_TEST(test_partial_fill_averages_over_count_not_window);
  RUN_TEST(test_wrap_evicts_oldest);
  RUN_TEST(test_running_sum_matches_recompute_over_many_wraps);
  RUN_TEST(test_at_is_chronological);
  RUN_TEST(test_last_tracks_newest_across_wrap);
  RUN_TEST(test_begin_clamps_window);
  RUN_TEST(test_runtime_window_shorter_than_capacity);
  RUN_TEST(test_begin_resets_running_state);
  RUN_TEST(test_reset_clears_everything);
  RUN_TEST(test_float_window);
  RUN_TEST(test_ema_converges_and_never_overshoots);
  RUN_TEST(test_ema_factor_bounds);
  RUN_TEST(test_ema_begin_resets_value);
  return UNITY_END();
}
