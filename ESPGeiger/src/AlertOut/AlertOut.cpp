/*
  AlertOut.cpp - GPIO output mirroring the Counter alert state.

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
#ifdef ALERT_OUT

#include "AlertOut.h"
#include "../Counter/Counter.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/Globals.h"
#include "../Util/PinSafety.h"

extern Counter gcounter;

AlertOut alertout;
EG_REGISTER_MODULE(alertout)

EG_PSTR(AO_L_PIN,  "GPIO");
EG_PSTR(AO_H_PIN,  "Alert output pin (-1 = disabled). Reboot to apply.");
EG_PSTR(AO_L_INV,  "Polarity");
EG_PSTR(AO_H_INV,  "0 = active high, 1 = active low");
EG_PSTR(AO_L_MOD,  "Mode");
EG_PSTR(AO_H_MOD,  "0 = steady level, 1 = 1 Hz pulse while alerting");

static const EGPref ALERT_PREF_ITEMS[] = {
  {"pin",      AO_L_PIN, AO_H_PIN, "-1", nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT,  0},
  {"polarity", AO_L_INV, AO_H_INV, "0",  nullptr,  0, 1,            0, EGP_UINT, 0},
  {"mode",     AO_L_MOD, AO_H_MOD, "0",  nullptr,  0, 1,            0, EGP_UINT, 0},
};

static const EGPrefGroup ALERT_PREF_GROUP = {
  "alert", "Alert Out", 1,
  ALERT_PREF_ITEMS,
  sizeof(ALERT_PREF_ITEMS) / sizeof(ALERT_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* AlertOut::prefs_group() { return &ALERT_PREF_GROUP; }

void AlertOut::on_prefs_loaded() {
  _invert = (uint8_t)EGPrefs::getUInt("alert", "polarity") & 1;
  _mode   = (uint8_t)EGPrefs::getUInt("alert", "mode") & 1;
  // Pin is read once at begin() so prefs save during runtime can't reassign a
  // claimed GPIO out from under PinSafety.
}

void AlertOut::begin() {
  int p = EGPrefs::getInt("alert", "pin");
  if (p < 0 || p > MAX_GPIO_PIN) {
    EGModuleRegistry::set_tick_enabled(this, false);
    return;
  }
  if (const char* why = PinSafety::claim_output(p, PSTR("AlertOut"))) {
    Log::console(PSTR("AlertOut: pin=%d unsafe (%s)"), p, why);
    EGModuleRegistry::set_tick_enabled(this, false);
    return;
  }
  _pin = (int8_t)p;
  pinMode(_pin, OUTPUT);
  apply_level(false);
  Log::console(PSTR("AlertOut: pin=%d polarity=%u mode=%u"),
               (int)_pin, (unsigned)_invert, (unsigned)_mode);
}

void AlertOut::apply_level(bool active) {
  if (_pin < 0) return;
  digitalWrite(_pin, (active ^ _invert) ? HIGH : LOW);
}

void AlertOut::s_tick(unsigned long now_s) {
  if (_pin < 0) return;
  bool active = gcounter.is_alert();
  if (_mode == 1 && active) {
    // 1 Hz square: even seconds high, odd seconds low (subject to invert).
    apply_level((now_s & 1u) == 0);
  } else if (active != _last_state) {
    apply_level(active);
  }
  _last_state = active;
}

#endif
