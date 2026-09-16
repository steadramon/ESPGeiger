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

#include "../../../AppConfig.h"

#include <Arduino.h>

// default for led.pin, -1 = none
#ifndef LED_SEND_RECEIVE
#if defined(ESP8266) || defined(CONFIG_IDF_TARGET_ESP32)
#define LED_SEND_RECEIVE 2
#else
#define LED_SEND_RECEIVE -1   // C3/S3 boards have no plain LED on 2
#endif
#endif
#ifdef ESP8266
#define LED_PIN_BLOCKED   // every esp8266 module has it on 2
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

extern long start;
extern uint8_t send_indicator; // senders set 2, OLED counts down

#endif
