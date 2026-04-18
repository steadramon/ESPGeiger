/*
  Status.h - Status class
  
  Copyright (C) 2023 @steadramon

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

#ifndef Status_h
#define Status_h

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

#ifdef ESPGEIGER_HW
#ifndef GEIGER_BLIPLED
#define GEIGER_BLIPLED 15
#endif
#endif

struct Status {
  long start = 0;
#if LED_SEND_RECEIVE_ON == LOW
  JLed led = JLed(LED_SEND_RECEIVE).LowActive();
#else
  JLed led = JLed(LED_SEND_RECEIVE);
#endif
};

#endif