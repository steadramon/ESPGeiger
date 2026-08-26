/*
  test_clock - Clock: first sync, boot epoch, UTC offset, local time, freshness.

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
#include <stdlib.h>
#include <Arduino.h>
#include <time.h>
#include "clock_fakes.h"
// inline localTm(), same clock as Clock.cpp
#define time(out) fake_time(out)
#include "Corium/System/Clock.h"
#include "Corium/System/Globals.h"
#include "Corium/System/Uptime.h"
#include "Corium/Logger/Logger.h"

void Log::console(const char*, ...) {}

static int s_steps = 0;
static int32_t s_step_delta = 0;
static void record_step(int32_t d) { s_steps++; s_step_delta = d; }

// 2026-01-15 12:00 UTC, CET
static const time_t JAN = 1768478400;
// 2026-07-15 12:00 UTC, CEST
static const time_t JUL = 1784116800;

static void use_tz(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

void setUp(void) {
  eg_clock_reset();
  Uptime::counter = UptimeCounter();
  Clock::synced = false;
  Clock::boot_epoch = 0;
  Clock::tz_offset_min = 0;
  Clock::last_sync_ms = 0;
  Clock::resync = false;
  Clock::resync_ms = 0;
  start = 0;
  fake_now = JAN;
  use_tz("UTC0");
  s_steps = 0;
  s_step_delta = 0;
  Clock::on_step(record_step);
}
void tearDown(void) {}

static void starts_unsynced(void) {
  struct tm t;
  TEST_ASSERT_FALSE(Clock::synced);
  TEST_ASSERT_FALSE(Clock::localTm(&t));
  TEST_ASSERT_FALSE(Clock::isFresh());
}

static void first_sync_once(void) {
  TEST_ASSERT_TRUE(Clock::on_sync());
  TEST_ASSERT_TRUE(Clock::synced);
  TEST_ASSERT_FALSE(Clock::on_sync());
  TEST_ASSERT_FALSE(Clock::on_sync());
  TEST_ASSERT_TRUE(Clock::synced);
}

static void boot_epoch(void) {
  eg_clock_set_ms(5000);
  Uptime::tick();
  start = 6;                 // +1 as ESPGeiger.cpp
  eg_clock_set_ms(125000);
  Uptime::tick();            // 125 s up
  fake_now = JAN;
  Clock::on_sync();
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(JAN - (125 - 6)), (uint32_t)Clock::boot_epoch);
}

// small drift, no step
static void resync_drift(void) {
  eg_clock_set_ms(10000);
  Uptime::tick();
  Clock::on_sync();
  unsigned long first = Clock::boot_epoch;
  eg_clock_set_ms(3600000);
  Uptime::tick();
  fake_now = JAN + 3590 + 5;
  Clock::on_sync();
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(first + 5), (uint32_t)Clock::boot_epoch);
  TEST_ASSERT_EQUAL_INT(0, s_steps);
}

// bad first reply, next sync fixes
static void step_back_reply(void) {
  const time_t BAD = JAN - 69L * 86400L;
  eg_clock_set_ms(10000);
  Uptime::tick();
  fake_now = BAD;
  Clock::on_sync();
  eg_clock_set_ms(310000);
  Uptime::tick();
  fake_now = JAN;
  Clock::on_sync();
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(JAN - 310), (uint32_t)Clock::boot_epoch);
  TEST_ASSERT_EQUAL_INT(1, s_steps);
  TEST_ASSERT_EQUAL_INT32((int32_t)(JAN - 300 - BAD), s_step_delta);
}

// over 60 s either way is a step, 60 isn't
static void step_edge(void) {
  eg_clock_set_ms(10000);
  Uptime::tick();
  Clock::on_sync();
  eg_clock_set_ms(20000);
  Uptime::tick();
  fake_now = JAN + 10 - 61;   // 61 s behind
  Clock::on_sync();
  TEST_ASSERT_EQUAL_INT(1, s_steps);
  TEST_ASSERT_EQUAL_INT32(-61, s_step_delta);
  fake_now = JAN + 10 - 61 - 60;   // 60 more, at the edge
  Clock::on_sync();
  TEST_ASSERT_EQUAL_INT(1, s_steps);
}

static void last_sync_ms(void) {
  eg_clock_set_ms(1234);
  Clock::on_sync();
  TEST_ASSERT_EQUAL_UINT32(1234, (uint32_t)Clock::last_sync_ms);
  eg_clock_set_ms(98765);
  Clock::on_sync();
  TEST_ASSERT_EQUAL_UINT32(98765, (uint32_t)Clock::last_sync_ms);
}

static void tz_offset(void) {
  use_tz("CET-1CEST,M3.5.0,M10.5.0/3");
  fake_now = JAN;
  Clock::on_sync();
  TEST_ASSERT_EQUAL_INT16(60, Clock::tz_offset_min);
  fake_now = JUL;
  Clock::refresh_tz_offset_min();
  TEST_ASSERT_EQUAL_INT16(120, Clock::tz_offset_min);
}

static void tz_offset_west(void) {
  use_tz("EST5EDT,M3.2.0,M11.1.0");
  fake_now = JAN;
  Clock::on_sync();
  TEST_ASSERT_EQUAL_INT16(-300, Clock::tz_offset_min);
}

static void local_time(void) {
  use_tz("CET-1CEST,M3.5.0,M10.5.0/3");
  fake_now = JAN;
  Clock::on_sync();
  struct tm t;
  TEST_ASSERT_TRUE(Clock::localTm(&t));
  TEST_ASSERT_EQUAL_INT(13, t.tm_hour);
  TEST_ASSERT_EQUAL_INT(0, t.tm_min);
  TEST_ASSERT_EQUAL_INT(15, t.tm_mday);
}

// UTC whatever the zone, nothing before a sync
static void utc_time(void) {
  use_tz("CET-1CEST,M3.5.0,M10.5.0/3");
  fake_now = JAN;
  struct tm t;
  TEST_ASSERT_FALSE(Clock::utcTm(&t));
  Clock::on_sync();
  TEST_ASSERT_TRUE(Clock::utcTm(&t));
  TEST_ASSERT_EQUAL_INT(12, t.tm_hour);
  TEST_ASSERT_EQUAL_INT(0, t.tm_mon);
  TEST_ASSERT_EQUAL_INT(126, t.tm_year);
}

// past 2038 still fits the 32 bits
static void epoch_2038(void) {
  fake_now = JAN;
  TEST_ASSERT_EQUAL_UINT32(1768478400u, Clock::epoch());
  fake_now = (time_t)3000000000LL;
  TEST_ASSERT_EQUAL_UINT32(3000000000u, Clock::epoch());
}

static void fresh_25h(void) {
  eg_clock_set_ms(1000);
  Clock::on_sync();
  TEST_ASSERT_TRUE(Clock::isFresh());
  eg_clock_set_ms(1000 + 25UL * 3600UL * 1000UL - 1);
  TEST_ASSERT_TRUE(Clock::isFresh());
  eg_clock_set_ms(1000 + 25UL * 3600UL * 1000UL);
  TEST_ASSERT_FALSE(Clock::isFresh());
}

static void resync_once(void) {
  TEST_ASSERT_FALSE(Clock::resync_due(60000));
  Clock::resync = true;
  TEST_ASSERT_TRUE(Clock::resync_due(60000));
  TEST_ASSERT_FALSE(Clock::resync);
  TEST_ASSERT_FALSE(Clock::resync_due(120000));
}

static void resync_5min(void) {
  Clock::resync = true;
  Clock::resync_due(60000);
  Clock::resync = true;
  TEST_ASSERT_FALSE(Clock::resync_due(60000 + 300000 - 1));
  TEST_ASSERT_TRUE(Clock::resync);
  TEST_ASSERT_TRUE(Clock::resync_due(60000 + 300000));
  TEST_ASSERT_EQUAL_UINT32(360000, (uint32_t)Clock::resync_ms);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(starts_unsynced);
  RUN_TEST(first_sync_once);
  RUN_TEST(boot_epoch);
  RUN_TEST(resync_drift);
  RUN_TEST(step_back_reply);
  RUN_TEST(step_edge);
  RUN_TEST(last_sync_ms);
  RUN_TEST(tz_offset);
  RUN_TEST(tz_offset_west);
  RUN_TEST(local_time);
  RUN_TEST(utc_time);
  RUN_TEST(epoch_2038);
  RUN_TEST(fresh_25h);
  RUN_TEST(resync_once);
  RUN_TEST(resync_5min);
  return UNITY_END();
}
