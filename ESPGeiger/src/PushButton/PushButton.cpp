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

PushButton::PushButton() {
}

void PushButton::init()
{
  pbutton.begin();
}

void PushButton::begin()
{
  pbutton.onPressed(do_pushbutton);
  pbutton.onPressedFor(2000, do_longpress);
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
  if (status.button_pushed == true) {
    status.led.Blink(200, 1);
#if defined(SSD1306_DISPLAY)
    status.oled_timeout = now;
    if (status.oled_on) {
      status.oled_page++;
      status.oled_last_update = 0;
    }
#endif
    gcounter.reset_alarm();
    status.button_pushed = false;
  }
  pbutton.read();

}
#endif