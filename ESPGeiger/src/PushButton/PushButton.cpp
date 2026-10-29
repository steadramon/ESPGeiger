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
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/System/PinSafety.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/LedPatterns.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/Debounce.h"
#include <Ticker.h>
#ifdef SSD1306_DISPLAY
#include "Display/Display.h"
#endif

static const uint16_t POLL_MS      = 10;
static const uint16_t DEBOUNCE_MS  = 30;
static const uint16_t LONG_PRESS_MS = 2000;

// 0 = next page, 1 = back. loop task only, pin set with the ticker off
struct BtnState {
  int      pin = -1;
  Debounce deb;
  uint32_t press_start_ms = 0;
  bool     long_fired = false;
};
static BtnState s_btn[2];

static Ticker buttonTicker;
static bool s_raw[2];   // ticker only

static bool read_down(const BtnState& b) { return digitalRead(b.pin) == LOW; }

// samples, a change wakes the loop
static void button_tick() {
  bool moved = false;
  for (uint8_t i = 0; i < 2; i++) {
    if (s_btn[i].pin < 0) continue;
    bool down = read_down(s_btn[i]);
    if (down == s_raw[i]) continue;
    s_raw[i] = down;
    moved = true;
  }
  if (moved) EGModuleRegistry::post(&pushbutton);
}

#ifdef SSD1306_DISPLAY
static void do_longpress(unsigned long now) {
  Log::console(PSTR("Btn: long"));
  if (display.toggle_auto_off(now)) {
    LedSignal::play(LED_DISP_ON);
  } else {
    LedSignal::play(LED_DISP_OFF);
  }
}
#endif

PushButton pushbutton;
EG_REGISTER_FEATURE(pushbutton, FEAT_BUTTON)

#ifndef BTN_PIN_BLOCKED
#define BTN_STR_(x) #x
#define BTN_STR(x)  BTN_STR_(x)

EG_PSTR(BTN_L_PIN, "Button Pin");
EG_PSTR(BTN_H_PIN, "GPIO for pushbutton (-1 = disabled). Internal pull-up, active LOW.");

