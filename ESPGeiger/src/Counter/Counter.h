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

#ifndef GEIGER_RATIO
  #define GEIGER_RATIO 151.0
#endif

#define GEIGER_TYPE_PULSE 1
#define GEIGER_TYPE_SERIAL 2
#define GEIGER_TYPE_TEST 3
#define GEIGER_TYPE_TESTPULSE 4
#define GEIGER_TYPE_TESTSERIAL 5

#ifndef GEIGER_TYPE
#define GEIGER_TYPE GEIGER_TYPE_PULSE
#endif

#define GEIGER_SERIAL_CPM 1
#define GEIGER_SERIAL_CPS 2

#ifndef GEIGER_RXPIN
#define GEIGER_RXPIN 13
#endif

#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 500
#endif

#define GEIGER_STYPE_GC10 1
#define GEIGER_STYPE_GC10NX 2
#define GEIGER_STYPE_MIGHTYOHM 3

#ifndef GEIGER_SERIALTYPE
#define GEIGER_SERIALTYPE GEIGER_STYPE_GC10
#endif

#if GEIGER_TYPE == GEIGER_TYPE_SERIAL || GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
#if GEIGER_SERIALTYPE == GEIGER_STYPE_GC10
  #ifndef GEIGER_BAUDRATE
  #define GEIGER_BAUDRATE 9600
  #endif
  #ifndef GEIGER_MODEL
  #define GEIGER_MODEL "GC10"
  #endif
  #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
#elif GEIGER_SERIALTYPE == GEIGER_STYPE_GC10NX
  #ifndef GEIGER_BAUDRATE
  #define GEIGER_BAUDRATE 115200
  #endif
  #ifndef GEIGER_MODEL
  #define GEIGER_MODEL "GC10Next"
  #endif
  #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
#elif GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  #ifndef GEIGER_BAUDRATE
  #define GEIGER_BAUDRATE 9600
  #endif
  #ifndef GEIGER_MODEL
  #define GEIGER_MODEL "MightyOhm"
  #endif
  #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
#endif
#endif

static int _geiger_rxpin = GEIGER_RXPIN;

#if GEIGER_TYPE == GEIGER_TYPE_PULSE
  #ifndef GEIGER_MODEL
    #define GEIGER_MODEL "genpulse"
  #endif
  #ifndef GEIGER_SERIAL_TYPE
    #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
  #endif
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
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

static char _serial_buffer[64];
static uint8_t _serial_idx = 0;
#include <SoftwareSerial.h>
static EspSoftwareSerial::UART geigerPort;
#define IGNORE_PCNT
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
  #ifndef GEIGER_MODEL
    #define GEIGER_MODEL "test"
  #endif
  #ifndef GEIGER_SERIAL_TYPE
    #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
  #endif
  #define IGNORE_PCNT
  #define GEIGERTESTMODE
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  #ifndef GEIGER_MODEL
    #define GEIGER_MODEL "testpulse"
  #endif
  #ifndef GEIGER_SERIAL_TYPE
    #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
  #endif
  #ifndef GEIGER_TXPIN
    #define GEIGER_TXPIN 12
  #endif
  #define GEIGERTESTMODE
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  #define GEIGERTESTMODE
  #ifndef GEIGER_BAUDRATE
    #define GEIGER_BAUDRATE 115200
  #endif
  #ifndef GEIGER_SERIAL_TYPE
    #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
  #endif
  #ifndef GEIGER_MODEL
    #define GEIGER_MODEL "testserial"
  #endif
  #ifndef GEIGER_TXPIN
    #define GEIGER_TXPIN 12
  #endif
static char _serial_buffer[64];
static uint8_t _serial_idx = 0;
#include <SoftwareSerial.h>
static EspSoftwareSerial::UART geigerPort;
#define IGNORE_PCNT
static int _last_secidx = 0;
  /* MightyOhm CPS, 1, CPM, 60, uSv/hr, 1.23, INST/FAST/SLOW\n */
  /* GC10 60\n */
#endif

#ifndef GEIGER_MODEL
  #define GEIGER_MODEL "genpulse"
#endif

#ifndef GEIGER_TXPIN
  #define GEIGER_TXPIN -1
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

#ifndef GEIGER_CPM_COUNT
#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
#define GEIGER_CPM_COUNT 60
#else
#define GEIGER_CPM_COUNT 60
#endif
#endif
#ifndef GEIGER_CPM5_COUNT
#define GEIGER_CPM5_COUNT 60
#endif
#ifndef GEIGER_CPM15_COUNT
#define GEIGER_CPM15_COUNT 60
#endif

static volatile int eventCounter;
static volatile unsigned long _last_blip;
static unsigned long _debounce = microsecondsToClockCycles(GEIGER_DEBOUNCE);
static int _geiger_txpin = GEIGER_TXPIN;

extern Status status;

static bool _waiting = false;

