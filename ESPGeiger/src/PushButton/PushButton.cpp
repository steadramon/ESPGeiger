/*
  PushButton.cpp - User interaction via button

  Copyright (C) 2023 @steadramon

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
#ifdef GEIGER_PUSHBUTTON

#include "PushButton.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/System/PinSafety.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/LedPatterns.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/Debounce.h"

static const uint16_t POLL_MS      = 10;
static const uint16_t DEBOUNCE_MS  = 30;
static const uint16_t LONG_PRESS_MS = 2000;

// 0 = next page, 1 = back. loop task only
struct BtnState {
  int      pin = -1;
  corium::Debounce deb;
  uint32_t down_ms = 0;
  bool     long_fired = false;
};
static BtnState s_btn[2];

static bool read_down(const BtnState& b) { return digitalRead(b.pin) == LOW; }

PushButton pushbutton;
CORIUM_REGISTER_FEATURE(pushbutton, FEAT_BUTTON)

#ifndef BTN_PIN_BLOCKED

CORIUM_PSTR(BTN_L_PIN, "Button pin");
CORIUM_PSTR(BTN_H_PIN, "GPIO for pushbutton (-1 = disabled). Internal pull-up, active LOW.");

static const corium::Pref BTN_PREF_ITEMS[] PROGMEM = {
  {EG_K_PIN, BTN_L_PIN, BTN_H_PIN, STR(PUSHBUTTON_PIN), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_PIN_BTN},
};

static const corium::PrefGroup BTN_PREF_GROUP PROGMEM = {
  "btn", "Button", 1,
  BTN_PREF_ITEMS,
  sizeof(BTN_PREF_ITEMS) / sizeof(BTN_PREF_ITEMS[0]),
};

const corium::PrefGroup* PushButton::prefsGroup() { return &BTN_PREF_GROUP; }

void PushButton::onPrefsLoaded() {
  int pin1 = (int)corium::Prefs::getInt("btn", EG_K_PIN);
  set_pin(pin1);
  // pin2 build flag only
  if (PUSHBUTTON_PIN2 >= 0) set_pin2(PUSHBUTTON_PIN2);
  corium::Log::console(PSTR("Btn: pin=%d pin2=%d"), pin1, (int)PUSHBUTTON_PIN2);
}
#endif

PushButton::PushButton() {
}

// reset only on change, keeps the prefs cascade quiet
static void set_pin_slot(uint8_t slot, int pin) {
  if (slot >= 2) return;
  BtnState& b = s_btn[slot];
  if (const char* why = corium::PinSafety::claimButton(pin, PSTR("Btn"))) {
    corium::Log::console(PSTR("Btn: pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  if (pin == b.pin) return;
  uint32_t now = corium::fastMillis();
  bool down = false;
  b.pin = pin;
  if (pin >= 0) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(20);
    down = read_down(b);
  }
  // held at boot: no short on release
  b.deb.reset(down, now);
  b.down_ms = now;
  b.long_fired = down;
  bool on = s_btn[0].pin >= 0 || s_btn[1].pin >= 0;
  corium::Registry::every(&pushbutton, on ? POLL_MS : 0);
}

void PushButton::set_pin(int pin)  { set_pin_slot(0, pin); }
void PushButton::set_pin2(int pin) { set_pin_slot(1, pin); }

// boot hold reads it before begin, fixed pins set here
void PushButton::init()
{
#ifdef BTN_PIN_BLOCKED
  if (PUSHBUTTON_PIN  >= 0) set_pin(PUSHBUTTON_PIN);
  if (PUSHBUTTON_PIN2 >= 0) set_pin2(PUSHBUTTON_PIN2);
#else
  // safe mode skips onPrefsLoaded (quarantined): the boot hold still reads the set pin
  if (s_btn[0].pin < 0) set_pin((int)corium::Prefs::getInt("btn", EG_K_PIN));
#endif
}

void PushButton::begin()
{
  init();
#ifdef BTN_PIN_BLOCKED
  corium::Log::console(PSTR("Btn: pin=%d pin2=%d"),
               (int)PUSHBUTTON_PIN, (int)PUSHBUTTON_PIN2);
#endif
#ifdef PIN_SCAN
  corium::Registry::every(this, POLL_MS);
#endif
}

// samples too, setup has no loop
bool PushButton::isPressed()
{
  uint32_t now = corium::fastMillis();
  for (BtnState& b : s_btn) {
    if (b.pin >= 0) b.deb.step(read_down(b), now, DEBOUNCE_MS);
  }
  return s_btn[0].deb.level || s_btn[1].deb.level;
}

bool PushButton::is_active()
{
  return s_btn[0].pin >= 0 || s_btn[1].pin >= 0;
}

#ifdef PIN_SCAN
// Diagnostic, logs any pin going LOW
static bool pin_scan_skips(int p) {
  if (p == 0 || p == 41 || p == 42 || p == 48) return true;
  if (p >= 22 && p <= 25) return true;      // gap on S3
  return p >= 26 && p <= 37;                // possible flash/PSRAM
}

static void pin_scan_init() {
  for (int p = 1; p <= 48; p++) {
    if (pin_scan_skips(p)) continue;
    pinMode(p, INPUT_PULLUP);
  }
}

static void pin_scan_poll() {
  static uint8_t s_level[49] = {0};
  static bool s_primed = false;
  if (!s_primed) {
    delay(5);
    for (int p = 1; p <= 48; p++) s_level[p] = digitalRead(p) ? 1 : 0;
    s_primed = true;
    return;
  }
  for (int p = 1; p <= 48; p++) {
    if (pin_scan_skips(p)) continue;
    uint8_t cur = digitalRead(p) ? 1 : 0;
    if (cur != s_level[p]) {
      corium::Log::console(PSTR("PinScan: GPIO%d %s"), p, cur ? "HIGH" : "LOW");
      s_level[p] = cur;
    }
  }
}

static void pin_scan_loop(unsigned long now) {
  static bool s_inited = false;
  if (!s_inited) { pin_scan_init(); s_inited = true; }
  static unsigned long s_poll_ms = 0;
  if (now - s_poll_ms >= 30) { s_poll_ms = now; pin_scan_poll(); }
}
#endif

static void short_press(uint8_t i) {
  corium::Log::console(PSTR("Btn: Short"));
  corium::LedSignal::play(LED_ACK);
  corium::Registry::emit(PushButton::TOPIC_PRESS);
  if (i) corium::Registry::emit(PushButton::TOPIC_BACK);
  gcounter.reset_alarm();
}

// long or short, never both
static void check_long(BtnState& b, unsigned long now) {
  if (b.long_fired || (uint32_t)(now - b.down_ms) < LONG_PRESS_MS) return;
  if (!corium::Registry::heard(PushButton::TOPIC_LONG)) return;
  b.long_fired = true;
  corium::Log::console(PSTR("Btn: Long"));
  corium::Registry::emit(PushButton::TOPIC_LONG);
}

static void step_one(uint8_t i, unsigned long now) {
  BtnState& b = s_btn[i];
  if (b.pin < 0) return;
  bool down = read_down(b);
  if (b.deb.step(down, now, DEBOUNCE_MS)) {
    if (down) {
      b.down_ms = now;
      b.long_fired = false;
    } else if (!b.long_fired) {
      short_press(i);
    }
  }
  if (b.deb.level && b.deb.read) check_long(b, now);   // not while settling
}

// every POLL_MS while a pin is set
void PushButton::loop(unsigned long now)
{
#ifdef PIN_SCAN
  pin_scan_loop(now);
#endif
  for (uint8_t i = 0; i < 2; i++) step_one(i, now);
}
#endif
