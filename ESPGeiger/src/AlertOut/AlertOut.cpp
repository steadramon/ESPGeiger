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
#include "App/AppKeys.h"
#include "Counter/Counter.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/Lifecycle.h"
#include "App/AppConfig.h"
#include "Corium/System/PinSafety.h"

AlertOut alertout;
CORIUM_REGISTER_MODULE(alertout)

CORIUM_PSTR(AO_H_PIN,  "Alert output pin (-1 = disabled).");
CORIUM_PSTR(AO_O_MOD,  "Steady level|1 Hz pulse");

static const corium::Pref ALERT_PREF_ITEMS[] PROGMEM = {
  {EG_K_PIN,      EG_L_PIN, AO_H_PIN, "-1", nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT,  corium::P_REBOOT | corium::P_PIN_OUT},
  {EG_K_POLARITY, EG_L_POL, nullptr,  "0",  EG_O_POL, 0, 1,            0, corium::P_ENUM, 0},
  {EG_K_MODE,     EG_L_MOD, nullptr,  "0",  AO_O_MOD, 0, 1,            0, corium::P_ENUM, 0},
};

static const corium::PrefGroup ALERT_PREF_GROUP PROGMEM = {
  "alert", "Alert Out", 1,
  ALERT_PREF_ITEMS,
  sizeof(ALERT_PREF_ITEMS) / sizeof(ALERT_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, EG_K_PIN, 0,
};

const corium::PrefGroup* AlertOut::prefsGroup() { return &ALERT_PREF_GROUP; }

void AlertOut::onPrefsLoaded() {
  _invert = (uint8_t)corium::Prefs::getUInt("alert", EG_K_POLARITY) & 1;
  _mode   = (uint8_t)corium::Prefs::getUInt("alert", EG_K_MODE) & 1;
  // pin only at begin(), PinSafety holds the claim
  _pulsing = false;
  corium::Registry::post(this);
}

static void alert_lifecycle(corium::Lifecycle::Event e) {
  if (e == corium::Lifecycle::SHUTDOWN) alertout.off();
}

// every boot, safe mode too: off before the join
void AlertOut::park() {
  int p = corium::Prefs::getInt("alert", EG_K_PIN);
  if (p < 0 || p > MAX_GPIO_PIN) return;
  if (const char* why = corium::PinSafety::claimOutput(p, PSTR("AlertOut"))) {
    corium::Log::console(PSTR("AlertOut: pin=%d unsafe (%s)"), p, why);
    return;
  }
  _invert = (uint8_t)corium::Prefs::getUInt("alert", EG_K_POLARITY) & 1;   // safe mode skips onPrefsLoaded
  _pin = (int8_t)p;
  pinMode(_pin, OUTPUT);
  apply_level(false);
}

// a relay stays as it was through a restart
void AlertOut::off() {
  apply_level(false);
  _pin = -1;
}

void AlertOut::begin() {
  if (_pin < 0) return;
  corium::Lifecycle::on(alert_lifecycle);
  corium::Registry::on(Counter::TOPIC_ALARM, this);
  corium::Log::console(PSTR("AlertOut: pin=%d polarity=%u mode=%u"),
               (int)_pin, (unsigned)_invert, (unsigned)_mode);
}

void AlertOut::apply_level(bool active) {
  if (_pin < 0) return;
  digitalWrite(_pin, (active ^ _invert) ? HIGH : LOW);
}

// on alarm changes; while pulsing, at each flip
void AlertOut::loop(unsigned long now) {
  bool active = gcounter.is_alert() && !gcounter.is_snoozed();
  if (_mode != 1 || !active) {
    _pulsing = false;
    apply_level(active);
    return;
  }
  // other alarm changes don't move the flips
  if (!_pulsing) {
    _pulsing = true;
    _high = false;
    _flip_at = now;
  }
  if (corium::dueEvery(now, _flip_at, FLIP_MS)) {
    _high = !_high;
    apply_level(_high);
  }
  corium::Registry::dueAt(this, _flip_at);
}

#endif
