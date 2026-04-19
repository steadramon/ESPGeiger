/*
  Globals.h - Process-wide shared state (what Status.h used to hold).

  Copyright (C) 2026 @steadramon
*/
#ifndef UTIL_GLOBALS_H
#define UTIL_GLOBALS_H

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
