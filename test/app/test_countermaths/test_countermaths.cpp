/*
  test_countermaths - ratio, dead time, tube health.

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
#include <math.h>
#include <Arduino.h>

#include "Counter/CounterMaths.h"
#include "Corium/Util/EGSmoothed.h"

class Maths : public CounterMaths {};

static Maths m;

void setUp(void)    { m = Maths(); }
void tearDown(void) {}

static void ratio_change(void) {
  m.set_ratio(100.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
}

static void bad_ratio(void) {
  m.set_ratio(100.0f);
  m.set_ratio(0.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
  m.set_ratio(-5.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
  m.set_ratio(1e-6f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
  m.set_ratio(NAN);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, m.get_ratio());
}

static void alive_to_timeout(void) {
  m.set_ratio(1.0f);              // timeout 12000 s
  TEST_ASSERT_TRUE (m.tube_alive(11999u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(12000u, 0u, 0u));
  TEST_ASSERT_TRUE (m.tube_alive(20000u, 19000u, 0u));
}

static void never_counted(void) {
  m.set_ratio(1.0f);
  TEST_ASSERT_FALSE(m.tube_alive(50000u, 0u, 0u));
}

// 0.31 CPM: 191 s mean gap
static void observed_rate(void) {
  m.set_ratio(6.8f);                              // 12000/6.8 floors at 1800 s
  const uint32_t up = 604800u, tc = 3125u;
  TEST_ASSERT_TRUE (m.tube_alive(up, up - 1980u, tc));
  TEST_ASSERT_FALSE(m.tube_alive(up, up - 4200u, tc));
}

static void dead_cap_latch(void) {
  m.set_ratio(151.0f);
  TEST_ASSERT_FALSE(m.tube_alive(28800u, 100u, 5u));   // 8 h uptime, 5 clicks
}

static void fast_tube(void) {
  m.set_ratio(151.0f);                            // timeout 1800 s
  const uint32_t up = 604800u, tc = 201600u;      // 20 CPM
  TEST_ASSERT_FALSE(m.tube_alive(up, up - 1860u, tc));
}

static void dead_time_zero(void) {
  m.set_dead_time_us(0);
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, m.apply_dead_time(5000.0f));
}

static void dead_time_model(void) {
  m.set_dead_time_us(100);                       // tau = 100e-6 s
  // n = m / (1 - m*tau); m=1000 -> 1000/(1-0.1) = 1111.11
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1111.11f, m.apply_dead_time(1000.0f));
  // m=100 -> 100/(1-0.01) = 101.0101
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 101.0101f, m.apply_dead_time(100.0f));
}

static void dead_time_cap(void) {
  m.set_dead_time_us(1000);
  // x clamps at 0.9, so max 10x
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100000.0f, m.apply_dead_time(10000.0f));
  TEST_ASSERT_FLOAT_WITHIN(10.0f, 1000000.0f, m.apply_dead_time(100000.0f));
}

// floating pin, cpm still fits an int
static void dead_time_bounds(void) {
  m.set_dead_time_us(100);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, m.apply_dead_time(-5.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, m.apply_dead_time(NAN));
  TEST_ASSERT_TRUE(m.apply_dead_time(1e12f) * 60.0f < 2.2e9f);
  m.set_dead_time_us(0);
  TEST_ASSERT_EQUAL_FLOAT(CounterMaths::CPS_MAX, m.apply_dead_time(1e12f));
}

static void counts_clamp(void) {
  TEST_ASSERT_EQUAL_INT(0, CounterMaths::clamp_counts(-1));
  TEST_ASSERT_EQUAL_INT(40, CounterMaths::clamp_counts(40));
  TEST_ASSERT_EQUAL_INT((int)CounterMaths::CPS_MAX, CounterMaths::clamp_counts(INT32_MAX));
}

// volatile, else a folded cast hides it
static void to_int_sat(void) {
  volatile float nan = NAN, neg = -3.0f, huge = 1e20f;
  TEST_ASSERT_EQUAL_INT(0, CounterMaths::to_int(nan));
  TEST_ASSERT_EQUAL_INT(0, CounterMaths::to_int(neg));
  TEST_ASSERT_EQUAL_INT(0, CounterMaths::to_int(-0.05f));
  TEST_ASSERT_EQUAL_INT(2, CounterMaths::to_int(2.49f));
  TEST_ASSERT_EQUAL_INT(3, CounterMaths::to_int(2.5f));
  TEST_ASSERT_EQUAL_INT(INT32_MAX, CounterMaths::to_int(huge));
}

static void x600_sat(void) {
  volatile float nan = NAN, neg = -1.0f, huge = 1e20f;
  TEST_ASSERT_EQUAL_UINT16(0, CounterMaths::cps_x600(nan));
  TEST_ASSERT_EQUAL_UINT16(0, CounterMaths::cps_x600(neg));
  TEST_ASSERT_EQUAL_UINT16(300, CounterMaths::cps_x600(0.5f));
  TEST_ASSERT_EQUAL_UINT16(8, CounterMaths::cps_x600(0.0125f));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, CounterMaths::cps_x600(200.0f));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, CounterMaths::cps_x600(huge));
}

// no divide below 50 cps
static void dead_time_skip(void) {
  m.set_dead_time_us(100);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.25f, m.apply_dead_time(50.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.01f, m.apply_dead_time(10.0f));
}

// both branches meet at 50 cps
static void dead_time_join(void) {
  m.set_dead_time_us(190);
  float below = m.apply_dead_time(49.99f);
  float above = m.apply_dead_time(50.01f);
  TEST_ASSERT_TRUE(above > below);
  TEST_ASSERT_TRUE((above / 50.01f - below / 49.99f) < 0.0001f);
  float lo = m.apply_dead_time(50.0f);
  float exact = 50.0f / (1.0f - 50.0f * 190e-6f);
  TEST_ASSERT_TRUE((exact - lo) / exact < 0.0001f);
}

static void long_tau(void) {
  m.set_dead_time_us(1000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 52.5f, m.apply_dead_time(50.0f));
  float just_over = m.apply_dead_time(50.01f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 52.64f, just_over);
  TEST_ASSERT_TRUE((just_over / 50.01f) > 1.05f);
}

static void saturation_rate(void) {
  m.set_dead_time_us(100);        // saturated above 0.875/100e-6 = 8750 cps
  TEST_ASSERT_FALSE(m.saturated_at(8000.0f));
  TEST_ASSERT_TRUE (m.saturated_at(9000.0f));
}

static void saturation_off(void) {
  m.set_dead_time_us(0);
  TEST_ASSERT_FALSE(m.saturated_at(1000000.0f));
}

static void threshold_bg(void) {
  m.set_ratio(151.0f);
  // 15.1 CPM -> 39.7 s
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(0u, 0u));
}

static void threshold_cal(void) {
  m.set_ratio(151.0f);
  // 0.01/s -> 10 counts in 1000 s
  TEST_ASSERT_EQUAL_UINT32(1000u, m.missing_threshold_s(10000u, 100u));
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(10000u, 100000u));
}

static void threshold_cap(void) {
  m.set_ratio(151.0f);            // timeout floors at 1800
  // would want 1e7 s
  TEST_ASSERT_EQUAL_UINT32(GEIGER_TUBE_TIMEOUT_MIN_S,
                           m.missing_threshold_s(1000000u, 1u));
}

// 0.0625 and 0.25 are exact in binary
static void bg_low(void) {
  m.set_ratio(1.0f);
  m.set_background(0.0625f, 1800u);   // 1200 / 0.0625
  TEST_ASSERT_TRUE (m.tube_alive(19199u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(19200u, 0u, 0u));
  m.set_ratio(151.0f);
  TEST_ASSERT_EQUAL_UINT32(63u, m.missing_threshold_s(0u, 0u));   // was 39
}

static void bg_high(void) {
  m.set_ratio(1.0f);
  m.set_background(0.25f, 1800u);
  TEST_ASSERT_TRUE (m.tube_alive(4799u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(4800u, 0u, 0u));
}

static void bg_first(void) {
  m.set_background(0.0625f, 1800u);
  m.set_ratio(1.0f);
  TEST_ASSERT_FALSE(m.tube_alive(19200u, 0u, 0u));
  TEST_ASSERT_TRUE (m.tube_alive(19199u, 0u, 0u));
}

static void bg_refused(void) {
  m.set_ratio(1.0f);   // 12000 s
  m.set_background(0.0f, 0u);
  m.set_background(NAN, 0u);
  m.set_background(2.0f, 0u);
  TEST_ASSERT_TRUE (m.tube_alive(11999u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(12000u, 0u, 0u));
}

// a saved 0 goes back to the default, not the last one
static void bg_zeroed(void) {
  m.set_ratio(1.0f);
  m.set_background(0.25f, 1800u);
  m.set_background(0.0f, 1800u);
  TEST_ASSERT_TRUE (m.tube_alive(11999u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(12000u, 0u, 0u));
}

static void quiet_floor(void) {
  m.set_ratio(151.0f);   // 79 s, the floor rules
  m.set_background(0.1f, 3600u);
  TEST_ASSERT_TRUE (m.tube_alive(3599u, 0u, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(3600u, 0u, 0u));
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(0u, 0u));
}

static void missing_fires(void) {
  m.set_ratio(151.0f);            // 39 s, but 60 s floor
  TEST_ASSERT_FALSE(m.counts_missing(59u, 0u, 0u));
  TEST_ASSERT_TRUE (m.counts_missing(61u, 0u, 0u));
}

// nothing below 60 s
static void missing_60s(void) {
  m.set_ratio(151.0f);
  for (uint32_t s = 0; s < 60; s++) {
    TEST_ASSERT_FALSE(m.counts_missing(s, 0u, 0u));
  }
}

static void missing_quiet(void) {
  m.set_ratio(151.0f);
  // counted 5 s ago
  TEST_ASSERT_FALSE(m.counts_missing(10000u, 9995u, 5000u));
}

static void missing_wrap(void) {
  m.set_ratio(151.0f);
  uint32_t last = 0xFFFFFFF0u;
  uint32_t now  = 10u;                 // wrapped, 26 s
  TEST_ASSERT_FALSE(m.counts_missing(now, last, 1000u));
  TEST_ASSERT_TRUE (m.counts_missing(last + 200u, last, 1000u));
}

static void alive_wrap(void) {
  m.set_ratio(1.0f);                   // timeout 12000 s
  uint32_t last = 0xFFFFFFF0u;
  TEST_ASSERT_TRUE (m.tube_alive(10u, last, 0u));
  TEST_ASSERT_FALSE(m.tube_alive(last + 12001u, last, 0u));
}

// 2^32 + 5 counts in a week is fast, not 5
static void total_wrap(void) {
  m.set_ratio(151.0f);
  const uint32_t up = 604800u;
  uint64_t tc = (1ull << 32) + 5u;
  TEST_ASSERT_FALSE(m.tube_alive(up, up - 1860u, tc));
  TEST_ASSERT_TRUE (m.counts_missing(up, up - 100u, tc));
  TEST_ASSERT_EQUAL_UINT32(39u, m.missing_threshold_s(up, tc));
}

static void bucket_edges(void) {
  TEST_ASSERT_EQUAL_UINT8(0,  CounterMaths::interval_bucket(0));
  TEST_ASSERT_EQUAL_UINT8(0,  CounterMaths::interval_bucket(63));
  TEST_ASSERT_EQUAL_UINT8(1,  CounterMaths::interval_bucket(64));
  TEST_ASSERT_EQUAL_UINT8(1,  CounterMaths::interval_bucket(127));
  TEST_ASSERT_EQUAL_UINT8(2,  CounterMaths::interval_bucket(128));
  TEST_ASSERT_EQUAL_UINT8(14, CounterMaths::interval_bucket(1000000u));   // 1 s
  TEST_ASSERT_EQUAL_UINT8(23, CounterMaths::interval_bucket((1u << 29) - 1));
  TEST_ASSERT_EQUAL_UINT8(24, CounterMaths::interval_bucket(1u << 29));
  TEST_ASSERT_EQUAL_UINT8(24, CounterMaths::interval_bucket(0xFFFFFFFFu));
  TEST_ASSERT_EQUAL_UINT8(CounterMaths::HIST_BUCKETS - 1, CounterMaths::interval_bucket(0xFFFFFFFFu));
}

static void bucket_log2(void) {
  for (int lg = 0; lg < 32; lg++) {
    const uint32_t lo = 1u << lg;
    const uint32_t ds[3] = {lo, lo + (lo >> 1), lo + (lo - 1)};
    for (uint32_t d : ds) {
      uint8_t want = (d < 64) ? 0 : (lg < 30 ? (uint8_t)(lg - 5) : 24);
      TEST_ASSERT_EQUAL_UINT8(want, CounterMaths::interval_bucket(d));
    }
  }
}

// 63% like a 60 sample avg
static void ema_window(void) {
  EGEma<float> ema;
  ema.begin(GEIGER_EMA_FACTOR);
  EGRingAvg<float, GEIGER_CPM5_COUNT> avg;
  for (int i = 0; i < GEIGER_CPM5_COUNT; i++) { ema.add(0.0f); avg.add(0.0f); }
  int ema_n = 0, avg_n = 0;
  for (int n = 1; n <= 200 && !(ema_n && avg_n); n++) {
    ema.add(100.0f);
    avg.add(100.0f);
    if (!ema_n && ema.get() >= 63.0f) ema_n = n;
    if (!avg_n && avg.get() >= 63.0f) avg_n = n;
  }
  TEST_ASSERT_EQUAL_INT((int)ceilf(0.63f * GEIGER_CPM5_COUNT), avg_n);
  TEST_ASSERT_TRUE(ema_n > avg_n / 2);
  TEST_ASSERT_TRUE(ema_n <= avg_n);
}

static void ema_first_sample(void) {
  EGEma<float> cpm5, cpm15;
  cpm5.begin(GEIGER_EMA_FACTOR);
  cpm15.begin(GEIGER_EMA_FACTOR);
  for (int i = 0; i < 3; i++) {
    cpm5.add(1.0f);
    cpm15.add(cpm5.get());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f, cpm5.get() * 60.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f, cpm15.get() * 60.0f);
  }
}

static void alarm_cold(void) {
  TEST_ASSERT_FALSE(CounterMaths::alarm_over(100000, 50, false));
}

// over the limit, not at it; 0 = off
static void alarm_limit(void) {
  TEST_ASSERT_TRUE(CounterMaths::alarm_over(51, 50, true));
  TEST_ASSERT_FALSE(CounterMaths::alarm_over(50, 50, true));
  TEST_ASSERT_FALSE(CounterMaths::alarm_over(100000, 0, true));
}

// no alarm till the first minute is full
static void alarm_boot(void) {
  EGRingAvg<float, 60> ticks;
  ticks.begin(60);
  for (int s = 1; s <= 60; s++) {
    ticks.add(s <= 3 ? 2.0f : 0.0f);
    int cpm = (int)(ticks.get() * 60.0f + 0.5f);
    if (s == 1) TEST_ASSERT_TRUE(cpm > 50);
    TEST_ASSERT_FALSE(CounterMaths::alarm_over(cpm, 50, ticks.warm()));
  }
  for (int s = 0; s < 60; s++) ticks.add(2.0f);
  int cpm = (int)(ticks.get() * 60.0f + 0.5f);
  TEST_ASSERT_TRUE(CounterMaths::alarm_over(cpm, 50, ticks.warm()));
}

// full window multiplies, same answer as the divide
static void window_mean(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, CounterMaths::window_mean(0, 0, 60, 1.0f / 60.0f));
  for (uint32_t sum = 0; sum < 20000; sum += 7) {
    float want = (float)sum / 60.0f;
    TEST_ASSERT_FLOAT_WITHIN(want * 1e-6f, want, CounterMaths::window_mean(sum, 60, 60, 1.0f / 60.0f));
  }
  // warming up divides by what it has
  TEST_ASSERT_EQUAL_FLOAT(10.0f, CounterMaths::window_mean(30, 3, 60, 1.0f / 60.0f));
}

static void cps_same(void) {
  CounterMaths::CpsInputs in;
  TEST_ASSERT_FALSE(in.same(0, 0, 0));      // first is never same
  TEST_ASSERT_TRUE(in.same(0, 0, 0));
  TEST_ASSERT_FALSE(in.same(5, 0, 0));
  TEST_ASSERT_FALSE(in.same(5, 1, 0));
  TEST_ASSERT_FALSE(in.same(5, 1, 30));     // dead time moved
  TEST_ASSERT_TRUE(in.same(5, 1, 30));
  in.reset();
  TEST_ASSERT_FALSE(in.same(5, 1, 30));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(window_mean);
  RUN_TEST(cps_same);
  RUN_TEST(ratio_change);
  RUN_TEST(bad_ratio);
  RUN_TEST(alive_to_timeout);
  RUN_TEST(never_counted);
  RUN_TEST(observed_rate);
  RUN_TEST(dead_cap_latch);
  RUN_TEST(fast_tube);
  RUN_TEST(dead_time_zero);
  RUN_TEST(dead_time_model);
  RUN_TEST(dead_time_cap);
  RUN_TEST(dead_time_join);
  RUN_TEST(dead_time_bounds);
  RUN_TEST(counts_clamp);
  RUN_TEST(to_int_sat);
  RUN_TEST(x600_sat);
  RUN_TEST(dead_time_skip);
  RUN_TEST(long_tau);
  RUN_TEST(saturation_rate);
  RUN_TEST(saturation_off);
  RUN_TEST(threshold_bg);
  RUN_TEST(threshold_cal);
  RUN_TEST(threshold_cap);
  RUN_TEST(bg_low);
  RUN_TEST(bg_high);
  RUN_TEST(bg_first);
  RUN_TEST(bg_refused);
  RUN_TEST(bg_zeroed);
  RUN_TEST(quiet_floor);
  RUN_TEST(missing_fires);
  RUN_TEST(missing_60s);
  RUN_TEST(missing_quiet);
  RUN_TEST(missing_wrap);
  RUN_TEST(alive_wrap);
  RUN_TEST(total_wrap);
  RUN_TEST(bucket_edges);
  RUN_TEST(bucket_log2);
  RUN_TEST(ema_window);
  RUN_TEST(ema_first_sample);
  RUN_TEST(alarm_cold);
  RUN_TEST(alarm_limit);
  RUN_TEST(alarm_boot);
  return UNITY_END();
}
