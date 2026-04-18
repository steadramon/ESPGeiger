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
#include <EasyButton.h>
#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif

static EasyButton pbutton(PUSHBUTTON_PIN);
static volatile bool button_pushed = false;

#ifdef ESP32
static void IRAM_ATTR do_pushbutton() {
  button_pushed = true;
}
#ifdef SSD1306_DISPLAY
static void IRAM_ATTR do_longpress() {
  display.enable_oled_timeout = !display.enable_oled_timeout;
  if (display.enable_oled_timeout) {
    display.oled_timeout = 0;
  }
}
#endif
#else
static void do_pushbutton() {
  button_pushed = true;
}

#ifdef SSD1306_DISPLAY
static void do_longpress() {
  display.enable_oled_timeout = !display.enable_oled_timeout;
  if (display.enable_oled_timeout) {
    display.oled_timeout = 0;
#ifdef ESPGEIGER_HW
    gcounter.blip_led.Blink(200,100).Repeat(2);
#endif
  } else {
#ifdef ESPGEIGER_HW
    gcounter.blip_led.Blink(200,1).Repeat(1);
#endif
  }
}
#endif
#endif

PushButton pushbutton;
EG_REGISTER_MODULE(pushbutton)

PushButton::PushButton() {
}

void PushButton::init()
{
  pbutton.begin();
}

void PushButton::begin()
{
  pbutton.onPressed(do_pushbutton);
#ifdef SSD1306_DISPLAY
  pbutton.onPressedFor(2000, do_longpress);
#endif
}

void PushButton::read()
{
  pbutton.read();
}

bool PushButton::isPressed()
{
  return pbutton.isPressed();
}

void PushButton::loop(unsigned long now)
{
  if (button_pushed) {
    status.led.Blink(200, 1);
#if defined(SSD1306_DISPLAY)
    display.oled_timeout = now;
    if (display.oled_on) {
      display.oled_page = (display.oled_page % OLED_PAGES) + 1;
    }
    display.oled_last_update = 0;
#endif
    gcounter.reset_alarm();
    button_pushed = false;
  }
  pbutton.read();

}
#endif