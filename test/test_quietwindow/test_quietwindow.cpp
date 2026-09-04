/*
  test_quietwindow - daily quiet windows, including ones that cross midnight.

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

#include "Corium/Output/QuietWindow.h"

void setUp(void)    {}
void tearDown(void) {}

static int16_t at(int h, int m) { return (int16_t)(h * 60 + m); }

static void parse_times(void) {
  TEST_ASSERT_EQUAL_INT16(0, quiet_minute("00:00"));
  TEST_ASSERT_EQUAL_INT16(1350, quiet_minute("22:30"));
  TEST_ASSERT_EQUAL_INT16(1439, quiet_minute("23:59"));
}

static void bad_times(void) {
  TEST_ASSERT_EQUAL_INT16(-1, quiet_minute(""));
  TEST_ASSERT_EQUAL_INT16(-1, quiet_minute(nullptr));
  TEST_ASSERT_EQUAL_INT16(-1, quiet_minute("7:30"));
  TEST_ASSERT_EQUAL_INT16(-1, quiet_minute("24:00"));
  TEST_ASSERT_EQUAL_INT16(-1, quiet_minute("ab:cd"));
}

static void no_window(void) {
  TEST_ASSERT_FALSE(quiet_in_window(at(12, 0), -1, at(7, 0)));
  TEST_ASSERT_FALSE(quiet_in_window(at(12, 0), at(22, 0), -1));
  TEST_ASSERT_FALSE(quiet_in_window(at(12, 0), at(12, 0), at(12, 0)));
}

static void same_day_window(void) {
  int16_t from = at(9, 0), to = at(17, 0);
  TEST_ASSERT_FALSE(quiet_in_window(at(8, 59), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(9, 0), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(16, 59), from, to));
  TEST_ASSERT_FALSE(quiet_in_window(at(17, 0), from, to));
}

static void over_midnight(void) {
  int16_t from = at(22, 0), to = at(7, 0);
  TEST_ASSERT_FALSE(quiet_in_window(at(21, 59), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(22, 0), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(23, 59), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(0, 0), from, to));
  TEST_ASSERT_TRUE(quiet_in_window(at(6, 59), from, to));
  TEST_ASSERT_FALSE(quiet_in_window(at(7, 0), from, to));
  TEST_ASSERT_FALSE(quiet_in_window(at(12, 0), from, to));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(parse_times);
  RUN_TEST(bad_times);
  RUN_TEST(no_window);
  RUN_TEST(same_day_window);
  RUN_TEST(over_midnight);
  return UNITY_END();
}
