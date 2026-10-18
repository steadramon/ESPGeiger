/*
  test_mathutil - clamp, brightness and the Poisson helpers.

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

#include "Corium/Util/MathUtil.h"

void setUp(void)    {}
void tearDown(void) {}

static void clamp_range(void) {
  TEST_ASSERT_EQUAL_INT(5,  clamp(5,  0, 10));
  TEST_ASSERT_EQUAL_INT(0,  clamp(0,  0, 10));
  TEST_ASSERT_EQUAL_INT(10, clamp(10, 0, 10));
  TEST_ASSERT_EQUAL_INT(0,  clamp(-1, 0, 10));
  TEST_ASSERT_EQUAL_INT(10, clamp(11, 0, 10));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clamp(0.5f, 0.0f, 1.0f));
}

static void std_sqrt(void) {
  TEST_ASSERT_EQUAL_FLOAT(10.0f, poisson_std(100.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,  poisson_std(1.0f));
}

static void std_negative(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(-1.0f));
}

static void z_sigmas(void) {
  // expected 100 -> sigma 10
  TEST_ASSERT_EQUAL_FLOAT(0.0f,  poisson_z(100.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,  poisson_z(110.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(-2.0f, poisson_z(80.0f,  100.0f));
  TEST_ASSERT_EQUAL_FLOAT(-10.0f, poisson_z(0.0f,  100.0f));
}

static void z_no_expected(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(500.0f, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(500.0f, -3.0f));
}

static void bright_curve(void) {
  TEST_ASSERT_EQUAL_UINT32(0,   brightness_curve(0,   255));
  TEST_ASSERT_EQUAL_UINT32(64,  brightness_curve(50,  255));
  TEST_ASSERT_EQUAL_UINT32(255, brightness_curve(100, 255));
  TEST_ASSERT_EQUAL_UINT32(255, brightness_curve(150, 255));
  TEST_ASSERT_EQUAL_UINT32(4,   brightness_curve(50,  15));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(clamp_range);
  RUN_TEST(bright_curve);
  RUN_TEST(std_sqrt);
  RUN_TEST(std_negative);
  RUN_TEST(z_sigmas);
  RUN_TEST(z_no_expected);
  return UNITY_END();
}
