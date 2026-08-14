/*
  test_egled - EGLed duty over a fake clock.

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
#include "../../lib/EGLed/src/EGLed.h"

static const uint8_t PIN = 2;

static int      s_duty;
static int      s_writes;
static int      s_any;   // any pin
static uint32_t s_ms;

void rec_duty(uint8_t pin, int val) {
  s_any++;
  if (pin != PIN) return;
  s_duty = val;
  s_writes++;
}

void rec_mode(uint8_t, uint8_t) { s_any++; }

static uint32_t clock_ms() { return s_ms; }

void setUp(void) {
  s_duty = -1;
  s_writes = 0;
  s_any = 0;
  s_ms = 1000;
  EGLed::setClock(clock_ms);
}
void tearDown(void) {}

static void steady(void) {
  EGLed led(PIN);
  led.on();
  TEST_ASSERT_EQUAL_INT(255, s_duty);
  TEST_ASSERT_FALSE(led.isRunning());
  s_writes = 0;
  s_ms += 5000;
  led.update();
  TEST_ASSERT_EQUAL_INT(0, s_writes);
}

static void low_active(void) {
  EGLed led(PIN, true);
  led.on();
  TEST_ASSERT_EQUAL_INT(0, s_duty);
  led.off();
  TEST_ASSERT_EQUAL_INT(255, s_duty);
}

static void dimmed(void) {
  EGLed led(PIN);
  led.setBrightness(100);
  led.on();
  TEST_ASSERT_EQUAL_INT(100, s_duty);
}

static void stops_blink(void) {
  EGLed led(PIN);
  led.blink(10, 10);
  led.on();
  s_ms += 15;
  led.update();
  TEST_ASSERT_EQUAL_INT(255, s_duty);
  led.off();
  TEST_ASSERT_EQUAL_INT(0, s_duty);
}

static void pulse(void) {
  EGLed led(PIN);
  led.pulse(20);
  TEST_ASSERT_EQUAL_INT(255, s_duty);
  s_ms += 19;
  led.update();
  TEST_ASSERT_EQUAL_INT(255, s_duty);
  s_ms += 1;
  led.update();
  TEST_ASSERT_EQUAL_INT(0, s_duty);
}

// no pin writes nothing, attach() takes it later
static void late_pin(void) {
  EGLed led(EGLed::NO_PIN);
  led.on();
  led.pulse(20);
  TEST_ASSERT_EQUAL_INT(0, s_any);
  led.attach(PIN, false);
  TEST_ASSERT_EQUAL_INT(0, s_duty);
  led.on();
  TEST_ASSERT_EQUAL_INT(255, s_duty);
}

// polarity comes with the late pin
static void late_low(void) {
  EGLed led(EGLed::NO_PIN);
  led.attach(PIN, true);
  TEST_ASSERT_EQUAL_INT(255, s_duty);   // off
  led.on();
  TEST_ASSERT_EQUAL_INT(0, s_duty);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(steady);
  RUN_TEST(low_active);
  RUN_TEST(dimmed);
  RUN_TEST(stops_blink);
  RUN_TEST(pulse);
  RUN_TEST(late_pin);
  RUN_TEST(late_low);
  return UNITY_END();
}
