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

// FULL is 255 here, 1023 on esp8266

#include <unity.h>
#include <Arduino.h>

#include "Corium/Output/PulseEngine.h"

static const uint16_t FULL = PulseEngine::FULL;

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

static void percent_endpoints(void) {
  TEST_ASSERT_EQUAL_UINT16(0,    PulseEngine::levelFromPercent(0));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(100));
}

static void percent_clamp(void) {
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(101));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(1000));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFromPercent(0xFFFFFFFFu));
}

static void from_8bit_endpoints(void) {
  TEST_ASSERT_EQUAL_UINT16(0,    PulseEngine::levelFrom8bit(0));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(255));
}

static void from_8bit_clamp(void) {
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(256));
  TEST_ASSERT_EQUAL_UINT16(FULL, PulseEngine::levelFrom8bit(0xFFFFFFFFu));
}

static void half_input(void) {
  const uint16_t tol = (uint16_t)(FULL / 100) + 1;
  TEST_ASSERT_UINT16_WITHIN(tol, FULL / 2, PulseEngine::levelFromPercent(50));
  TEST_ASSERT_UINT16_WITHIN(tol, FULL / 2, PulseEngine::levelFrom8bit(128));
}

static void monotonic(void) {
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

static void default_level(void) {
  PulseEngine e;
  TEST_ASSERT_EQUAL_UINT16(FULL, e.active_level);
}

static void zero_burst_freq(void) {
  PulseEngine e;
  e.voice_freq = 0.0f;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT32(1, e.burst_freq_eff);
}

// prefs go in raw, 70 ms must not wrap into range
static void limits(void) {
  PulseEngine e;
  e.mode = 7;
  e.pulse_us = 70000;
  e.freq_hz = 0;
  e.cycles = 0;
  e.polarity = 5;
  e.fade_shift = 9;
  e.max_hz = 300;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT8(PulseEngine::MODE_PULSE, e.mode);
  TEST_ASSERT_EQUAL_UINT32(PulseEngine::PULSE_US_MAX, e.pulse_us);
  TEST_ASSERT_EQUAL_UINT32(PulseEngine::FREQ_MIN, e.freq_hz);
  TEST_ASSERT_EQUAL_UINT32(1, e.cycles);
  TEST_ASSERT_EQUAL_UINT8(0, e.polarity);
  TEST_ASSERT_EQUAL_UINT8(PulseEngine::FADE_SHIFT_DEFAULT, e.fade_shift);
  TEST_ASSERT_EQUAL_UINT16(1000 / PulseEngine::MAX_HZ_MAX, e.token_interval_ms);
  e.pulse_us = 100;
  e.freq_hz = 9000;
  e.cycles = 11;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT32(PulseEngine::PULSE_US_MIN, e.pulse_us);
  TEST_ASSERT_EQUAL_UINT32(PulseEngine::FREQ_MAX, e.freq_hz);
  TEST_ASSERT_EQUAL_UINT32(PulseEngine::CYCLES_MAX, e.cycles);
}

static void token_interval(void) {
  PulseEngine e;
  e.max_hz = 20;  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(50, e.token_interval_ms);
  e.max_hz = 200; e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(5, e.token_interval_ms);
  // 0 = off
  e.max_hz = 0;   e.commitConfig();
  TEST_ASSERT_EQUAL_UINT16(0, e.token_interval_ms);
}

static void voice_jitter(void) {
  PulseEngine e;
  e.pulse_us = 5000; e.voice_pulse = 1.5f;
  e.freq_hz  = 2000; e.voice_freq  = 0.5f;
  e.commitConfig();
  TEST_ASSERT_EQUAL_UINT32(7500, e.pulse_us_eff);
  TEST_ASSERT_EQUAL_UINT32(1000, e.burst_freq_eff);
}

static PulseEngine makeEngine(void) {
  PulseEngine e;
  e.pin = 2;
  e.mode = PulseEngine::MODE_PULSE;
  e.max_hz = 20;            // 50 ms per token
  e.commitConfig();
  return e;
}

static void click_no_pin(void) {
  PulseEngine e = makeEngine();
  e.pin = -1;
  TEST_ASSERT_FALSE(e.take(10000));
}

static void click_no_tokens(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  TEST_ASSERT_FALSE(e.take(10000));
}

static void tokens_accrue(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  TEST_ASSERT_TRUE(e.take(10050));
  TEST_ASSERT_EQUAL_UINT8(0, e.tokens);
}

static void tokens_cap(void) {
  PulseEngine e = makeEngine();
  e.last_token_ms = 10000;
  e.tokens = 0;
  TEST_ASSERT_TRUE(e.take(10000 + 50 * 100));
  TEST_ASSERT_EQUAL_UINT8(4, e.tokens);
}

// the loop side never touches the pin
static void take_only(void) {
  PulseEngine e = makeEngine();
  e.tokens = 5;
  TEST_ASSERT_TRUE(e.take(10000));
  TEST_ASSERT_FALSE(e.busy());
  TEST_ASSERT_FALSE(e.pin_high);
  e.start();
  TEST_ASSERT_TRUE(e.busy());
  TEST_ASSERT_TRUE(e.pin_high);
}

static void click_busy(void) {
  PulseEngine e = makeEngine();
  e.phases_remaining = 3;
  e.next_us = 777;
  e.start();
  TEST_ASSERT_EQUAL_UINT8(3, e.phases_remaining);
  TEST_ASSERT_EQUAL_UINT32(777, e.next_us);
}

static void fade_busy(void) {
  PulseEngine e = makeEngine();
  e.mode = PulseEngine::MODE_FADE;
  e.start();
  TEST_ASSERT_TRUE(e.busy());
  e.brightness = 0;
  TEST_ASSERT_FALSE(e.busy());
}

static void unthrottled(void) {
  PulseEngine e = makeEngine();
  e.max_hz = 0;
  e.commitConfig();
  TEST_ASSERT_TRUE(e.take(10000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(percent_endpoints);
  RUN_TEST(percent_clamp);
  RUN_TEST(from_8bit_endpoints);
  RUN_TEST(from_8bit_clamp);
  RUN_TEST(half_input);
  RUN_TEST(monotonic);
  RUN_TEST(default_level);
  RUN_TEST(zero_burst_freq);
  RUN_TEST(limits);
  RUN_TEST(token_interval);
  RUN_TEST(voice_jitter);
  RUN_TEST(click_no_pin);
  RUN_TEST(click_no_tokens);
  RUN_TEST(tokens_accrue);
  RUN_TEST(tokens_cap);
  RUN_TEST(take_only);
  RUN_TEST(click_busy);
  RUN_TEST(fade_busy);
  RUN_TEST(unthrottled);
  return UNITY_END();
}
