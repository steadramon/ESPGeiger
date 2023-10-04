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

#ifndef PUSHBUTTON_PIN
#define PUSHBUTTON_PIN 14
#endif

static unsigned long _push_debounce = microsecondsToClockCycles(250000);

extern Status status;

#ifdef ESP32
static void IRAM_ATTR do_pushbutton() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - status.last_pushbutton > _push_debounce) {
    portENTER_CRITICAL_ISR(&timerMux);
    status.last_pushbutton = cycles;
    status.button_pushed = true;
    status.led.Blink(200, 200);
    portEXIT_CRITICAL_ISR(&timerMux);
  }
};
#else
static void ICACHE_RAM_ATTR do_pushbutton() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - status.last_pushbutton > _push_debounce) {
    status.last_pushbutton = cycles;
    status.button_pushed = true;
    status.led.Blink(200, 200);
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