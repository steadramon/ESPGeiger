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

static uint32_t s_real;
static int s_reads;

void setUp(void) {
  s_real = 1000000;
  s_reads = 0;
}
void tearDown(void) {}

// as the Counter: count, else read time()
static uint32_t step(TickClock& c, uint32_t boundary) {
  if (c.count()) return c.now;
  s_reads++;
  c.read(s_real, boundary);
  return c.now;
}

// one tick a second, the real clock moves with it
static uint32_t tick(TickClock& c, uint32_t boundary) {
  s_real++;
  return step(c, boundary);
}

static void first_read(void) {
  TickClock c;
  TEST_ASSERT_EQUAL_INT64(1000000, step(c, 2000000));
  TEST_ASSERT_EQUAL_INT(1, s_reads);
}

static void counts(void) {
  TickClock c;
  step(c, 2000000);
  for (int i = 0; i < 30; i++) {
    uint32_t got = tick(c, 2000000);
    TEST_ASSERT_EQUAL_INT64(s_real, got);
  }
  TEST_ASSERT_EQUAL_INT(1, s_reads);
}

static void minute(void) {
  TickClock c;
  step(c, 2000000);
  for (int i = 0; i < 120; i++) tick(c, 2000000);
  TEST_ASSERT_EQUAL_INT(3, s_reads);
}

// a step lands at the next read
static void ntp_step(void) {
  TickClock c;
  step(c, 2000000);
  s_real += 500;
  uint32_t got = 0;
  for (int i = 0; i < TickClock::READ_EVERY; i++) got = tick(c, 2000000);
  TEST_ASSERT_EQUAL_INT64(s_real, got);
}

// counted ahead of a slow clock: the boundary waits for the real one
static void boundary(void) {
  TickClock c;
  step(c, 1000005);
  s_real -= 2;   // ticks ran fast
  for (int i = 0; i < 4; i++) tick(c, 1000005);   // counted to 1000004
  int before = s_reads;
  uint32_t got = tick(c, 1000005);
  TEST_ASSERT_GREATER_THAN_INT(before, s_reads);
  TEST_ASSERT_EQUAL_INT64(s_real, got);
  TEST_ASSERT_TRUE(got < 1000005);
}

static void no_boundary(void) {
  TickClock c;
  step(c, 0);
  tick(c, 0);
  tick(c, 0);
  TEST_ASSERT_EQUAL_INT(3, s_reads);
}

// the hour after the 2106 wrap is still ahead: once a minute, not every tick
static void wrap(void) {
  TickClock c;
  s_real = 4294967290UL;
  step(c, 1904);
  for (int i = 0; i < 10; i++) tick(c, 1904);
  TEST_ASSERT_EQUAL_INT(1, s_reads);
  TEST_ASSERT_EQUAL_UINT32(4, c.now);
}

// a stepped clock reads on the next tick
static void read_next(void) {
  TickClock c;
  step(c, 2000000);
  c.read_next();
  tick(c, 2000000);
  TEST_ASSERT_EQUAL_INT(2, s_reads);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(first_read);
  RUN_TEST(counts);
  RUN_TEST(minute);
  RUN_TEST(ntp_step);
  RUN_TEST(boundary);
  RUN_TEST(no_boundary);
  RUN_TEST(wrap);
  RUN_TEST(read_next);
  return UNITY_END();
}
