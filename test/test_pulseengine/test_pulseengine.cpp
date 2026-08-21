/*
  test_pulseengine - level scaling and the click token bucket.

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

// The level helpers are the whole reason this suite exists. active_level is
// FULL-scale, and FULL is 1023 on ESP8266; a caller assigning a raw 8-bit value
// lands at FULL/4 and drops off the digitalWrite path, so the LED goes dim or
// dark instead of bright. Naming FULL, levelFromPercent and levelFrom8bit here
// also means removing them is a build failure rather than a dark LED.
//
// FULL is 255 on the host, so these assert relationships to FULL rather than
// absolute values. The endpoint cases are the load-bearing ones.

#include <unity.h>
#include <Arduino.h>

#include "Util/PulseEngine.h"

static const uint16_t FULL = PulseEngine::FULL;

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// --- level scaling ---

static void test_percent_endpoints(void) {
  TEST_ASSERT_EQUAL_UINT16(0,    PulseEngine::levelFromPercent(0));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(100));
}

static void test_percent_clamps_above_100(void) {
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(101));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(1000));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(0xFFFFFFFFu));
}

static void test_8bit_endpoints(void) {
  TEST_ASSERT_EQUAL_UINT16(0,    PulseEngine::levelFrom8bit(0));
  // The regression this guards: 255 must reach FULL, not stay 255.
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(255));
}

static void test_8bit_clamps_above_255(void) {
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(256));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(0xFFFFFFFFu));
}

// Half the input is half the output, to within 1% of full scale. Stated as a
// property rather than the formula: repeating the expression here would pass
// for any formula, including a wrong one.
static void test_half_input_gives_half_output(void) {
  const uint16_t tol = (uint16_t)(FULL / 100) + 1;
  TEST_ASSERT_UINT16_WITHIN(tol, FULL / 2, PulseEngine::levelFromPercent(50));
  TEST_ASSERT_UINT16_WITHIN(tol, FULL / 2, PulseEngine::levelFrom8bit(128));
}

static void test_scaling_is_monotonic(void) {
  uint16_t prev = 0;
  for (uint32_t p = 0; p <= 100; p++) {
    uint16_t v = PulseEngine::levelFromPercent(p);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(prev, v);
    prev = v;
  }
  prev = 0;
  for (uint32_t v8 = 0; v8 <= 255; v8++) {
    uint16_t v = PulseEngine::levelFrom8bit(v8);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(prev, v);
    prev = v;
  }
}

// A default-constructed engine must already sit on the digitalWrite path.
static void test_default_active_level_is_full(void) {
  PulseEngine e;
  TEST_ASSERT_EQUAL_UINT16(FULL, e.active_level);
}

// --- commitConfig ---

static void test_commit_guards_zero_burst_freq(void) {
  PulseEngine e;
  e.freq_hz = 0;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT32(1, e.burst_freq_eff);
}

static void test_commit_token_interval_from_max_hz(void) {
  PulseEngine e;
  e.max_hz = 20;  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(50, e.token_interval_ms);
  e.max_hz = 200; e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(5, e.token_interval_ms);
  // 0 disables throttling entirely rather than dividing by zero.
  e.max_hz = 0;   e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(0, e.token_interval_ms);
}

static void test_commit_applies_voice_jitter(void) {
  PulseEngine e;
  e.pulse_us = 5000; e.voice_pulse = 1.5f;
  e.freq_hz  = 2000; e.voice_freq  = 0.5f;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT32(7500, e.pulse_us_eff);
  TEST_ASSERT_EQUAL_UINT32(1000, e.burst_freq_eff);
}

// --- notifyClick token bucket ---

static PulseEngine makeEngine(void) {
  PulseEngine e;
  e.pin = 2;
  e.mode = PulseEngine::MODE_PULSE;
  e.max_hz = 20;            // 50 ms per token
  e.commitConfig();
  return e;
}

static void test_click_declines_without_pin(void) {
  PulseEngine e = makeEngine();
  e.pin = -1;
  TEST_ASSERT_FALSE(e.notifyClick(10000));
}

static void test_click_declines_with_no_tokens(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  // Same instant as the last token: nothing has accrued.
  TEST_ASSERT_FALSE(e.notifyClick(10000));
}

static void test_tokens_accrue_at_the_interval(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  TEST_ASSERT_TRUE(e.notifyClick(10050));
  TEST_ASSERT_EQUAL_UINT8(0, e.tokens);
}

// A long idle gap must not bank unlimited clicks. 100 intervals accrue, the
// bucket stops at 5, and the click being served spends one.
static void test_tokens_cap_at_five(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  TEST_ASSERT_TRUE(e.notifyClick(10000 + 50 * 100));
  TEST_ASSERT_EQUAL_UINT8(4, e.tokens);
}

static void test_click_declines_while_busy(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 5;
  e.phases_remaining = 3;
  TEST_ASSERT_FALSE(e.notifyClick(10050));
}

static void test_unthrottled_engine_always_fires(void) {
  PulseEngine e = makeEngine();
  e.max_hz = 0;
  e.commitConfig();
  TEST_ASSERT_TRUE(e.notifyClick(10000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_percent_endpoints);
  RUN_TEST(test_percent_clamps_above_100);
  RUN_TEST(test_8bit_endpoints);
  RUN_TEST(test_8bit_clamps_above_255);
  RUN_TEST(test_half_input_gives_half_output);
  RUN_TEST(test_scaling_is_monotonic);
  RUN_TEST(test_default_active_level_is_full);
  RUN_TEST(test_commit_guards_zero_burst_freq);
  RUN_TEST(test_commit_token_interval_from_max_hz);
  RUN_TEST(test_commit_applies_voice_jitter);
  RUN_TEST(test_click_declines_without_pin);
  RUN_TEST(test_click_declines_with_no_tokens);
  RUN_TEST(test_tokens_accrue_at_the_interval);
  RUN_TEST(test_tokens_cap_at_five);
  RUN_TEST(test_click_declines_while_busy);
  RUN_TEST(test_unthrottled_engine_always_fires);
  return UNITY_END();
}
