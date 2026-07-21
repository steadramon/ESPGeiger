/*
  test_hostmodel - checks the host matches the target where tests rely on it.

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
#include <Arduino.h>
#include <stdint.h>

// macro breaks <algorithm>
#ifdef min
#error "min must not be a macro in the shim"
#endif
#ifdef max
#error "max must not be a macro in the shim"
#endif

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// needs -fno-signed-char
static void char_unsigned(void) {
  char c = (char)0x80;
  TEST_ASSERT_EQUAL_INT(128, (int)c);
  TEST_ASSERT_FALSE((char)-1 < 0);
}

static void clock_moves(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, millis());
  TEST_ASSERT_EQUAL_UINT32(0u, micros());

  eg_clock_advance_ms(1500);
  TEST_ASSERT_EQUAL_UINT32(1500u, millis());
  TEST_ASSERT_EQUAL_UINT32(1500000u, micros());

  delay(500);
  TEST_ASSERT_EQUAL_UINT32(2000u, millis());
}

static void clock_wraps(void) {
  eg_clock_set_us((uint64_t)0xFFFFFFFBu * 1000ULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFBu, millis());

  eg_clock_advance_ms(10);
  TEST_ASSERT_EQUAL_UINT32(5u, millis());

  eg_clock_set_us(0xFFFFFFFFULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, micros());
  eg_clock_advance_us(1);
  TEST_ASSERT_EQUAL_UINT32(0u, micros());
}

static void cycle_count(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, ESP.getCycleCount());
  eg_clock_advance_us(1000);
  TEST_ASSERT_EQUAL_UINT32(80000u, ESP.getCycleCount());   // 80 MHz
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(char_unsigned);
  RUN_TEST(clock_moves);
  RUN_TEST(clock_wraps);
  RUN_TEST(cycle_count);
  return UNITY_END();
}
