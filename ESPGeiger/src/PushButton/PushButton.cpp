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

#include <Arduino.h>
#include "PushButton.h"
#include "../Logger/Logger.h"

PushButton::PushButton() {
}

void PushButton::begin()
{
  pinMode(PUSHBUTTON_PIN, INPUT_PULLUP); // declare push button as input
  attachInterrupt(digitalPinToInterrupt(PUSHBUTTON_PIN), do_pushbutton, FALLING);
}

void PushButton::loop()
{
  if (status.button_pushed == true) {
#if defined(SSD1306_DISPLAY)
    status.oled_page++;
    status.oled_last_update = 0;
    if (status.oled_page > 3) {
      status.oled_page = 1;
    }
#endif
    status.high_cpm_alarm = false;
    status.button_pushed = false;
  }
}
#endif