static const EGPref BTN_PREF_ITEMS[] PROGMEM = {
  {EG_K_PIN, BTN_L_PIN, BTN_H_PIN, BTN_STR(PUSHBUTTON_PIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, EGP_PIN_BTN},
};

static const EGPrefGroup BTN_PREF_GROUP PROGMEM = {
  "btn", "Button", 1,
  BTN_PREF_ITEMS,
  sizeof(BTN_PREF_ITEMS) / sizeof(BTN_PREF_ITEMS[0]),
};

const EGPrefGroup* PushButton::prefs_group() { return &BTN_PREF_GROUP; }

void PushButton::on_prefs_loaded() {
  int pin1 = (int)EGPrefs::getInt("btn", EG_K_PIN);
  set_pin(pin1);
  // pin2 build flag only
  if (PUSHBUTTON_PIN2 >= 0) set_pin2(PUSHBUTTON_PIN2);
  Log::console(PSTR("Btn: pin=%d pin2=%d"), pin1, (int)PUSHBUTTON_PIN2);
}
#endif

PushButton::PushButton() {
}

// detach only on change, keeps the prefs cascade quiet
static void set_pin_slot(uint8_t slot, int pin) {
  if (slot >= 2) return;
  BtnState& b = s_btn[slot];
  if (const char* why = PinSafety::claim_button(pin, PSTR("Btn"))) {
    Log::console(PSTR("Btn: pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  if (pin == b.pin && buttonTicker.active()) return;
  buttonTicker.detach();
  uint32_t now = fast_millis();
  bool down = false;
  b.pin = pin;
  if (pin >= 0) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(20);
    down = read_down(b);
  }
  // held at boot: no short on release
  b.deb.reset(down, now);
  b.press_start_ms = now;
  b.long_fired = down;
  s_raw[slot] = down;
  if (s_btn[0].pin >= 0 || s_btn[1].pin >= 0) {
    buttonTicker.attach_ms(POLL_MS, button_tick);
  }
}

void PushButton::set_pin(int pin)  { set_pin_slot(0, pin); }
void PushButton::set_pin2(int pin) { set_pin_slot(1, pin); }

void PushButton::init()
{
  static bool ready = false;
  if (ready) return;
  if (s_btn[0].pin >= 0) set_pin_slot(0, s_btn[0].pin);
  if (s_btn[1].pin >= 0) set_pin_slot(1, s_btn[1].pin);
  ready = true;
}

void PushButton::begin()
{
#ifdef BTN_PIN_BLOCKED
  if (PUSHBUTTON_PIN  >= 0) set_pin(PUSHBUTTON_PIN);
  if (PUSHBUTTON_PIN2 >= 0) set_pin2(PUSHBUTTON_PIN2);
  Log::console(PSTR("Btn: pin=%d pin2=%d"),
               (int)PUSHBUTTON_PIN, (int)PUSHBUTTON_PIN2);
#endif
#ifdef PIN_SCAN
  EGModuleRegistry::every(this, LOOP_MS);
#endif
  init();
}

// samples too, setup has no loop
bool PushButton::isPressed()
{
  uint32_t now = fast_millis();
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
  static uint8_t last[49] = {0};
  static bool primed = false;
  if (!primed) {
    delay(5);
    for (int p = 1; p <= 48; p++) last[p] = digitalRead(p) ? 1 : 0;
    primed = true;
    return;
  }
  for (int p = 1; p <= 48; p++) {
    if (pin_scan_skips(p)) continue;
    uint8_t cur = digitalRead(p) ? 1 : 0;
    if (cur != last[p]) {
      Log::console(PSTR("PinScan: GPIO%d %s"), p, cur ? "HIGH" : "LOW");
      last[p] = cur;
    }
  }
}

static void pin_scan_loop(unsigned long now) {
  static bool inited = false;
  if (!inited) { pin_scan_init(); inited = true; }
  static unsigned long s_last = 0;
  if (now - s_last >= 30) { s_last = now; pin_scan_poll(); }
}
#endif

static void short_press(uint8_t i, unsigned long now) {
  Log::console(PSTR("Btn: short"));
  LedSignal::play(LED_ACK);
  EGModuleRegistry::emit(PushButton::TOPIC_PRESS);
#ifdef SSD1306_DISPLAY
  display.button(now, i == 0 ? 1 : -1);
#endif
  gcounter.reset_alarm();
}

static void soonest(Deadline& next, uint32_t t) {
  if (!next.armed() || !reached(t, next.at)) next.set(t);
}

// long or short, never both
static void check_long(BtnState& b, unsigned long now, Deadline& next) {
#ifdef SSD1306_DISPLAY
  if (b.long_fired) return;
  if (now - b.press_start_ms < LONG_PRESS_MS) {
    soonest(next, b.press_start_ms + LONG_PRESS_MS);
    return;
  }
  b.long_fired = true;
  do_longpress(now);
#endif
}

static void step_one(uint8_t i, unsigned long now, Deadline& next) {
  BtnState& b = s_btn[i];
  if (b.pin < 0) return;
  bool down = read_down(b);
  if (b.deb.step(down, now, DEBOUNCE_MS)) {
    if (down) {
      b.press_start_ms = now;
      b.long_fired = false;
    } else if (!b.long_fired) {
      short_press(i, now);
    }
  }
  if (b.deb.read != b.deb.level) soonest(next, b.deb.since + DEBOUNCE_MS);   // settling
  else if (b.deb.level) check_long(b, now, next);
}

// woken by the ticker on a change, else the debounce or long press due
void PushButton::loop(unsigned long now)
{
#ifdef PIN_SCAN
  pin_scan_loop(now);
#endif
  Deadline next;
  for (uint8_t i = 0; i < 2; i++) step_one(i, now, next);
  if (next.armed()) EGModuleRegistry::due_at(this, next.at);
}
#endif
