/*
  test_externalpause - the auto-expiring pause on external posters.

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

#include "Corium/Module/ExternalPause.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/FastMillis.h"

volatile uint32_t _fast_ms_counter = 0;

void Log::console(const char*, ...) {}

void setUp(void) {
  _fast_ms_counter = 0;
  ExternalPause::start(0);
}
void tearDown(void) {}

static void off_until_started(void) {
  TEST_ASSERT_FALSE(ExternalPause::active());
  TEST_ASSERT_EQUAL_UINT32(0, ExternalPause::remaining_ms());
}

static void runs_timeout(void) {
  _fast_ms_counter = 5000;
  ExternalPause::start(1000);
  _fast_ms_counter = 5999;
  TEST_ASSERT_TRUE(ExternalPause::active());
  TEST_ASSERT_EQUAL_UINT32(1, ExternalPause::remaining_ms());
  _fast_ms_counter = 6000;
  TEST_ASSERT_FALSE(ExternalPause::active());
  TEST_ASSERT_EQUAL_UINT32(0, ExternalPause::remaining_ms());
}

static void zero_resumes(void) {
  ExternalPause::start(60000);
  TEST_ASSERT_TRUE(ExternalPause::active());
  ExternalPause::start(0);
  TEST_ASSERT_FALSE(ExternalPause::active());
}

static void millis_wrap(void) {
  _fast_ms_counter = 0xFFFFFE0C;
  ExternalPause::start(1000);
  _fast_ms_counter = 0xFFFFFF9C;
  TEST_ASSERT_TRUE(ExternalPause::active());
  TEST_ASSERT_EQUAL_UINT32(600, ExternalPause::remaining_ms());
  _fast_ms_counter = 0x1F3;
  TEST_ASSERT_TRUE(ExternalPause::active());
  _fast_ms_counter = 0x1F4;
  TEST_ASSERT_FALSE(ExternalPause::active());
}

// 0 also means not paused
static void end_at_zero(void) {
  _fast_ms_counter = 0xFFFFFC18;
  ExternalPause::start(1000);
  _fast_ms_counter = 0xFFFFFF00;
  TEST_ASSERT_TRUE(ExternalPause::active());
  _fast_ms_counter = 1;
  TEST_ASSERT_FALSE(ExternalPause::active());
}

// junk keeps the running pause
static void secs_junk(void) {
  ExternalPause::start(60000);
  TEST_ASSERT_FALSE(ExternalPause::start_s("abc"));
  TEST_ASSERT_FALSE(ExternalPause::start_s(""));
  TEST_ASSERT_FALSE(ExternalPause::start_s("5s"));
  TEST_ASSERT_FALSE(ExternalPause::start_s(nullptr));
  TEST_ASSERT_EQUAL_UINT32(60000, ExternalPause::remaining_ms());
}

static void secs_range(void) {
  TEST_ASSERT_TRUE(ExternalPause::start_s("30"));
  TEST_ASSERT_EQUAL_UINT32(30000, ExternalPause::remaining_ms());
  TEST_ASSERT_TRUE(ExternalPause::start_s("999999"));
  TEST_ASSERT_EQUAL_UINT32(86400000, ExternalPause::remaining_ms());
  TEST_ASSERT_TRUE(ExternalPause::start_s("0"));
  TEST_ASSERT_FALSE(ExternalPause::active());
  ExternalPause::start(60000);
  TEST_ASSERT_TRUE(ExternalPause::start_s("-5"));
  TEST_ASSERT_FALSE(ExternalPause::active());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(off_until_started);
  RUN_TEST(runs_timeout);
  RUN_TEST(zero_resumes);
  RUN_TEST(millis_wrap);
  RUN_TEST(end_at_zero);
  RUN_TEST(secs_junk);
  RUN_TEST(secs_range);
  return UNITY_END();
}
