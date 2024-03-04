/*
  PushButton.h - User interaction via button

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
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H
#include <Arduino.h>
#include "../Status.h"
#include "../Counter/Counter.h"
#include <EasyButton.h>

#ifndef PUSHBUTTON_PIN
#define PUSHBUTTON_PIN 14
#endif

extern Status status;
extern Counter gcounter;
static EasyButton pbutton(PUSHBUTTON_PIN);

#ifdef ESP32
static void IRAM_ATTR do_pushbutton() {
  status.button_pushed = true;
};
static void IRAM_ATTR do_longpress() {
  status.enable_oled_timeout = !status.enable_oled_timeout;
  if (status.enable_oled_timeout) {
    status.oled_timeout = 0;
  }
};
#else
static void do_pushbutton() {
  status.button_pushed = true;
}

static void do_longpress() {
  status.enable_oled_timeout = !status.enable_oled_timeout;
  if (status.enable_oled_timeout) {
    status.oled_timeout = 0;
#ifdef ESPGEIGER_HW
    status.blip_led.Blink(200,100).Repeat(2);
#endif
  } else {
#ifdef ESPGEIGER_HW
    status.blip_led.Blink(200,1).Repeat(1);
#endif
  }
}
#endif

class PushButton {
  public:
    PushButton();
    void loop();
    void begin();
};

#endif
#endif