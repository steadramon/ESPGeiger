/*
  test_tickclock.cpp - counted epoch seconds, read once a minute and at the boundary.

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
#include "Counter/TickClock.h"

static time_t s_real;
static int s_reads;

static time_t fake_time(time_t* out) {
  s_reads++;
  if (out) *out = s_real;
  return s_real;
}

void setUp(void) {
  s_real = 1000000;
  s_reads = 0;
}
void tearDown(void) {}

// one tick a second, the real clock moves with it
static time_t tick(TickClock& c, time_t boundary) {
  s_real++;
  return c.step(boundary, fake_time);
}

static void first_read(void) {
  TickClock c;
  TEST_ASSERT_EQUAL_INT64(1000000, c.step(2000000, fake_time));
  TEST_ASSERT_EQUAL_INT(1, s_reads);
}

static void counts(void) {
  TickClock c;
  c.step(2000000, fake_time);
  for (int i = 0; i < 30; i++) {
    time_t got = tick(c, 2000000);
    TEST_ASSERT_EQUAL_INT64(s_real, got);
  }
  TEST_ASSERT_EQUAL_INT(1, s_reads);
}

static void minute(void) {
  TickClock c;
  c.step(2000000, fake_time);
  for (int i = 0; i < 120; i++) tick(c, 2000000);
  TEST_ASSERT_EQUAL_INT(3, s_reads);
}

// a step lands at the next read
static void ntp_step(void) {
  TickClock c;
  c.step(2000000, fake_time);
  s_real += 500;
  time_t got = 0;
  for (int i = 0; i < TickClock::READ_EVERY; i++) got = tick(c, 2000000);
  TEST_ASSERT_EQUAL_INT64(s_real, got);
}

// counted ahead of a slow clock: the boundary waits for the real one
static void boundary(void) {
  TickClock c;
  c.step(1000005, fake_time);
  s_real -= 2;   // ticks ran fast
  for (int i = 0; i < 4; i++) tick(c, 1000005);   // counted to 1000004
  int before = s_reads;
  time_t got = tick(c, 1000005);
  TEST_ASSERT_GREATER_THAN_INT(before, s_reads);
  TEST_ASSERT_EQUAL_INT64(s_real, got);
  TEST_ASSERT_TRUE(got < 1000005);
}

static void no_boundary(void) {
  TickClock c;
  c.step(0, fake_time);
  tick(c, 0);
  tick(c, 0);
  TEST_ASSERT_EQUAL_INT(3, s_reads);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(first_read);
  RUN_TEST(counts);
  RUN_TEST(minute);
  RUN_TEST(ntp_step);
  RUN_TEST(boundary);
  RUN_TEST(no_boundary);
  return UNITY_END();
}
