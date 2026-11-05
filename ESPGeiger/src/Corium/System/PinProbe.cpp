/*
  PinProbe.cpp - read a pin against each internal pull in turn.

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
#ifdef ESP32
#include <Arduino.h>
#include "PinProbe.h"

corium::PinProbe::Pull corium::PinProbe::pulled(int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(100);
  bool down = digitalRead(pin) == HIGH;
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(100);
  bool up = digitalRead(pin) == HIGH;
  pinMode(pin, INPUT);
  return fromReads(down, up);
}
#endif
