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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/PinSafety.h"
#include "../Util/FastMillis.h"
#include <Ticker.h>
#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif

static const uint16_t POLL_MS      = 10;
static const uint16_t DEBOUNCE_MS  = 30;
static const uint16_t LONG_PRESS_MS = 2000;

// Two-slot state array. Slot 0 = "primary" (legacy, advances page on tap).
// Slot 1 = "secondary" (optional, retreats page on tap). Both share long-
// press behaviour. Statics rather than members because the Ticker ISR
// needs to reach them without going through an instance pointer.
struct BtnState {
  volatile int      pin = -1;
  volatile bool     last_read = false;
  volatile uint32_t last_change_ms = 0;
  volatile uint32_t press_start_ms = 0;
  volatile bool     pressed = false;
  volatile bool     short_press_pending = false;
  volatile bool     long_press_fired = false;
};
static BtnState s_btn[2];

static Ticker buttonTicker;

static void tick_one(BtnState& b) {
  if (b.pin < 0) return;
  bool down = (digitalRead(b.pin) == LOW);
  if (down != b.last_read) {
    b.last_read = down;
    b.last_change_ms = fast_millis();
    return;
  }
  if (down == b.pressed) return;
  uint32_t now = fast_millis();
  if (now - b.last_change_ms < DEBOUNCE_MS) return;
  b.pressed = down;
  if (down) {
    b.press_start_ms = now;
    b.long_press_fired = false;
  } else if (!b.long_press_fired) {
    b.short_press_pending = true;
  }
}

static void button_tick() {
  tick_one(s_btn[0]);
  tick_one(s_btn[1]);
}

#ifdef SSD1306_DISPLAY
static void do_longpress() {
  display.enable_oled_timeout = !display.enable_oled_timeout;
  if (display.enable_oled_timeout) {
    display.oled_timeout = 0;
#ifdef GEIGER_BLIPLED
    gcounter.blip_led.Blink(200,100).Repeat(2);
#elif defined(HAS_EXT_BLIP)
    if (gcounter.ext_blip_led) gcounter.ext_blip_led->Blink(200,100).Repeat(2);
#endif
  } else {
#ifdef GEIGER_BLIPLED
    gcounter.blip_led.Blink(200,1).Repeat(1);
#elif defined(HAS_EXT_BLIP)
    if (gcounter.ext_blip_led) gcounter.ext_blip_led->Blink(200,1).Repeat(1);
#endif
  }
}
#endif

PushButton pushbutton;
EG_REGISTER_MODULE(pushbutton)

#ifndef BTN_PIN_BLOCKED
#define _BTN_STR(x) #x
#define BTN_STR(x)  _BTN_STR(x)

EG_PSTR(BTN_L_PIN, "Button Pin");
EG_PSTR(BTN_H_PIN, "GPIO for pushbutton (-1 = disabled). Internal pull-up, active LOW.");

static const EGPref BTN_PREF_ITEMS[] = {
  {"pin", BTN_L_PIN, BTN_H_PIN, BTN_STR(PUSHBUTTON_PIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, 0},
};

static const EGPrefGroup BTN_PREF_GROUP = {
  "btn", "Button", 1,
  BTN_PREF_ITEMS,
  sizeof(BTN_PREF_ITEMS) / sizeof(BTN_PREF_ITEMS[0]),
};

const EGPrefGroup* PushButton::prefs_group() { return &BTN_PREF_GROUP; }

void PushButton::on_prefs_loaded() {
  int pin1 = (int)EGPrefs::getInt("btn", "pin");
  set_pin(pin1);
  // pin2 is build-flag-only (PUSHBUTTON_PIN2); applied in begin().
  if (PUSHBUTTON_PIN2 >= 0) set_pin2(PUSHBUTTON_PIN2);
  Log::console(PSTR("Btn: pin=%d pin2=%d"), pin1, (int)PUSHBUTTON_PIN2);
  EGModuleRegistry::set_loop_interval(this,
    (pin1 < 0 && PUSHBUTTON_PIN2 < 0) ? -1 : 50);
}
#endif

PushButton::PushButton() {
}

// Configure a button slot. Detaches the shared ticker, reconfigures, then
// re-attaches if anything is still wired up. Skipping the detach/reattach
// when nothing changes keeps the on_prefs_loaded cascade quiet.
static void set_pin_slot(uint8_t slot, int pin) {
  if (slot >= 2) return;
  BtnState& b = s_btn[slot];
  if (const char* why = PinSafety::claim_input(pin, PSTR("Btn"))) {
    Log::console(PSTR("Btn: pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  if (pin == b.pin && buttonTicker.active()) return;
  buttonTicker.detach();
  b.pressed = false;
  b.last_read = false;
  b.short_press_pending = false;
  b.long_press_fired = false;
  b.last_change_ms = fast_millis();
  b.press_start_ms = b.last_change_ms;
  b.pin = pin;
  if (pin >= 0) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(20);
    bool down = (digitalRead(pin) == LOW);
    b.last_read = down;
    b.pressed = down;
    b.long_press_fired = down;
  }
  // Re-attach if either slot is active.
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
  init();
}

bool PushButton::isPressed()
{
  return s_btn[0].pressed || s_btn[1].pressed;
}

bool PushButton::is_active()
{
  return s_btn[0].pin >= 0 || s_btn[1].pin >= 0;
}

#ifdef PIN_SCAN
// Diagnostic: configure all safe GPIOs as INPUT_PULLUP at boot, then log
// any transition. Press a button - whichever pin goes LOW is what it's
// wired to. Skip already-claimed pins (I2C, NeoPixel, BOOT) to avoid
// clobbering them.
static void pin_scan_init() {
  for (int p = 1; p <= 48; p++) {
    if (p == 0 || p == 41 || p == 42 || p == 48) continue;
    if (p >= 22 && p <= 25) continue;        // gap on S3
    if (p >= 26 && p <= 37) continue;        // possible flash/PSRAM
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
    if (p == 0 || p == 41 || p == 42 || p == 48) continue;
    if (p >= 22 && p <= 25) continue;
    if (p >= 26 && p <= 37) continue;
    uint8_t cur = digitalRead(p) ? 1 : 0;
    if (cur != last[p]) {
      Log::console(PSTR("PinScan: GPIO%d %s"), p, cur ? "HIGH" : "LOW");
      last[p] = cur;
    }
  }
}
#endif

void PushButton::loop(unsigned long now)
{
#ifdef PIN_SCAN
  static bool inited = false;
  if (!inited) { pin_scan_init(); inited = true; }
  static unsigned long s_last = 0;
  if (now - s_last >= 30) { s_last = now; pin_scan_poll(); }
#endif
  if (s_btn[0].pin < 0 && s_btn[1].pin < 0) return;
#ifdef SSD1306_DISPLAY
  for (uint8_t i = 0; i < 2; i++) {
    BtnState& b = s_btn[i];
    if (b.pin < 0) continue;
    if (b.pressed && !b.long_press_fired && (now - b.press_start_ms) >= LONG_PRESS_MS) {
      b.long_press_fired = true;
      do_longpress();
    }
  }
#endif
  for (uint8_t i = 0; i < 2; i++) {
    BtnState& b = s_btn[i];
    if (!b.short_press_pending) continue;
    b.short_press_pending = false;
    led.Blink(200, 1);
#ifdef SSD1306_DISPLAY
    display.onButtonTap(now, i == 0 ? 1 : -1);
#endif
    gcounter.reset_alarm();
  }
}
#endif
