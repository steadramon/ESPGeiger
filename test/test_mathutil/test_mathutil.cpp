/*
  test_mathutil - clamp and the Poisson helpers.

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

// poisson_z backs the counts_missing advisory, so the guard values matter more
// than the arithmetic: a divide by a zero sigma or a NaN reaching a threshold
// compare would read as a dead tube.

#include <unity.h>
#include <Arduino.h>
#include <math.h>

#include "Util/MathUtil.h"

void setUp(void)    {}
void tearDown(void) {}

// --- clamp ------------------------------------------------------------------

static void test_clamp_passes_through_inside_range(void) {
  TEST_ASSERT_EQUAL_INT(5, clamp(5, 0, 10));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clamp(0.5f, 0.0f, 1.0f));
}

static void test_clamp_bounds_are_inclusive(void) {
  TEST_ASSERT_EQUAL_INT(0,  clamp(0,  0, 10));
  TEST_ASSERT_EQUAL_INT(10, clamp(10, 0, 10));
}

static void test_clamp_saturates_outside_range(void) {
  TEST_ASSERT_EQUAL_INT(0,  clamp(-1, 0, 10));
  TEST_ASSERT_EQUAL_INT(10, clamp(11, 0, 10));
  TEST_ASSERT_EQUAL_INT(-10, clamp(-50, -10, -1));
}

static void test_clamp_degenerate_range_yields_the_point(void) {
  TEST_ASSERT_EQUAL_INT(7, clamp(3, 7, 7));
  TEST_ASSERT_EQUAL_INT(7, clamp(9, 7, 7));
}

static void test_clamp_holds_for_unsigned_and_wide_types(void) {
  TEST_ASSERT_EQUAL_UINT8(200u, clamp<uint8_t>(255u, 0u, 200u));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, clamp<uint32_t>(4294967295u, 0u, 4294967295u));
  TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, clamp<int32_t>(-2147483647 - 1, -2147483647 - 1, 0));
}

// Literal-bound calls must fold, or the hot-path callers pay a real call.
static void test_clamp_folds_at_compile_time(void) {
  static_assert(clamp(11, 0, 10) == 10, "clamp must be usable in a constant expression");
  static_assert(clamp(-1, 0, 10) == 0,  "clamp must be usable in a constant expression");
  TEST_PASS();
}

// --- poisson_std ------------------------------------------------------------

static void test_poisson_std_is_sqrt_n(void) {
  TEST_ASSERT_EQUAL_FLOAT(10.0f, poisson_std(100.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,  poisson_std(1.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 24.4949f, poisson_std(600.0f));
}

static void test_poisson_std_floors_at_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(-1.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(-1e9f));
}

// sqrtf(negative) is NaN, so the N > 0 guard is what keeps a NaN out of every
// downstream threshold compare.
static void test_poisson_std_never_returns_nan(void) {
  TEST_ASSERT_FALSE(isnan(poisson_std(-4.0f)));
  TEST_ASSERT_FALSE(isnan(poisson_std(0.0f)));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_std(NAN));   // NAN > 0 is false
}

// --- poisson_z --------------------------------------------------------------

static void test_poisson_z_is_zero_when_observed_matches(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(100.0f, 100.0f));
}

static void test_poisson_z_counts_sigmas(void) {
  // expected 100 -> sigma 10
  TEST_ASSERT_EQUAL_FLOAT(1.0f,  poisson_z(110.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(3.0f,  poisson_z(130.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(-2.0f, poisson_z(80.0f,  100.0f));
}

static void test_poisson_z_sign_marks_direction(void) {
  TEST_ASSERT_TRUE(poisson_z(150.0f, 100.0f) > 0.0f);   // more counts than expected
  TEST_ASSERT_TRUE(poisson_z(50.0f,  100.0f) < 0.0f);   // fewer, the dead-tube direction
}

// A zero or negative expectation has no meaningful sigma. Returning 0 keeps
// "no evidence" from reading as a large deviation at either sign.
static void test_poisson_z_guards_a_zero_expectation(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(500.0f, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(0.0f,   0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, poisson_z(500.0f, -3.0f));
}

// Zero observed against a real expectation is the dead-tube signal itself and
// must NOT be swallowed by a guard.
static void test_poisson_z_reports_a_silent_tube(void) {
  TEST_ASSERT_EQUAL_FLOAT(-10.0f, poisson_z(0.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(-30.0f, poisson_z(0.0f, 900.0f));
}

// Sensitivity scales as sqrt(N): the same fractional drop is more significant
// from a larger expectation. Hence the advisory needs a count floor, not a
// fixed CPM threshold.
static void test_poisson_z_scales_with_expectation(void) {
  float small = poisson_z(9.0f,   10.0f);      // -10% of 10
  float large = poisson_z(900.0f, 1000.0f);    // -10% of 1000
  TEST_ASSERT_TRUE(fabsf(large) > fabsf(small));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.3162f, small);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.1623f, large);
}

// A NaN observation propagates: every comparison against it is false, so a
// threshold test silently reports "not deviating". Callers must not feed one.
static void test_poisson_z_propagates_a_nan_observation(void) {
  TEST_ASSERT_TRUE(isnan(poisson_z(NAN, 100.0f)));
  TEST_ASSERT_FALSE(poisson_z(NAN, 100.0f) < -3.0f);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_passes_through_inside_range);
  RUN_TEST(test_clamp_bounds_are_inclusive);
  RUN_TEST(test_clamp_saturates_outside_range);
  RUN_TEST(test_clamp_degenerate_range_yields_the_point);
  RUN_TEST(test_clamp_holds_for_unsigned_and_wide_types);
  RUN_TEST(test_clamp_folds_at_compile_time);
  RUN_TEST(test_poisson_std_is_sqrt_n);
  RUN_TEST(test_poisson_std_floors_at_zero);
  RUN_TEST(test_poisson_std_never_returns_nan);
  RUN_TEST(test_poisson_z_is_zero_when_observed_matches);
  RUN_TEST(test_poisson_z_counts_sigmas);
  RUN_TEST(test_poisson_z_sign_marks_direction);
  RUN_TEST(test_poisson_z_guards_a_zero_expectation);
  RUN_TEST(test_poisson_z_reports_a_silent_tube);
  RUN_TEST(test_poisson_z_scales_with_expectation);
  RUN_TEST(test_poisson_z_propagates_a_nan_observation);
  return UNITY_END();
}
