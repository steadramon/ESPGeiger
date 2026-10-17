/*
  test_lifecycle - events in the order added and the restart the main loop runs.

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
#include <string>
#include "Corium/System/Lifecycle.h"

static std::string s_log;

static void first(Lifecycle::Event e)  { s_log += "a" + std::to_string((int)e); }
static void second(Lifecycle::Event e) { s_log += "b" + std::to_string((int)e); }

void setUp(void) {}
void tearDown(void) {}

// Runs first, shared table
static void events(void) {
  TEST_ASSERT_TRUE(Lifecycle::on(first));
  TEST_ASSERT_TRUE(Lifecycle::on(second));
  TEST_ASSERT_FALSE(Lifecycle::on(nullptr));
  Lifecycle::send(Lifecycle::OTA_START);
  TEST_ASSERT_EQUAL_STRING("a1b1", s_log.c_str());
}

static void table_full(void) {
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(Lifecycle::on(first));
  TEST_ASSERT_FALSE(Lifecycle::on(first));
}

static void restart(void) {
  TEST_ASSERT_FALSE(Lifecycle::restart_due(5000));
  Lifecycle::request_restart(1000, 500);
  TEST_ASSERT_FALSE(Lifecycle::restart_due(1499));
  TEST_ASSERT_TRUE(Lifecycle::restart_due(1500));
  TEST_ASSERT_TRUE(Lifecycle::restart_due(9000));
}

static void restart_again(void) {
  Lifecycle::request_restart(1000, 100);
  Lifecycle::request_restart(1000, 1500);
  TEST_ASSERT_FALSE(Lifecycle::restart_due(2499));
  TEST_ASSERT_TRUE(Lifecycle::restart_due(2500));
}

static void restart_wrap(void) {
  Lifecycle::request_restart(0xFFFFFF00u, 0x200);
  TEST_ASSERT_FALSE(Lifecycle::restart_due(0xFFFFFFF0u));
  TEST_ASSERT_FALSE(Lifecycle::restart_due(0x000000FFu));
  TEST_ASSERT_TRUE(Lifecycle::restart_due(0x00000100u));
}

// 0 means none asked
static void restart_zero(void) {
  Lifecycle::request_restart(0xFFFFFFFFu, 1);
  TEST_ASSERT_TRUE(Lifecycle::restart_due(1));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(events);
  RUN_TEST(table_full);
  RUN_TEST(restart);
  RUN_TEST(restart_again);
  RUN_TEST(restart_wrap);
  RUN_TEST(restart_zero);
  return UNITY_END();
}
