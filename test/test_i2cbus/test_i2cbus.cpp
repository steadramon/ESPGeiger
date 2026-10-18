/*
  test_i2cbus.cpp - one Wire.begin, first pins win.

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
#include <Wire.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/I2CBus.h"
#include "Corium/System/PinSafety.h"

// the bus stays up, tests run in order

TwoWire Wire;
void Log::console(const char*, ...) {}

const char EG_K_SDA[] = "sda";
const char EG_K_SCL[] = "scl";
const char EG_H_RBA[] = "Reboot to apply";
bool EGModuleRegistry::add(EGModule*) { return true; }

void setUp(void) {}
void tearDown(void) {}

static void no_pins(void) {
  TEST_ASSERT_FALSE(I2CBus::begin());
  TEST_ASSERT_EQUAL_INT(0, Wire.begins);
}

static void refused(void) {
  TEST_ASSERT_NULL(PinSafety::claim_output(5, "Max7219"));
  I2CBus::pins(4, 5);
  TEST_ASSERT_FALSE(I2CBus::begin());
  TEST_ASSERT_EQUAL_INT(0, Wire.begins);
}

static void once(void) {
  I2CBus::pins(21, 22);
  TEST_ASSERT_TRUE(I2CBus::begin());
  I2CBus::pins(4, 5);
  TEST_ASSERT_TRUE(I2CBus::begin());
  TEST_ASSERT_EQUAL_INT(1, Wire.begins);
  TEST_ASSERT_EQUAL_INT(21, Wire.sda);
  TEST_ASSERT_EQUAL_INT(22, Wire.scl);
  TEST_ASSERT_EQUAL_STRING("I2C", PinSafety::claim_output(22, "Max7219"));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(no_pins);
  RUN_TEST(refused);
  RUN_TEST(once);
  return UNITY_END();
}
