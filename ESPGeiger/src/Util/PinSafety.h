/*
  PinSafety.h - GPIO safety check + in-use registry.

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

namespace PinSafety {

// Register pin as in use. Same-owner re-claim is silent (pointer compare); different-owner warns.
void claim(int pin, const char* owner);

// Returns nullptr if pin is safe for output (and claims it), else PROGMEM reason. -1 is no-op.
inline const char* claim_output(int pin, const char* owner) {
  if (pin == -1) return nullptr;
#ifdef ESP8266
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 16)   return PSTR("out of range");
#elif defined(ESP32)
  if (pin >= 6 && pin <= 11)  return PSTR("SPI flash");
  if (pin >= 34 && pin <= 39) return PSTR("input only");
  if (pin < 0 || pin > 39)    return PSTR("out of range");
#endif
  claim(pin, owner);
  return nullptr;
}

// Returns nullptr if pin is safe for input (and claims it), else PROGMEM reason. -1 is no-op.
inline const char* claim_input(int pin, const char* owner) {
  if (pin == -1) return nullptr;
#ifdef ESP8266
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 16)   return PSTR("out of range");
#elif defined(ESP32)
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
  if (pin < 0 || pin > 39)   return PSTR("out of range");
#endif
  claim(pin, owner);
  return nullptr;
}

}  // namespace PinSafety

#endif
