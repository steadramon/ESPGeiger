/*
  test_bootguard - crash boots in a row and safe mode.

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
#include <string.h>

#include "../../ESPGeiger/src/Corium/System/BootGuardState.h"

using namespace BootGuard;

static constexpr uint32_t MAGIC = 0xE56B0014;
static constexpr uint32_t FW    = 0x1234;

static State s;

void setUp(void) {
  memset(&s, 0xA5, sizeof(s));   // RTC garbage
}
void tearDown(void) {}

static void boot(Ending end) { apply(s, MAGIC, FW, end); }
static void lived() { s.lived = 1; }   // what mark_lived does

static void garbage(void) {
  boot(END_OTHER);
  TEST_ASSERT_EQUAL_HEX32(MAGIC, s.magic);
  TEST_ASSERT_EQUAL_UINT8(0, s.lived);
  TEST_ASSERT_EQUAL_UINT8(0, s.crash_streak);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  TEST_ASSERT_EQUAL_UINT8(0, s.retries);
  TEST_ASSERT_EQUAL_UINT8(0, s.enter);
}

static void new_firmware(void) {
  boot(END_OTHER);
  for (int i = 0; i < 5; i++) boot(END_CRASH);
  s.retries = 3;
  apply(s, MAGIC, FW + 1, END_CRASH);
  TEST_ASSERT_EQUAL_HEX32(FW + 1, s.fw);
  TEST_ASSERT_EQUAL_UINT8(1, s.crash_streak);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  TEST_ASSERT_EQUAL_UINT8(0, s.retries);
}

static void three_crashes(void) {
  boot(END_OTHER);
  for (int i = 1; i <= 2; i++) {
    boot(END_CRASH);
    TEST_ASSERT_EQUAL_UINT8(i, s.crash_streak);
    TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  }
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
}

// crash after LIVED_S starts over
static void long_run(void) {
  boot(END_OTHER);
  for (int i = 0; i < 2; i++) boot(END_CRASH);
  lived();
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(0, s.crash_streak);
  TEST_ASSERT_EQUAL_UINT8(0, s.lived);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  for (int i = 0; i < 2; i++) boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
}

// lived is per boot
static void lived_cleared(void) {
  boot(END_OTHER);
  lived();
  boot(END_RESTART);
  TEST_ASSERT_EQUAL_UINT8(0, s.lived);
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.crash_streak);
}

// safe mode stays on a long run that crashes
static void safe_kept(void) {
  boot(END_OTHER);
  for (int i = 0; i < 3; i++) boot(END_CRASH);
  lived();
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
}

static void restart_clears(void) {
  boot(END_OTHER);
  for (int i = 0; i < 5; i++) boot(END_CRASH);
  boot(END_RESTART);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  TEST_ASSERT_EQUAL_UINT8(0, s.crash_streak);
  for (int i = 0; i < 5; i++) boot(END_CRASH);
  boot(END_OTHER);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
}

static void saturate(void) {
  boot(END_OTHER);
  for (int i = 0; i < 300; i++) boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(255, s.crash_streak);
}

static void endings(void) {
  TEST_ASSERT_EQUAL_UINT8(END_OTHER,   ending(0));
  TEST_ASSERT_EQUAL_UINT8(END_OTHER,   ending(1));
  TEST_ASSERT_EQUAL_UINT8(END_RESTART, ending(2));
  TEST_ASSERT_EQUAL_UINT8(END_RESTART, ending(3));
  TEST_ASSERT_EQUAL_UINT8(END_CRASH,   ending(4));
  TEST_ASSERT_EQUAL_UINT8(END_CRASH,   ending(5));
  TEST_ASSERT_EQUAL_UINT8(END_CRASH,   ending(6));
  TEST_ASSERT_EQUAL_UINT8(END_OTHER,   ending(7));
}

static void retry(void) {
  const uint32_t MIN = 60UL * 1000;
  TEST_ASSERT_EQUAL_UINT32(30 * MIN, retry_ms(0));
  TEST_ASSERT_EQUAL_UINT32(60 * MIN, retry_ms(1));
  TEST_ASSERT_EQUAL_UINT32(480 * MIN, retry_ms(4));
  TEST_ASSERT_EQUAL_UINT32(960 * MIN, retry_ms(5));
  TEST_ASSERT_EQUAL_UINT32(960 * MIN, retry_ms(255));
}

static void retries_kept(void) {
  boot(END_OTHER);
  s.retries = 2;
  boot(END_RESTART);
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(2, s.retries);
}

static void enter(void) {
  boot(END_OTHER);
  s.enter = 1;
  boot(END_RESTART);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
  boot(END_RESTART);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  s.enter = 1;
  boot(END_CRASH);
  TEST_ASSERT_EQUAL_UINT8(1, s.safe);
  boot(END_RESTART);
  s.enter = 1;
  boot(END_OTHER);
  TEST_ASSERT_EQUAL_UINT8(0, s.safe);
  TEST_ASSERT_EQUAL_UINT8(0, s.enter);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(garbage);
  RUN_TEST(new_firmware);
  RUN_TEST(three_crashes);
  RUN_TEST(long_run);
  RUN_TEST(lived_cleared);
  RUN_TEST(safe_kept);
  RUN_TEST(restart_clears);
  RUN_TEST(saturate);
  RUN_TEST(endings);
  RUN_TEST(retry);
  RUN_TEST(retries_kept);
  RUN_TEST(enter);
  return UNITY_END();
}
