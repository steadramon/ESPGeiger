/*
  test_webapi - WebAPISchedule, the handshake next-attempt clock.

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

// On 2026-08-24 a server-side outage produced one minute of `wapi=0/145`: 145
// signed handshakes, all failed, ~410ms apart. The arm was only advanced inside
// the send-succeeded branch, so a connect that failed synchronously left the
// handshake due and the module re-attempted at the registry's 100ms floor.
//
// The suite asserts the property that was violated - an attempt advances the
// arm before it can fail - and carries the unsigned compare the class rejects,
// asserting it is broken. A guard test is worth nothing unless it fails on the
// code it is guarding against.

#include <unity.h>
#include <stdint.h>

#include "WebAPI/WebAPISchedule.h"

void setUp(void)    {}
void tearDown(void) {}

static const uint32_t HOUR_MS = 60UL * 60UL * 1000UL;
static const uint32_t MIN_BACKOFF = 30000UL;
static const uint32_t MAX_BACKOFF = 300000UL;

// The registry floors an overdue module at 100ms, so "still due" and "spins"
// are the same statement.
static const uint32_t LOOP_FLOOR_MS = 100UL;

// --- anchoring --------------------------------------------------------------

static void test_a_fresh_schedule_is_unanchored(void) {
  WebAPISchedule s(HOUR_MS);
  TEST_ASSERT_FALSE(s.anchored());
  TEST_ASSERT_EQUAL_UINT32(MIN_BACKOFF, s.backoff_ms());
}

static void test_anchor_waits_out_the_offset(void) {
  const uint32_t now = 1000000;
  const uint32_t offsets[] = { 0, 1, 500, 30000, 59999 };
  for (uint32_t off : offsets) {
    WebAPISchedule s(HOUR_MS);
    s.anchor(now, off);
    TEST_ASSERT_TRUE(s.anchored());
    if (off > 0) TEST_ASSERT_FALSE(s.due(now + off - 1));
    TEST_ASSERT_TRUE(s.due(now + off));
  }
}

static void test_unanchor_reopens_the_first_arm(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(1000, 0);
  s.sent(1000);
  TEST_ASSERT_TRUE(s.anchored());
  s.unanchor();
  TEST_ASSERT_FALSE(s.anchored());
}

// 0 is the unanchored sentinel and is also a value the arithmetic can land on.
// A schedule that anchors itself into "never armed" is due forever.
static void test_the_arm_never_lands_on_the_sentinel(void) {
  // anchor(): now + offset - interval == 0
  WebAPISchedule a(HOUR_MS);
  a.anchor(HOUR_MS - 7, 7);
  TEST_ASSERT_TRUE(a.anchored());

  // sent(): now == 0, the instant millis() wraps
  WebAPISchedule b(HOUR_MS);
  b.anchor(1000, 0);
  b.sent(0);
  TEST_ASSERT_TRUE(b.anchored());

  // retry(): now - interval + backoff == 0
  WebAPISchedule c(HOUR_MS);
  c.anchor(1000, 0);
  c.retry(HOUR_MS - MIN_BACKOFF);
  TEST_ASSERT_TRUE(c.anchored());
}

// --- the invariant ----------------------------------------------------------

static void test_retry_is_not_due_until_the_backoff_elapses(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);
  uint32_t now = 50000;
  uint32_t expect = MIN_BACKOFF;
  for (int attempt = 0; attempt < 8; attempt++) {
    s.retry(now);
    TEST_ASSERT_FALSE(s.due(now));
    TEST_ASSERT_FALSE(s.due(now + LOOP_FLOOR_MS));
    TEST_ASSERT_FALSE(s.due(now + expect - 1));
    TEST_ASSERT_TRUE(s.due(now + expect));
    now += expect;
    expect = (expect * 2 > MAX_BACKOFF) ? MAX_BACKOFF : expect * 2;
  }
}

static void test_backoff_doubles_then_caps(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);
  const uint32_t want[] = { 60000, 120000, 240000, 300000, 300000, 300000 };
  uint32_t now = 1000;
  for (uint32_t w : want) {
    s.retry(now);
    TEST_ASSERT_EQUAL_UINT32(w, s.backoff_ms());
    now += 1000;
  }
}

static void test_acceptance_forgets_the_backoff(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);
  for (int i = 0; i < 6; i++) s.retry(1000);
  TEST_ASSERT_EQUAL_UINT32(MAX_BACKOFF, s.backoff_ms());

  s.reset_backoff();
  TEST_ASSERT_EQUAL_UINT32(MIN_BACKOFF, s.backoff_ms());

  for (int i = 0; i < 6; i++) s.retry(1000);
  s.accepted(1000, 0);
  TEST_ASSERT_EQUAL_UINT32(MIN_BACKOFF, s.backoff_ms());
}

static void test_a_sent_request_holds_the_arm_for_a_full_interval(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);
  const uint32_t at = 123456;
  s.sent(at);
  TEST_ASSERT_FALSE(s.due(at + HOUR_MS - 1));
  TEST_ASSERT_TRUE(s.due(at + HOUR_MS));
}

static void test_acceptance_lands_on_the_slot(void) {
  WebAPISchedule s(HOUR_MS);
  const uint32_t now = 5000000;
  const uint32_t waits[] = { 0, 1, 90000, HOUR_MS - 1 };
  for (uint32_t w : waits) {
    s.accepted(now, w);
    if (w > 0) TEST_ASSERT_FALSE(s.due(now + w - 1));
    TEST_ASSERT_TRUE(s.due(now + w));
  }
}

// The field symptom, as a bound. A server that fails every attempt instantly
// must not let the module attempt more than a handful of times a minute.
static void test_a_dead_server_cannot_produce_a_burst(void) {
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);

  // ~410ms per attempt was the measured cost of a sign plus an instant failure.
  const uint32_t ATTEMPT_MS = 410;
  const uint32_t WINDOW_MS  = 60000;

  uint32_t attempts_in_window = 0;
  uint32_t window_start = 0;
  uint32_t worst = 0;

  for (uint32_t now = 0; now < 30UL * 60UL * 1000UL; now += LOOP_FLOOR_MS) {
    if (now - window_start >= WINDOW_MS) {
      if (attempts_in_window > worst) worst = attempts_in_window;
      attempts_in_window = 0;
      window_start = now;
    }
    if (!s.due(now)) continue;
    // doHandshake: the arm moves before any work that can fail.
    s.retry(now);
    attempts_in_window++;
    now += ATTEMPT_MS;
  }
  if (attempts_in_window > worst) worst = attempts_in_window;

  // 60s of window against a 30s opening backoff.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(3, worst);
}

// --- signed compare ---------------------------------------------------------

// What due() would be with an unsigned compare. Carried so the case below
// demonstrably fails on it; if this starts agreeing, the suite has stopped
// testing anything.
static bool due_unsigned(uint32_t now, uint32_t last, uint32_t interval) {
  return (now - last) >= interval;
}

// Backoff can exceed the interval when the interval is short. The arm then sits
// in the future, and an unsigned delta underflows into a huge number: due
// forever, which is the spin.
static void test_backoff_past_the_interval_is_not_due(void) {
  const uint32_t SHORT = 60000;
  WebAPISchedule s(SHORT);
  s.anchor(0, 0);
  for (int i = 0; i < 4; i++) s.retry(1000);       // backoff now 300000 > SHORT
  TEST_ASSERT_EQUAL_UINT32(MAX_BACKOFF, s.backoff_ms());

  const uint32_t now = 1000;
  s.retry(now);
  TEST_ASSERT_FALSE(s.due(now));
  TEST_ASSERT_FALSE(s.due(now + LOOP_FLOOR_MS));
  TEST_ASSERT_FALSE(s.due(now + MAX_BACKOFF - 1));
  TEST_ASSERT_TRUE(s.due(now + MAX_BACKOFF));

  // The arm is at now - SHORT + 300000. An unsigned compare calls that due
  // immediately, and keeps calling it due.
  const uint32_t last = now - SHORT + MAX_BACKOFF;
  TEST_ASSERT_TRUE(due_unsigned(now, last, SHORT));
  TEST_ASSERT_TRUE(due_unsigned(now + LOOP_FLOOR_MS, last, SHORT));
}

// --- the 49.7 day wrap ------------------------------------------------------

static void test_due_survives_the_millis_wrap(void) {
  const uint32_t before = 0xFFFFFFFFu - 10000;   // 10s short of the wrap
  WebAPISchedule s(HOUR_MS);
  s.sent(before);

  TEST_ASSERT_FALSE(s.due(before + 5000));            // still before the wrap
  TEST_ASSERT_FALSE(s.due((uint32_t)(before + 20000)));// past it, arm not reached
  TEST_ASSERT_FALSE(s.due((uint32_t)(before + HOUR_MS - 1)));
  TEST_ASSERT_TRUE(s.due((uint32_t)(before + HOUR_MS)));
}

static void test_retry_survives_the_millis_wrap(void) {
  const uint32_t near_wrap = 0xFFFFFFFFu - 1000;
  WebAPISchedule s(HOUR_MS);
  s.anchor(0, 0);
  s.retry(near_wrap);
  TEST_ASSERT_FALSE(s.due(near_wrap));
  TEST_ASSERT_FALSE(s.due((uint32_t)(near_wrap + MIN_BACKOFF - 1)));
  TEST_ASSERT_TRUE(s.due((uint32_t)(near_wrap + MIN_BACKOFF)));
}

// --- wall-clock slot --------------------------------------------------------

static void test_slot_wait_is_within_the_interval(void) {
  const uint32_t ks[] = { 0, 1, 12345, 0x7FFFFFFFu, 0xFFFFFFFFu };
  const uint32_t epochs[] = { 0, 1, 1799, 3599, 1755000000u };
  for (uint32_t k : ks) {
    for (uint32_t e : epochs) {
      uint32_t w = wall_clock_wait_ms(k, HOUR_MS, 3600, e);
      TEST_ASSERT_LESS_THAN_UINT32(HOUR_MS, w);
    }
  }
}

static void test_slot_wait_lands_on_the_target_second(void) {
  // Target 600s into the hour, clock at 60s into the hour: 540s to wait.
  TEST_ASSERT_EQUAL_UINT32(540000, wall_clock_wait_ms(600000, HOUR_MS, 3600, 60));
  // Same target, clock already past it: wait out the rest of the hour.
  TEST_ASSERT_EQUAL_UINT32(HOUR_MS - 60000, wall_clock_wait_ms(0, HOUR_MS, 3600, 60));
  // Exactly on the target.
  TEST_ASSERT_EQUAL_UINT32(0, wall_clock_wait_ms(60000, HOUR_MS, 3600, 60));
}

// time_t goes negative on a 32-bit signed clock in 2038. The cast to uint32_t
// happens at the call site; this pins that an epoch past INT32_MAX still lands
// inside the interval rather than wrapping the result.
static void test_slot_wait_past_2038(void) {
  const uint32_t y2038 = 2147483648u;    // INT32_MAX + 1
  const uint32_t epochs[] = { y2038, y2038 + 1, y2038 + 3599, 4294967295u };
  for (uint32_t e : epochs) {
    uint32_t w = wall_clock_wait_ms(600000, HOUR_MS, 3600, e);
    TEST_ASSERT_LESS_THAN_UINT32(HOUR_MS, w);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_schedule_is_unanchored);
  RUN_TEST(test_anchor_waits_out_the_offset);
  RUN_TEST(test_unanchor_reopens_the_first_arm);
  RUN_TEST(test_the_arm_never_lands_on_the_sentinel);
  RUN_TEST(test_retry_is_not_due_until_the_backoff_elapses);
  RUN_TEST(test_backoff_doubles_then_caps);
  RUN_TEST(test_acceptance_forgets_the_backoff);
  RUN_TEST(test_a_sent_request_holds_the_arm_for_a_full_interval);
  RUN_TEST(test_acceptance_lands_on_the_slot);
  RUN_TEST(test_a_dead_server_cannot_produce_a_burst);
  RUN_TEST(test_backoff_past_the_interval_is_not_due);
  RUN_TEST(test_due_survives_the_millis_wrap);
  RUN_TEST(test_retry_survives_the_millis_wrap);
  RUN_TEST(test_slot_wait_is_within_the_interval);
  RUN_TEST(test_slot_wait_lands_on_the_target_second);
  RUN_TEST(test_slot_wait_past_2038);
  return UNITY_END();
}