#if GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
static unsigned long _out_last = 0;
static unsigned long _out_between = _debounce/2;
static int _pulse_state = 0;
static void IRAM_ATTR sendpulse() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _out_last > _out_between) {
    digitalWrite(_geiger_txpin, _pulse_state);
    _pulse_state = !_pulse_state;
    _out_last = cycles;
  }
}
#endif

static bool _handlesecond = false;

#ifdef ESP32
static portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
static hw_timer_t * hwtimer = NULL;

static void IRAM_ATTR count() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _last_blip > _debounce) {
    portENTER_CRITICAL_ISR(&timerMux);
    eventCounter++;
    portEXIT_CRITICAL_ISR(&timerMux);
    _last_blip = cycles;
  }
};

static void IRAM_ATTR handleSecondTick() {
  portENTER_CRITICAL_ISR(&timerMux);
  uint32_t last_clicks = status.total_clicks;
  _handlesecond = true;
#ifdef USE_PCNT
  int16_t pulseCount;
  pcnt_get_counter_value(PCNT_UNIT, &pulseCount);
  pcnt_counter_clear(PCNT_UNIT);
  eventCounter = pulseCount;
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST && ESP32
  eventCounter = timerRead(hwtimer)/600;
  timerRestart(hwtimer);
#endif
#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
  status.geigerTicks.add((float)eventCounter/(float)60);
  status.partial_clicks += (float)eventCounter/(float)60;
#else
  status.geigerTicks.add(eventCounter);
  status.total_clicks += eventCounter;
  eventCounter = 0;
#endif
  _handlesecond = false;
  portEXIT_CRITICAL_ISR(&timerMux);

  if (status.partial_clicks >= 1.0) {
    status.total_clicks += (int)status.partial_clicks;
    status.partial_clicks -= (int)status.partial_clicks;
  }
  time_t currentTime = time (NULL);
  if (currentTime > 0) {
    struct tm *timeinfo = localtime (&currentTime);
    if ((timeinfo->tm_hour == 0) && (timeinfo->tm_min == 0) && (timeinfo->tm_sec == 0)) {
      status.clicks_yesterday = status.clicks_today;
      status.clicks_today = 0;
    }
  }
  status.clicks_today += (status.total_clicks - last_clicks);

  unsigned long int secidx = (millis() / 1000) % 60;
  if (secidx % 5 == 0) {
    status.geigerTicks5.add(status.geigerTicks.get());
  }
  if (secidx % 15 == 0) {
    status.geigerTicks15.add(status.geigerTicks5.get());
  }
};
#else
static void ICACHE_RAM_ATTR count() {
  if (_handlesecond)
    return;
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _last_blip > _debounce) {
    eventCounter++;
    _last_blip = cycles;
  }
};

//static void ICACHE_RAM_ATTR handleSecondTick() {
static void handleSecondTick() {
  uint32_t last_clicks = status.total_clicks;
  _handlesecond = true;
#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
  status.geigerTicks.add((float)eventCounter/(float)60);
  status.partial_clicks += (float)eventCounter/(float)60;
#else
  status.geigerTicks.add(eventCounter);
  status.total_clicks += eventCounter;
  eventCounter = 0;
#endif
  _handlesecond = false;

  if (status.partial_clicks >= 1.0) {
    status.total_clicks += (int)status.partial_clicks;
    status.partial_clicks -= (int)status.partial_clicks;
  }
  int diff = (status.total_clicks - last_clicks);
  time_t currentTime = time (NULL);
  if (currentTime > 0) {
    struct tm *timeinfo = localtime (&currentTime);
    if ((timeinfo->tm_min == 0) && (timeinfo->tm_sec == 0)) {
      status.day_hourly_history.push(status.clicks_hour);
      status.clicks_hour = 0;
      if (timeinfo->tm_hour == 0) {
        status.clicks_yesterday = status.clicks_today;
        status.clicks_today = 0;
      }
    }
  }
  status.clicks_today += diff;
  status.clicks_hour += diff;

  unsigned long int secidx = (millis() / 1000) % 60;
  if (secidx % 5 == 0) {
    status.geigerTicks5.add(status.geigerTicks.get());
  }
  if (secidx % 15 == 0) {
    status.geigerTicks15.add(status.geigerTicks5.get());
  }
};
#endif

static Ticker geigerTicker;

class Counter {
    public:
      Counter();
      void loop();
      float get_cps();
      int get_cpm();
      float get_cpmf();
      int get_cpm5();
      float get_cpm5f();
      int get_cpm15();
      float get_cpm15f();
      float get_usv();
      void set_ratio(float ratio);
      void set_rx_pin(int pin);
      void set_tx_pin(int pin);
      int get_rx_pin();
      int get_tx_pin();
      void begin();
      const char* geiger_model() { return GEIGER_MODEL; };
    private:
      void setup_pulse();
      void handleSerial(char* input);
      float _ratio = GEIGER_RATIO;
};

#endif