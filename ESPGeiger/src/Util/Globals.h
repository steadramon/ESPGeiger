/*
  Globals.h - Process-wide shared state (what Status.h used to hold).

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
#ifndef UTIL_GLOBALS_H
#define UTIL_GLOBALS_H

// Highest valid GPIO number for pin-bounded prefs. Input-capable range.
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

// ESPGeiger-HW product identity implies a known set of capabilities. The
// official base:espgeigerhw env sets these as build flags directly; this
// block catches custom build paths that define only ESPGEIGER_HW.
#ifdef ESPGEIGER_HW
#ifndef GEIGER_BLIPLED
#define GEIGER_BLIPLED 15
#endif
#ifndef ESPG_HV
#define ESPG_HV
#endif
#ifndef ESPG_HV_ADC
#define ESPG_HV_ADC
#endif
#endif

#include <Arduino.h>

#ifndef LED_SEND_RECEIVE
#define LED_SEND_RECEIVE 2
#endif
#ifndef LED_SEND_RECEIVE_ON
#ifdef ESP8266
#define LED_SEND_RECEIVE_ON LOW
#else
#define LED_SEND_RECEIVE_ON HIGH
#endif
#endif

#ifndef ESPG_WARMUP_S
#define ESPG_WARMUP_S 10
#endif

extern long start;             // ntpclient.getUptime() at end of setup()
extern bool past_warmup;       // flips true ESPG_WARMUP_S seconds after setup
extern uint8_t send_indicator; // senders set to 2; OLED decrements as it paints

#endif
