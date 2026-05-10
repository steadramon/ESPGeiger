/*
  PinSafety.h - Reject GPIOs reserved for SPI flash or input-only.

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

#ifndef PINSAFETY_H
#define PINSAFETY_H

#include <Arduino.h>

// -1 is the universal "disabled / unset" sentinel and is treated as safe so
// modules can use it as their off marker.
//
// Returns nullptr if `pin` is safe for the requested role, else a PROGMEM
// reason string (use with `%s` in Log::console).
namespace PinSafety {

inline const char* unsafe_output(int pin) {
  if (pin == -1) return nullptr;
#ifdef ESP8266
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 16)   return PSTR("out of range");
#elif defined(ESP32)
  if (pin >= 6 && pin <= 11)  return PSTR("SPI flash");
  if (pin >= 34 && pin <= 39) return PSTR("input only");
  if (pin < 0 || pin > 39)    return PSTR("out of range");
#endif
  return nullptr;
}

inline const char* unsafe_input(int pin) {
  if (pin == -1) return nullptr;
#ifdef ESP8266
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 16)   return PSTR("out of range");
#elif defined(ESP32)
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 39)   return PSTR("out of range");
#endif
  return nullptr;
}

}  // namespace PinSafety

#endif
