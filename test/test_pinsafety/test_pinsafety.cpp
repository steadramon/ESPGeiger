/*
  test_pinsafety.cpp - one owner per pin.

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
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinSafety.h"

// table is never cleared, each test uses its own pins

static int s_logs = 0;
void Log::console(const char*, ...) { s_logs++; }

void setUp(void) {}
void tearDown(void) {}

static void same_owner(void) {
  char copy[] = "HV";   // same text, other pointer
  TEST_ASSERT_NULL(PinSafety::claim_output(12, "HV"));
  TEST_ASSERT_NULL(PinSafety::claim_output(12, copy));
}

static void other_owner(void) {
  TEST_ASSERT_NULL(PinSafety::claim_output(14, "HV"));
  const char* why = PinSafety::claim_output(14, "PulseOut");
  TEST_ASSERT_NOT_NULL(why);
  TEST_ASSERT_EQUAL_STRING("HV", why);
  TEST_ASSERT_EQUAL_STRING("HV", PinSafety::claim_input(14, "RX"));
}

static void shared_i2c(void) {
  TEST_ASSERT_NULL(PinSafety::claim_output(4, PinSafety::I2C));
  TEST_ASSERT_NULL(PinSafety::claim_output(4, PinSafety::I2C));
  TEST_ASSERT_EQUAL_STRING("I2C", PinSafety::claim_output(4, "Max7219"));
}

static void no_pin(void) {
  TEST_ASSERT_NULL(PinSafety::claim_output(-1, "HV"));
  TEST_ASSERT_NULL(PinSafety::claim_output(-1, "PulseOut"));
  TEST_ASSERT_NULL(PinSafety::claim(-1, "AlertOut"));
}

static void out_of_range(void) {
  TEST_ASSERT_NOT_NULL(PinSafety::claim_output(MAX_GPIO_PIN + 1, "HV"));
  TEST_ASSERT_NOT_NULL(PinSafety::claim_input(-2, "RX"));
  // a refused pin isn't claimed
  TEST_ASSERT_NULL(PinSafety::claim(MAX_GPIO_PIN + 1, "PulseOut"));
}

static void table_full(void) {
  static const char* const OWNERS[] = {"A", "B", "C", "D", "E", "F", "G", "H",
                                       "I", "J", "K", "L", "M", "N", "O", "P"};
  for (int i = 0; i < 16; i++) PinSafety::claim(20 + i, OWNERS[i]);
  int before = s_logs;
  TEST_ASSERT_NULL(PinSafety::claim(36, "Late"));
  TEST_ASSERT_EQUAL_INT(before + 1, s_logs);
  TEST_ASSERT_EQUAL_STRING("HV", PinSafety::claim_output(12, "AlertOut"));
}

// each chip's table, checked straight
static const char* out(const PinSafety::PinRule* t, int pin, bool usb = true) {
  const char* warn;
  return PinSafety::check(t, pin, true, usb, &warn);
}

static const char* in(const PinSafety::PinRule* t, int pin, const char** warn) {
  return PinSafety::check(t, pin, false, true, warn);
}

static void flash_pins(void) {
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP8266_PINS, 6));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32_PINS, 11));
  TEST_ASSERT_NULL(out(PinSafety::ESP32_PINS, 5));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32C3_PINS, 11));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32C3_PINS, 17));
  TEST_ASSERT_NULL(out(PinSafety::ESP32C3_PINS, 10));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32S3_PINS, 26));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32S2_PINS, 32));
  TEST_ASSERT_EQUAL_STRING("SPI flash", out(PinSafety::ESP32C6_PINS, 24));
  TEST_ASSERT_EQUAL_STRING("PSRAM", out(PinSafety::ESP32S3_PINS, 37));
  TEST_ASSERT_NULL(out(PinSafety::ESP32S3_PINS, 38));
}

static void missing_pins(void) {
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32_PINS, 20));
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32_PINS, 24));
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32_PINS, 28));
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32_PINS, 31));
  TEST_ASSERT_NULL(out(PinSafety::ESP32_PINS, 21));
  TEST_ASSERT_NULL(out(PinSafety::ESP32_PINS, 32));
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32S3_PINS, 22));
  TEST_ASSERT_EQUAL_STRING("no such pin", out(PinSafety::ESP32S2_PINS, 25));
  TEST_ASSERT_NULL(out(PinSafety::ESP32S3_PINS, 21));
}

static void usb_pins(void) {
  TEST_ASSERT_EQUAL_STRING("USB", out(PinSafety::ESP32C3_PINS, 18));
  TEST_ASSERT_EQUAL_STRING("USB", out(PinSafety::ESP32S3_PINS, 20));
  TEST_ASSERT_EQUAL_STRING("USB", out(PinSafety::ESP32C6_PINS, 13));
  // a UART console leaves them free
  TEST_ASSERT_NULL(out(PinSafety::ESP32C3_PINS, 19, false));
  TEST_ASSERT_NULL(out(PinSafety::ESP32S2_PINS, 19, false));
}

static void input_only(void) {
  const char* warn;
  TEST_ASSERT_EQUAL_STRING("input only", out(PinSafety::ESP32_PINS, 34));
  TEST_ASSERT_NULL(in(PinSafety::ESP32_PINS, 39, &warn));
  TEST_ASSERT_NOT_NULL(warn);   // no pull-up
  TEST_ASSERT_EQUAL_STRING("input only", out(PinSafety::ESP32S2_PINS, 46));
  TEST_ASSERT_NULL(out(PinSafety::ESP32S3_PINS, 46));
}

static void strap_warn(void) {
  const char* warn;
  TEST_ASSERT_NULL(in(PinSafety::ESP32_PINS, 12, &warn));
  TEST_ASSERT_NOT_NULL(warn);
  TEST_ASSERT_NULL(PinSafety::check(PinSafety::ESP32_PINS, 12, true, true, &warn));
  TEST_ASSERT_NULL(warn);   // outputs aren't pulled at boot
  TEST_ASSERT_NULL(in(PinSafety::ESP8266_PINS, 15, &warn));
  TEST_ASSERT_NOT_NULL(warn);
  TEST_ASSERT_NULL(in(PinSafety::ESP32S3_PINS, 45, &warn));
  TEST_ASSERT_NOT_NULL(warn);
  TEST_ASSERT_NULL(in(PinSafety::ESP8266_PINS, 4, &warn));
  TEST_ASSERT_NULL(warn);
}

// download-mode pins on each chip, warned not refused
static void strap_boot(void) {
  const char* warn;
  const struct { const PinSafety::PinRule* t; int pin; } STRAPS[] = {
    {PinSafety::ESP8266_PINS, 0}, {PinSafety::ESP32_PINS, 0}, {PinSafety::ESP32_PINS, 5},
    {PinSafety::ESP32_PINS, 15}, {PinSafety::ESP32S2_PINS, 0}, {PinSafety::ESP32S3_PINS, 3},
    {PinSafety::ESP32S3_PINS, 46}, {PinSafety::ESP32C3_PINS, 8}, {PinSafety::ESP32C3_PINS, 9},
  };
  for (const auto& s : STRAPS) {
    TEST_ASSERT_NULL(in(s.t, s.pin, &warn));
    TEST_ASSERT_NOT_NULL(warn);
  }
  TEST_ASSERT_NULL(in(PinSafety::ESP32C3_PINS, 4, &warn));
  TEST_ASSERT_NULL(warn);
}

// no pull-up: a button there floats
static void gpio16_pull(void) {
  const char* warn;
  TEST_ASSERT_NULL(in(PinSafety::ESP8266_PINS, 16, &warn));
  TEST_ASSERT_NOT_NULL(warn);
  TEST_ASSERT_NULL(out(PinSafety::ESP8266_PINS, 16));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(same_owner);
  RUN_TEST(other_owner);
  RUN_TEST(shared_i2c);
  RUN_TEST(no_pin);
  RUN_TEST(out_of_range);
  RUN_TEST(table_full);
  RUN_TEST(flash_pins);
  RUN_TEST(missing_pins);
  RUN_TEST(usb_pins);
  RUN_TEST(input_only);
  RUN_TEST(strap_warn);
  RUN_TEST(strap_boot);
  RUN_TEST(gpio16_pull);
  return UNITY_END();
}
