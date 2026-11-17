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

#ifndef CORIUM_PINSAFETY_H
#define CORIUM_PINSAFETY_H

#include <Arduino.h>
#include "App/AppConfig.h"

// Max input pin
#ifndef MAX_GPIO_PIN
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define MAX_GPIO_PIN 48
  #elif defined(CONFIG_IDF_TARGET_ESP32S2)
    #define MAX_GPIO_PIN 46
  #elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #define MAX_GPIO_PIN 30
  #elif defined(CONFIG_IDF_TARGET_ESP32C3)
    #define MAX_GPIO_PIN 21
  #elif defined(ESP32)
    #define MAX_GPIO_PIN 39
  #elif defined(ESP8266)
    #define MAX_GPIO_PIN 16
  #else
    #define MAX_GPIO_PIN 39
  #endif
#endif

#ifndef CORIUM_PINSAFETY_MAX_CLAIMS
#define CORIUM_PINSAFETY_MAX_CLAIMS 16
#endif

namespace corium {
namespace PinSafety {

extern const char I2C[];   // shared bus, one owner
extern const char OUT_OF_RANGE[];

// nullptr = ok, else why or the other owner (PROGMEM)
const char* claim(int pin, const char* owner);
const char* claimOutput(int pin, const char* owner);
const char* claimInput(int pin, const char* owner);
const char* claimButton(int pin, const char* owner);   // input, no strap note

enum Use : uint8_t { USE_OUT, USE_IN, USE_BUTTON };

// a pin the board wires, a null owner ends the table (PROGMEM)
struct Fixed { const char* owner; int8_t pin; Use use; };

// before prefs load: each claimed, and no pref can take one
void reserve(const Fixed* rows);
// shared ones, none claimed: a pref saves with a note, the first module takes it, warned
void share(const Fixed* rows);

enum PinKind : uint8_t {
  PK_FLASH,       // refused
  PK_PSRAM,       // refused
  PK_MISSING,     // no such GPIO, refused
  PK_USB,         // refused when USB is the console
  PK_INPUT_ONLY,  // outputs refused
  PK_STRAP,       // inputs warned, a pull the wrong way at power-on stops boot
  PK_NO_PULL,     // inputs warned, no internal pull-up
  PK_NO_IRQ,      // warned when NEED_IRQ
  PK_NO_PWM,      // warned when NEED_PWM
  PK_NO_OD,       // warned when NEED_OD
  PK_GLITCH,      // warned when NEED_IRQ, false edges
};

// what a pin is for, a kind it lacks is a warning
enum Need : uint8_t { NEED_IRQ = 1, NEED_PWM = 2, NEED_OD = 4 };

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
const char* check(const PinRule* rows, int pin, bool out, bool usb, const char** warn,
                  uint8_t needs = 0);

// this chip's table and the reserved pins, no claim; -1 = no pin; *warn as a claim's
const char* refuse(int pin, Use use, uint8_t needs, const char** warn);
constexpr size_t WHY_MAX = 24;   // longest refuse() text, with the nul

}  // namespace PinSafety
}  // namespace corium

#endif
