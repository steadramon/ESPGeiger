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

// none below 50 cps
static void dead_time_skip(void) {
  m.set_dead_time_us(100);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, m.apply_dead_time(50.0f));
  float just_over = m.apply_dead_time(50.01f);
  TEST_ASSERT_TRUE(just_over > 50.01f);
  TEST_ASSERT_TRUE((just_over / 50.01f) < 1.006f);
}

static void long_tau(void) {
  m.set_dead_time_us(1000);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, m.apply_dead_time(50.0f));
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

// over the limit, not at it
static void alarm_limit(void) {
  TEST_ASSERT_TRUE(CounterMaths::alarm_over(51, 50, true));
  TEST_ASSERT_FALSE(CounterMaths::alarm_over(50, 50, true));
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

int main(void) {
  UNITY_BEGIN();
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
  RUN_TEST(dead_time_skip);
  RUN_TEST(long_tau);
  RUN_TEST(saturation_rate);
  RUN_TEST(saturation_off);
  RUN_TEST(threshold_bg);
  RUN_TEST(threshold_cal);
  RUN_TEST(threshold_cap);
  RUN_TEST(missing_fires);
  RUN_TEST(missing_60s);
  RUN_TEST(missing_quiet);
  RUN_TEST(missing_wrap);
  RUN_TEST(alive_wrap);
  RUN_TEST(bucket_edges);
  RUN_TEST(bucket_log2);
  RUN_TEST(ema_window);
  RUN_TEST(ema_first_sample);
  RUN_TEST(alarm_cold);
  RUN_TEST(alarm_limit);
  RUN_TEST(alarm_boot);
  return UNITY_END();
}
