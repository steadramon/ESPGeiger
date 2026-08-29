/*
  I2CBus.cpp - one Wire.begin, shared by the OLED and env sensors

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
#if defined(SSD1306_DISPLAY) || !defined(DISABLE_ENVSENSOR)

#include "I2CBus.h"
#include <Wire.h>
#include "../Logger/Logger.h"
#include "../Module/EGModule.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "Globals.h"
#include "PinSafety.h"

// boards with fixed pins have no prefs
#ifndef OLED_PINS_BLOCKED
// one bus for the OLED and env sensors
class I2CPrefs : public EGModule {
public:
  const char* name() override { return PSTR("i2c"); }
  bool core_module() override { return true; }   // a bad pin is fixed from safe mode
  uint8_t display_order() override { return 16; }
  const EGPrefGroup* prefs_group() override;
};

static I2CPrefs i2cprefs;
EG_REGISTER_MODULE(i2cprefs)

#define I2C_STR_(x) #x
#define I2C_STR(x) I2C_STR_(x)

EG_PSTR(I2_L_SDA, "SDA Pin");
EG_PSTR(I2_L_SCL, "SCL Pin");

static const EGPref I2C_PREF_ITEMS[] PROGMEM = {
  {EG_K_SDA, I2_L_SDA, EG_H_RBA, I2C_STR(I2C_DEFAULT_SDA), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, EGP_REBOOT | EGP_PIN_OUT},
  {EG_K_SCL, I2_L_SCL, EG_H_RBA, I2C_STR(I2C_DEFAULT_SCL), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, EGP_REBOOT | EGP_PIN_OUT},
};

static const EGPrefGroup I2C_PREF_GROUP PROGMEM = {
  "i2c", "I2C Bus", 1,
  I2C_PREF_ITEMS,
  sizeof(I2C_PREF_ITEMS) / sizeof(I2C_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM, nullptr,
};

const EGPrefGroup* I2CPrefs::prefs_group() { return &I2C_PREF_GROUP; }
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
  s_up = true;
  return true;
}

#endif
