/*
  I2CBus.cpp - one Wire.begin, shared by the modules

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
#include "App/AppConfig.h"
#ifdef I2C_BUS

#include "I2CBus.h"
#include <Wire.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "PinSafety.h"

// boards with fixed pins have no prefs
#ifndef I2C_PINS_FIXED
// one bus for the modules
class I2CPrefs : public corium::Module {
public:
  const char* name() override { return PSTR("i2c"); }
  bool core_module() override { return true; }   // a bad pin is fixed from safe mode
  uint8_t display_order() override { return 16; }
  const corium::PrefGroup* prefs_group() override;
};

static I2CPrefs i2cprefs;
CORIUM_REGISTER_MODULE(i2cprefs)

#define I2C_STR_(x) #x
#define I2C_STR(x) I2C_STR_(x)

CORIUM_PSTR(I2_L_SDA, "SDA Pin");
CORIUM_PSTR(I2_L_SCL, "SCL Pin");

static const corium::Pref I2C_PREF_ITEMS[] PROGMEM = {
  {corium::K_SDA, I2_L_SDA, corium::H_RBA, I2C_STR(I2C_DEFAULT_SDA), nullptr, 0, MAX_GPIO_PIN, 0, corium::P_UINT, corium::P_REBOOT | corium::P_PIN_OUT | corium::P_PIN_OD},
  {corium::K_SCL, I2_L_SCL, corium::H_RBA, I2C_STR(I2C_DEFAULT_SCL), nullptr, 0, MAX_GPIO_PIN, 0, corium::P_UINT, corium::P_REBOOT | corium::P_PIN_OUT | corium::P_PIN_OD},
};

static const corium::PrefGroup I2C_PREF_GROUP PROGMEM = {
  "i2c", "I2C Bus", 1,
  I2C_PREF_ITEMS,
  sizeof(I2C_PREF_ITEMS) / sizeof(I2C_PREF_ITEMS[0]),
  corium::P_CAT_SYSTEM, nullptr,
};

const corium::PrefGroup* I2CPrefs::prefs_group() { return &I2C_PREF_GROUP; }
#endif

static const uint8_t NO_PIN = 255;

static uint8_t s_sda = NO_PIN;
static uint8_t s_scl = NO_PIN;
static bool s_up = false;

void I2CBus::pins(uint8_t sda, uint8_t scl) {
  s_sda = sda;
  s_scl = scl;
}

bool I2CBus::begin() {
  if (s_up) return true;
  if (s_sda == NO_PIN || s_scl == NO_PIN) return false;
  const char* why = PinSafety::claim_output(s_sda, PinSafety::I2C);
  if (!why) why = PinSafety::claim_output(s_scl, PinSafety::I2C);
  if (why) {
    Log::console(PSTR("I2C: pins %u/%u unsafe (%s)"), s_sda, s_scl, why);
    return false;
  }
  Wire.begin(s_sda, s_scl);
#ifdef ESP8266
  Wire.setClockStretchLimit(I2C_STRETCH_US);
#endif
  s_up = true;
  return true;
}

#endif
