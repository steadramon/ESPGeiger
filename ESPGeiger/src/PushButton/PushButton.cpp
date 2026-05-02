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
#include <Ticker.h>
#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif

static const uint16_t POLL_MS      = 5;
static const uint16_t DEBOUNCE_MS  = 30;
static const uint16_t LONG_PRESS_MS = 2000;

static Ticker buttonTicker;
static volatile bool s_last_read = false;
static volatile uint32_t s_last_change_ms = 0;
static volatile uint32_t s_press_start_ms = 0;
static volatile bool s_pressed = false;
static volatile bool s_short_press_pending = false;
static volatile bool s_long_press_fired = false;

static void IRAM_ATTR button_tick() {
  bool down = (digitalRead(PUSHBUTTON_PIN) == LOW);
  if (down != s_last_read) {
    s_last_read = down;
    s_last_change_ms = millis();
    return;
  }
  if (down == s_pressed) return;
  uint32_t now = millis();
  if (now - s_last_change_ms < DEBOUNCE_MS) return;
  s_pressed = down;
  if (down) {
    s_press_start_ms = now;
    s_long_press_fired = false;
  } else if (!s_long_press_fired) {
    s_short_press_pending = true;
  }
}

#ifdef SSD1306_DISPLAY
static void do_longpress() {
  display.enable_oled_timeout = !display.enable_oled_timeout;
  if (display.enable_oled_timeout) {
    display.oled_timeout = 0;
#ifdef HAS_EXT_BLIP
    if (gcounter.ext_blip_led) gcounter.ext_blip_led->Blink(200,100).Repeat(2);
#endif
  } else {
#ifdef HAS_EXT_BLIP
    if (gcounter.ext_blip_led) gcounter.ext_blip_led->Blink(200,1).Repeat(1);
#endif
  }
}
#endif

PushButton pushbutton;
EG_REGISTER_MODULE(pushbutton)

PushButton::PushButton() {
}

void PushButton::init()
{
  static bool ready = false;
  if (ready) return;
  pinMode(PUSHBUTTON_PIN, INPUT_PULLUP);
  delayMicroseconds(20);
  bool down = (digitalRead(PUSHBUTTON_PIN) == LOW);
  s_last_read = down;
  s_pressed = down;
  s_last_change_ms = millis();
  s_press_start_ms = s_last_change_ms;
  s_long_press_fired = down;
  buttonTicker.attach_ms(POLL_MS, button_tick);
  ready = true;
}

void PushButton::begin()
{
  init();
}

bool PushButton::isPressed()
{
  return s_pressed;
}

void PushButton::loop(unsigned long now)
{
#ifdef SSD1306_DISPLAY
  if (s_pressed && !s_long_press_fired && (now - s_press_start_ms) >= LONG_PRESS_MS) {
    s_long_press_fired = true;
    do_longpress();
  }
#endif
  if (s_short_press_pending) {
    s_short_press_pending = false;
    led.Blink(200, 1);
#if defined(SSD1306_DISPLAY)
    display.onButtonTap(now);
#endif
    gcounter.reset_alarm();
  }
}
#endif
