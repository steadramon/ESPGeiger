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
#include <Smoothed.h>
#include "jled.h"
#include <CircularBuffer.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

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
#ifndef BLIPLED
#define BLIPLED 15
#endif
#endif

#define TimeLedON                    1
#define MAX_SIZE 90
struct Status {
  const char thingName[11] = "ESPGeiger";
  const char* version = RELEASE_VERSION;
  const char* git_version = GIT_VERSION;
  bool high_cpm_alarm = false;
  Smoothed <float> geigerTicks;
  Smoothed <float> geigerTicks5;
  Smoothed <float> geigerTicks15;
  CircularBuffer<int,45> cpm_history;
  long start = 0;
  bool warmup = true;
  char* geiger_model = "";
  unsigned long total_clicks = 0;
  unsigned long clicks_today = 0;
  float partial_clicks = 0;
  unsigned long last_send = 0;
  unsigned long last_blip = 0;
  unsigned long last_beep = 0;
#ifdef MQTTOUT
  bool mqtt_connected = false;
  unsigned long last_mqtt = 0;
#endif
#ifdef GEIGER_PUSHBUTTON
  unsigned long last_pushbutton = 0;
  bool button_pushed = false;
#endif
#if LED_SEND_RECEIVE_ON == LOW
  JLed led = JLed(LED_SEND_RECEIVE).LowActive();
#else
  JLed led = JLed(LED_SEND_RECEIVE);
#endif
#if defined(SSD1306_DISPLAY)
  int oled_page = 1;
  int oled_last_update = 0;
#endif
#ifdef BLIPLED
  JLed blip_led = JLed(BLIPLED);
  Smoothed <float> hvReading;
#endif
};

#endif