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

#include <unity.h>
#include <stdint.h>

#include "Corium/System/Uptime.h"
#include "Corium/System/UptimeCounter.h"

void setUp(void)    {}
void tearDown(void) {}

// true period 4294967.296 s
static const uint32_t WRAP_S   = 4294967u;
static const uint64_t WRAP_MS  = 4294967296ULL;

static uint32_t millis_at(uint64_t true_ms) { return (uint32_t)true_ms; }

static void before_wrap(void) {
  UptimeCounter u;
  const uint64_t points[] = { 0, 1, 999, 1000, 1001, 59999, 60000, 86400000ULL };
  for (uint64_t ms : points) {
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(ms / 1000ULL), u.tick(millis_at(ms)));
  }
  TEST_ASSERT_EQUAL_UINT16(0, u.wraps());
}

static void seconds_truncate(void) {
  UptimeCounter u;
  TEST_ASSERT_EQUAL_UINT32(0, u.tick(999));
  TEST_ASSERT_EQUAL_UINT32(1, u.tick(1000));
  TEST_ASSERT_EQUAL_UINT32(1, u.tick(1999));
  TEST_ASSERT_EQUAL_UINT32(2, u.tick(2000));
}

static void single_wrap(void) {
  UptimeCounter u;
  u.tick(millis_at(WRAP_MS - 2000));
  TEST_ASSERT_EQUAL_UINT16(0, u.wraps());

  uint32_t after = u.tick(millis_at(WRAP_MS + 1000));
  TEST_ASSERT_EQUAL_UINT16(1, u.wraps());
  TEST_ASSERT_EQUAL_UINT32(WRAP_S + 1, after);
}

static void repeat_calls(void) {
  UptimeCounter u;
  u.tick(millis_at(WRAP_MS - 500));

  uint32_t first = u.tick(millis_at(WRAP_MS + 10));
  for (int i = 0; i < 50; i++) {
    uint32_t again = u.tick(millis_at(WRAP_MS + 10 + i));
    TEST_ASSERT_EQUAL_UINT16(1, u.wraps());
    TEST_ASSERT_EQUAL_UINT32(first, again);
  }
}

// needs a tick per wrap
static void many_wraps(void) {
  UptimeCounter u;
  const uint64_t STEP = WRAP_MS / 4;

  for (int w = 0; w <= 10; w++) {
    for (int q = 0; q < 4; q++) {
      u.tick(millis_at((uint64_t)w * WRAP_MS + q * STEP));
    }
    const uint64_t true_ms = (uint64_t)w * WRAP_MS + 3 * STEP + 5000ULL;
    uint32_t got = u.tick(millis_at(true_ms));
    TEST_ASSERT_EQUAL_UINT16(w, u.wraps());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(true_ms / 1000ULL), got);
  }
}

static void remainder_carries(void) {
  UptimeCounter u;
  uint64_t t_ms = WRAP_MS - 3700;
  u.tick(millis_at(t_ms));
  const uint32_t steps[] = { 1, 333, 999, 1000, 1001, 1777, 250, 4999 };
  for (uint32_t s : steps) {
    t_ms += s;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(t_ms / 1000ULL), u.tick(millis_at(t_ms)));
  }
  TEST_ASSERT_EQUAL_UINT16(1, u.wraps());
}

static void split(void) {
  Uptime::Split u = Uptime::split(2 * 86400 + 3 * 3600 + 4 * 60 + 5);
  TEST_ASSERT_EQUAL_UINT16(2, u.d);
  TEST_ASSERT_EQUAL_UINT8(3, u.h);
  TEST_ASSERT_EQUAL_UINT8(4, u.m);
  TEST_ASSERT_EQUAL_UINT8(5, u.s);
  u = Uptime::split(86399);
  TEST_ASSERT_EQUAL_UINT16(0, u.d);
  TEST_ASSERT_EQUAL_UINT8(23, u.h);
  TEST_ASSERT_EQUAL_UINT8(59, u.m);
  TEST_ASSERT_EQUAL_UINT8(59, u.s);
  TEST_ASSERT_EQUAL_UINT16(49710, Uptime::split(UINT32_MAX).d);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(before_wrap);
  RUN_TEST(seconds_truncate);
  RUN_TEST(single_wrap);
  RUN_TEST(repeat_calls);
  RUN_TEST(many_wraps);
  RUN_TEST(remainder_carries);
  RUN_TEST(split);
  return UNITY_END();
}
