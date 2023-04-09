/*
  Counter.h - Geiger Counter class
  
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

#ifndef COUNTER_H
#define COUNTER_H
#include <Arduino.h>
#include "../Status.h"
#include <Ticker.h>

#define GEIGER_SERIAL_CPM 1
#define GEIGER_SERIAL_CPS 2

#ifndef GEIGER_RXPIN
#define GEIGER_RXPIN 13
#endif

#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 500
#endif

static int _geiger_rxpin = GEIGER_RXPIN;

#ifdef SERIALGEIGER
  #ifndef GEIGER_BAUDRATE
    #define GEIGER_BAUDRATE 115200
  #endif
  #ifndef GEIGER_SERIAL_TYPE
    #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
  #endif
  #ifndef GEIGER_MODEL
    #define GEIGER_MODEL "genserial"
  #endif
  #ifndef GEIGER_TXPIN
    #define GEIGER_TXPIN -1
  #endif

static int _geiger_txpin = GEIGER_TXPIN;

#include <SoftwareSerial.h>
static int _geiger_baud = GEIGER_BAUDRATE;
static EspSoftwareSerial::UART geigerPort;
#else
#ifndef GEIGER_SERIAL_TYPE
#define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
#endif
#ifndef GEIGER_MODEL
  #define GEIGER_MODEL "genpulse"
#endif
#ifdef ESP32
  #ifndef IGNORE_PCNT
    #define USE_PCNT
  #endif
#endif

#ifdef USE_PCNT
extern "C" {
  #include "soc/pcnt_struct.h"
}
#include <driver/pcnt.h>
#define PCNT_HIGH_LIMIT 32767  // largest +ve value for int16_t.
#define PCNT_LOW_LIMIT  0

#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0
#endif
#endif

#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
static int GEIGER_CPM_COUNT = 6;
#elif GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPS
static int GEIGER_CPM_COUNT = 60;
#endif

static int GEIGER_CPM5_COUNT = 60;
static int GEIGER_CPM15_COUNT = 60;

static float eventCounter;  // the event counter
static unsigned long _last = 0;
static unsigned long _debounce = microsecondsToClockCycles(GEIGER_DEBOUNCE);

extern Status status;

static bool _waiting = false;

static void ICACHE_RAM_ATTR count() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _last > _debounce) {
    eventCounter += 1;
    _last = cycles;
  }
};

static void ICACHE_RAM_ATTR handleSecondTick() {
#ifdef USE_PCNT
  int16_t pulseCount;
  pcnt_get_counter_value(PCNT_UNIT, &pulseCount);
  pcnt_counter_clear(PCNT_UNIT);
  eventCounter = pulseCount;
#endif
  status.geigerTicks.add(eventCounter);
  eventCounter = 0;
  unsigned long int secidx = (millis() / 1000) % 60;
  if (secidx % 5 == 0) {
    float avgcpm = status.geigerTicks.get();
    status.geigerTicks5.add(avgcpm);
  }
  if (secidx % 15 == 0) {
    float avgcpm5 = status.geigerTicks5.get();
    status.geigerTicks15.add(avgcpm5);
  }
}

static Ticker geigerTicker;

class Counter {
    public:
      Counter();
      void loop();
      int get_cpm();
      int get_cpm5();
      int get_cpm15();
      float get_usv();
      void set_ratio(float ratio);
      void begin();
    private:
      float _ratio = 100.0;
};

#endif
