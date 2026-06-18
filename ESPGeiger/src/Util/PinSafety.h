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
#include "Globals.h"

namespace PinSafety {

// Register pin as in use. Same-owner re-claim is silent (pointer compare); different-owner warns.
void claim(int pin, const char* owner);

// Pins bonded to internal flash/PSRAM: touching them locks the bus.
inline const char* flash_reserved(int pin) {
#ifdef ESP8266
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
#elif defined(CONFIG_IDF_TARGET_ESP32)
  if (pin >= 6 && pin <= 11) return PSTR("SPI flash");
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  if (pin >= 26 && pin <= 32) return PSTR("SPI flash");
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  if (pin >= 26 && pin <= 32) return PSTR("SPI flash");
  if (pin >= 33 && pin <= 37) return PSTR("PSRAM");      // octal-PSRAM modules
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  if (pin >= 11 && pin <= 17) return PSTR("SPI flash");  // C3FH4 embedded flash
#endif
  return nullptr;
}

// Returns nullptr if pin is safe for output (and claims it), else PROGMEM reason. -1 is no-op.
inline const char* claim_output(int pin, const char* owner) {
  if (pin == -1) return nullptr;
  if (const char* r = flash_reserved(pin)) return r;
#if defined(CONFIG_IDF_TARGET_ESP32)
  if (pin >= 34 && pin <= 39) return PSTR("input only");
#endif
  if (pin < 0 || pin > MAX_GPIO_PIN) return PSTR("out of range");
  claim(pin, owner);
  return nullptr;
}

// Returns nullptr if pin is safe for input (and claims it), else PROGMEM reason. -1 is no-op.
inline const char* claim_input(int pin, const char* owner) {
  if (pin == -1) return nullptr;
  if (const char* r = flash_reserved(pin)) return r;
  if (pin < 0 || pin > MAX_GPIO_PIN) return PSTR("out of range");
  claim(pin, owner);
  return nullptr;
}

}  // namespace PinSafety

#endif
