/*
  GeigerInput/GeigerInput.h - Geiger Counter Input Class
  
  Copyright (C) 2024 @steadramon

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
#pragma once

#ifndef GEIGERINPUT_H
#define GEIGERINPUT_H
#include <Arduino.h>
#include "../Status.h"

#define GEIGER_TYPE_PULSE 1
#define GEIGER_TYPE_SERIAL 2
#define GEIGER_TYPE_TEST 3
#define GEIGER_TYPE_TESTPULSE 4
#define GEIGER_TYPE_TESTSERIAL 5
#define GEIGER_TYPE_TESTPULSEINT 6

#define GEIGER_STYPE_GC10 1
#define GEIGER_STYPE_GC10NX 2
#define GEIGER_STYPE_MIGHTYOHM 3

#ifndef GEIGER_SERIALTYPE
#define GEIGER_SERIALTYPE GEIGER_STYPE_GC10
#endif

#ifdef GEIGER_COUNT_TXPULSE
#define GEIGER_RXPIN -1
#endif

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

#ifndef GEIGER_RXPIN
#define GEIGER_RXPIN 13
#endif

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN -1
#endif

#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 500
#endif

#ifndef GEIGER_SERIAL_TYPE
#define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
#endif

static bool _eventFlipFlop = false;
static volatile unsigned long _last_blip = 0;
static volatile int eventCounter1 = 0;
static volatile int eventCounter2 = 0;

static unsigned long _debounce = microsecondsToClockCycles(GEIGER_DEBOUNCE);

#ifdef ESP32
static portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

class GeigerInput {
  public:
    virtual ~GeigerInput() {};
    virtual void loop();
    virtual void secondTicker();
    virtual int collect();
    virtual void begin();
    void set_debounce(int debounce) {
      _debounce = microsecondsToClockCycles(debounce);
    }
    void set_rx_pin(int pin) {
      _rx_pin = pin;
    };
    void set_tx_pin(int pin) {
      _tx_pin = pin;
    };
    int get_rx_pin() {
      return _rx_pin;
    };
    int get_tx_pin() {
      return _rx_pin;
    };
    void blip_led();
    unsigned long last_blip();
    const char* geiger_model();
    static void IRAM_ATTR countInterrupt();
    void setCounter(int val, bool update);
    void setCounter(int val);
    void setLastBlip();
    double generatePoissonRandom(double lambda);
  private:
    void (*callback)(void) = nullptr; // Member variable to store callback function pointer
  protected:
    int _tx_pin = GEIGER_TXPIN;
    int _rx_pin = GEIGER_RXPIN;
};

#endif