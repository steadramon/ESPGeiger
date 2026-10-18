/*
  test_logger - the log ring and copying a line out of it.

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

#include "log_fakes.h"
#include <unity.h>
#include <string.h>
#include "Corium/Logger/Logger.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t last_idx(void) {
  uint8_t i = (uint8_t)Log::getLogIdx() - 1;
  return i ? i : 255;
}

static void copy_line(void) {
  Log::console(PSTR("hello %d"), 5);
  char buf[64];
  size_t n = Log::copyLog(last_idx(), buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 8);
  buf[n] = '\0';
  TEST_ASSERT_EQUAL_STRING("hello 5\1", buf + n - 8);
}

static void copy_cap(void) {
  char line[120];
  memset(line, 'x', sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  Log::console(PSTR("%s"), line);
  char buf[10];
  TEST_ASSERT_EQUAL_UINT(9, Log::copyLog(last_idx(), buf, sizeof(buf)));
}

static void copy_gone(void) {
  char buf[16];
  TEST_ASSERT_EQUAL_UINT(0, Log::copyLog(0, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT(0, Log::copyLog(last_idx() + 1, buf, sizeof(buf)));
}

static void debug_filtered(void) {
  char before = Log::getLogIdx();
  Log::debug(PSTR("quiet"));
  TEST_ASSERT_EQUAL_INT(before, Log::getLogIdx());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(copy_line);
  RUN_TEST(copy_cap);
  RUN_TEST(copy_gone);
  RUN_TEST(debug_filtered);
  return UNITY_END();
}
