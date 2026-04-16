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

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
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
#ifndef GEIGER_BLIPLED
#define GEIGER_BLIPLED 15
#endif
#endif

struct Status {
  const char thingName[11] = "ESPGeiger";
  const char* version = RELEASE_VERSION;
  const char* git_version = GIT_VERSION;
  const char* build_env = BUILD_ENV;
  bool ntp_synced = false;
  long start = 0;
  unsigned long start_time = 0;
  bool warmup = true;
  const char* geiger_model = "";
  unsigned long last_send = 0;
  unsigned long last_blip = 0;
#ifdef MQTTOUT
  bool mqtt_connected = false;
#endif
#ifdef GEIGER_PUSHBUTTON
  bool button_pushed = false;
#endif
#if LED_SEND_RECEIVE_ON == LOW
  JLed led = JLed(LED_SEND_RECEIVE).LowActive();
#else
  JLed led = JLed(LED_SEND_RECEIVE);
#endif
  unsigned long oled_timeout = 0;
#if defined(SSD1306_DISPLAY)
  uint8_t oled_page = 1;
  unsigned long oled_last_update = 0;
  bool oled_on = true;
  bool enable_oled_timeout = true;
#endif
#ifdef GEIGER_BLIPLED
  JLed blip_led = JLed(GEIGER_BLIPLED).Stop();
#endif
#ifdef ESPGEIGER_HW
  Smoothed <float> hvReading;
#endif
  uint16_t serialOut = 0;
  bool wifi_disabled = false;
  unsigned long wifi_lost_at = 0;
  bool wifi_was_connected = false;
  uint32_t tick_us = 0;     // sTickerCB duration, EMA-smoothed (α = 1/8)
  uint32_t tick_max_us = 0; // peak tick_us since the last 60s window reset
  uint32_t lps = 0;         // loop iterations counted in the last second
};

#endif