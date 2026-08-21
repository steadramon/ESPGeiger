/*
  test_countermaths - dose conversion, dead time, tube health.

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

// No Arduino, no input type, no clock. Uptime and click totals are arguments,
// so the header stands alone.

#include <unity.h>
#include <math.h>

#include "Counter/CounterMaths.h"

// Members are protected for Counter's benefit; some tests read the derived
// timeout.
class Maths : public CounterMaths {};

static Maths m;

void setUp(void)    { m = Maths(); }
void tearDown(void) {}

// --- ratio and dose ---------------------------------------------------------

static void test_default_ratio_converts_cpm_to_usv(void) {
  // Default 151 CPM per uSv/h.
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, m.usv_from_cpm(151.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, m.usv_from_cpm(0.0f));
}

static void test_set_ratio_changes_conversion(void) {
  m.set_ratio(100.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, m.usv_from_cpm(200.0f));
}

// A bad ratio would divide by zero or invert the dose scale.
static void test_set_ratio_rejects_non_positive(void) {
  m.set_ratio(100.0f);
  m.set_ratio(0.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
  m.set_ratio(-5.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
}

// --- tube timeout, derived from ratio ---------------------------------------

static void test_tube_timeout_floors_at_30_minutes(void) {
  m.set_ratio(151.0f);            // 12000/151 = 79 s, below the floor
  TEST_ASSERT_EQUAL_UINT32(GEIGER_TUBE_TIMEOUT_MIN_S, m.get_tube_timeout_s());
  m.set_ratio(1.0f);              // 12000/1 = 12000 s, above it
  TEST_ASSERT_EQUAL_UINT32(12000u, m.get_tube_timeout_s());
}

static void test_tube_alive_until_the_timeout(void) {
  m.set_ratio(1.0f);              // timeout 12000 s
  TEST_ASSERT_TRUE (m.tube_alive(11999u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(12000u, 0u, 0u));
  TEST_ASSERT_TRUE (m.tube_alive(20000u, 19000u, 0u));   // counted recently
}

// A tube that has never counted since boot must still go dead.
static void test_tube_dead_when_it_never_counted(void) {
  m.set_ratio(1.0f);
  TEST_ASSERT_FALSE(m.tube_alive(50000u, 0u, 0u));
}

// The ratio-derived timeout is a floor, not the whole answer: a slow tube's
// own rate carries the latch past it. 0.31 CPM over a week is a 191 s mean
// gap, so the latch sits near 3870 s and a real 33 min silence is not death.
static void test_tube_alive_uses_the_observed_rate(void) {
  m.set_ratio(6.8f);                              // 12000/6.8 floors at 1800 s
  const uint32_t up = 604800u, tc = 3125u;
  TEST_ASSERT_TRUE (m.tube_alive(up, up - 1980u, tc));
  TEST_ASSERT_FALSE(m.tube_alive(up, up - 4200u, tc));
}

// Without the ceiling a tube that died under GEIGER_DEAD_COUNTS clicks grows
// its own threshold faster than the silence and never latches.
static void test_tube_dead_capped_so_few_clicks_still_latch(void) {
  m.set_ratio(151.0f);
  TEST_ASSERT_FALSE(m.tube_alive(28800u, 100u, 5u));   // 8 h uptime, 5 clicks
}

// Fast tubes are untouched: 20 x a 3 s gap is far under the timeout.
static void test_tube_alive_unchanged_for_a_fast_tube(void) {
  m.set_ratio(151.0f);                            // timeout 1800 s
  const uint32_t up = 604800u, tc = 201600u;      // 20 CPM
  TEST_ASSERT_FALSE(m.tube_alive(up, up - 1860u, tc));
}

// --- dead time --------------------------------------------------------------

static void test_dead_time_zero_is_a_passthrough(void) {
  m.set_dead_time_us(0);
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, m.apply_dead_time(5000.0f));
}

static void test_dead_time_matches_the_non_paralyzable_model(void) {
  m.set_dead_time_us(100);                       // tau = 100e-6 s
  // n = m / (1 - m*tau); m=1000 -> 1000/(1-0.1) = 1111.11
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1111.11f, m.apply_dead_time(1000.0f));
  // m=100 -> 100/(1-0.01) = 101.0101
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 101.0101f, m.apply_dead_time(100.0f));
}

static void test_dead_time_correction_caps_at_ten_x(void) {
  m.set_dead_time_us(1000);
  // x is clamped to 0.9, so the factor never exceeds 1/(1-0.9) = 10.
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100000.0f, m.apply_dead_time(10000.0f));
  TEST_ASSERT_FLOAT_WITHIN(10.0f, 1000000.0f, m.apply_dead_time(100000.0f));
}

// CONTRACT: below 50 cps the input is returned uncorrected, so the output
// steps at that boundary. Under 1% at the 100 us default.
static void test_dead_time_skip_boundary_is_negligible_at_the_default(void) {
  m.set_dead_time_us(100);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, m.apply_dead_time(50.0f));
  float just_over = m.apply_dead_time(50.01f);
  TEST_ASSERT_TRUE(just_over > 50.01f);
  TEST_ASSERT_TRUE((just_over / 50.01f) < 1.006f);
}

// The 50 cps constant is fixed, but the factor it approximates scales with
// dead time, and dead_time_us is a 0..1000 pref. At the top of that range the
// step exceeds 5%, a visible jump in dose at ~3000 CPM.
static void test_dead_time_skip_boundary_steps_at_a_long_dead_time(void) {
  m.set_dead_time_us(1000);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, m.apply_dead_time(50.0f));
  float just_over = m.apply_dead_time(50.01f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 52.64f, just_over);
  TEST_ASSERT_TRUE((just_over / 50.01f) > 1.05f);
}

static void test_saturation_tracks_raw_rate(void) {
  m.set_dead_time_us(100);        // saturated above 0.875/100e-6 = 8750 cps
  TEST_ASSERT_FALSE(m.saturated_at(8000.0f));
  TEST_ASSERT_TRUE (m.saturated_at(9000.0f));
}

static void test_saturation_impossible_without_dead_time(void) {
  m.set_dead_time_us(0);
  TEST_ASSERT_FALSE(m.saturated_at(1000000.0f));
}

// --- missing counts advisory ------------------------------------------------

// Before the first count the expected rate is the assumed background, so the
// threshold is the time to accumulate GEIGER_MISSING_COUNTS at that rate.
static void test_missing_threshold_uses_assumed_background_first(void) {
  m.set_ratio(151.0f);
  // 10 counts at 0.1 uSv/h * 151 CPM = 15.1 CPM -> 10/15.1 min = 39.7 s,
  // but the tube timeout floor does not apply below itself, so expect 39.
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(0u, 0u));
}

// Once counting, the observed lifetime rate takes over if it is slower, so a
// genuinely quiet site does not false-alarm.
static void test_missing_threshold_self_calibrates_to_observed_rate(void) {
  m.set_ratio(151.0f);
  // 100 clicks in 10000 s = 0.01/s -> 10 counts takes 1000 s.
  TEST_ASSERT_EQUAL_UINT32(1000u, m.missing_threshold_s(10000u, 100u));
  // A fast site stays on the background figure, which is larger here.
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(10000u, 100000u));
}

// The advisory must never be slower than the hard dead-tube latch.
static void test_missing_threshold_capped_by_tube_timeout(void) {
  m.set_ratio(151.0f);            // timeout floors at 1800
  // 1 click in 1e6 s would want 1e7 s; must clamp.
  TEST_ASSERT_EQUAL_UINT32(GEIGER_TUBE_TIMEOUT_MIN_S,
                           m.missing_threshold_s(1000000u, 1u));
}

static void test_counts_missing_fires_past_the_threshold(void) {
  m.set_ratio(151.0f);            // threshold 39 s, but a 60 s floor applies
  TEST_ASSERT_FALSE(m.counts_missing(59u, 0u, 0u));
  TEST_ASSERT_TRUE (m.counts_missing(61u, 0u, 0u));
}

// CONTRACT: below 60 s no threshold can fire, so the healthy path skips the
// soft-float and 64-bit divides entirely.
static void test_counts_missing_has_a_sixty_second_floor(void) {
  m.set_ratio(151.0f);
  for (uint32_t s = 0; s < 60; s++) {
    TEST_ASSERT_FALSE(m.counts_missing(s, 0u, 0u));
  }
}

static void test_counts_missing_quiet_when_counting(void) {
  m.set_ratio(151.0f);
  // Counted 5 s ago at uptime 10000.
  TEST_ASSERT_FALSE(m.counts_missing(10000u, 9995u, 5000u));
}

// Silence is measured against the last count, and both are uptime seconds, so
// this must survive a 32-bit wrap of the uptime counter.
static void test_counts_missing_survives_uptime_wrap(void) {
  m.set_ratio(151.0f);
  uint32_t last = 0xFFFFFFF0u;
  uint32_t now  = 10u;                 // wrapped: 26 s of real silence
  TEST_ASSERT_FALSE(m.counts_missing(now, last, 1000u));
  TEST_ASSERT_TRUE (m.counts_missing(last + 200u, last, 1000u));
}

static void test_tube_alive_survives_uptime_wrap(void) {
  m.set_ratio(1.0f);                   // timeout 12000 s
  uint32_t last = 0xFFFFFFF0u;
  TEST_ASSERT_TRUE (m.tube_alive(10u, last, 0u));           // 26 s of silence
  TEST_ASSERT_FALSE(m.tube_alive(last + 12001u, last, 0u));
}

// --- rate from a timestamp span ---------------------------------------------

static void test_cps_from_span_is_n_minus_one_over_t(void) {
  // 11 pulses over 1 s = 10 intervals = 10 cps.
  TEST_ASSERT_EQUAL_FLOAT(10.0f, CounterMaths::cps_from_span(11, 1000000u));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,  CounterMaths::cps_from_span(2, 1000000u));
}

// A single sample carries no interval, and a collapsed span would divide by
// zero. Same fault class as the OLED graph; must not reach the divide.
static void test_cps_from_span_guards_degenerate_input(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, CounterMaths::cps_from_span(0, 1000000u));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, CounterMaths::cps_from_span(1, 1000000u));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, CounterMaths::cps_from_span(100, 0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_ratio_converts_cpm_to_usv);
  RUN_TEST(test_set_ratio_changes_conversion);
  RUN_TEST(test_set_ratio_rejects_non_positive);
  RUN_TEST(test_tube_timeout_floors_at_30_minutes);
  RUN_TEST(test_tube_alive_until_the_timeout);
  RUN_TEST(test_tube_dead_when_it_never_counted);
  RUN_TEST(test_tube_alive_uses_the_observed_rate);
  RUN_TEST(test_tube_dead_capped_so_few_clicks_still_latch);
  RUN_TEST(test_tube_alive_unchanged_for_a_fast_tube);
  RUN_TEST(test_dead_time_zero_is_a_passthrough);
  RUN_TEST(test_dead_time_matches_the_non_paralyzable_model);
  RUN_TEST(test_dead_time_correction_caps_at_ten_x);
  RUN_TEST(test_dead_time_skip_boundary_is_negligible_at_the_default);
  RUN_TEST(test_dead_time_skip_boundary_steps_at_a_long_dead_time);
  RUN_TEST(test_saturation_tracks_raw_rate);
  RUN_TEST(test_saturation_impossible_without_dead_time);
  RUN_TEST(test_missing_threshold_uses_assumed_background_first);
  RUN_TEST(test_missing_threshold_self_calibrates_to_observed_rate);
  RUN_TEST(test_missing_threshold_capped_by_tube_timeout);
  RUN_TEST(test_counts_missing_fires_past_the_threshold);
  RUN_TEST(test_counts_missing_has_a_sixty_second_floor);
  RUN_TEST(test_counts_missing_quiet_when_counting);
  RUN_TEST(test_counts_missing_survives_uptime_wrap);
  RUN_TEST(test_tube_alive_survives_uptime_wrap);
  RUN_TEST(test_cps_from_span_is_n_minus_one_over_t);
  RUN_TEST(test_cps_from_span_guards_degenerate_input);
  return UNITY_END();
}
