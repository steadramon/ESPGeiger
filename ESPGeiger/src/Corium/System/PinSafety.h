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

extern const char I2C[];   // shared bus, one owner

// nullptr = ok, else why or the other owner (PROGMEM)
const char* claim(int pin, const char* owner);
const char* claim_output(int pin, const char* owner);
const char* claim_input(int pin, const char* owner);
const char* claim_button(int pin, const char* owner);   // input, no strap note

enum PinKind : uint8_t {
  PK_FLASH,       // refused
  PK_PSRAM,       // refused
  PK_MISSING,     // no such GPIO, refused
  PK_USB,         // refused when USB is the console
  PK_INPUT_ONLY,  // outputs refused
  PK_STRAP,       // inputs warned, a pull the wrong way at power-on stops boot
  PK_NO_PULL,     // inputs warned, no internal pull-up
};

// lo..hi inclusive, a lo of PIN_END ends the table
struct PinRule { uint8_t lo, hi; PinKind kind; };
constexpr uint8_t PIN_END = 255;

// one per chip, the build uses its own
extern const PinRule ESP8266_PINS[];
extern const PinRule ESP32_PINS[];
extern const PinRule ESP32S2_PINS[];
extern const PinRule ESP32S3_PINS[];
extern const PinRule ESP32C3_PINS[];
extern const PinRule ESP32C6_PINS[];

// nullptr = ok, else why; *warn = a note worth logging
const char* check(const PinRule* rows, int pin, bool out, bool usb, const char** warn);

// this chip's table only, no claim; -1 = no pin
const char* refuse(int pin, bool out);

}  // namespace PinSafety

#endif
