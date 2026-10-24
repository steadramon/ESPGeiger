/*
  PinProbe.h - what resistor a board fits on a pin, for straps and revisions.

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

#ifndef PINPROBE_H
#define PINPROBE_H

#include <stdint.h>

namespace PinProbe {

enum Pull : uint8_t { NONE, UP, DOWN };

// a fitted resistor beats the chip's own pull, odd reads claim nothing
inline Pull from_reads(bool high_on_down, bool high_on_up) {
  if (high_on_down && high_on_up) return UP;
  if (!high_on_down && !high_on_up) return DOWN;
  return NONE;
}

#ifdef ESP32
// esp8266 has no pull-down. Leaves it INPUT, the caller sets it back
Pull pulled(int pin);
#endif

}

#endif
