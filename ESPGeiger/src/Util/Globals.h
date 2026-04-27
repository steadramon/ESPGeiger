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
#ifndef GEIGER_MODEL_FIXED
#define GEIGER_MODEL_FIXED
#endif
#endif

#include <Arduino.h>
#include "jled.h"

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

extern long start;             // NTP.getUptime() at end of setup()
extern JLed led;               // shared send/receive activity LED
extern bool past_warmup;       // flips true ESPG_WARMUP_S seconds after setup
extern uint8_t send_indicator; // senders set to 2; OLED decrements as it paints

#endif
