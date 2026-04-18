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

// GEIGER_TYPE bitfield. Bit 7 partitions test (>=128) from real (<128) builds;
// bits 0-2 describe the input pipeline; bits 3-6 reserved for future variants.
// PCNT (ESP32 hardware pulse counter) is an implementation detail of the pulse
// input path, controlled separately via the IGNORE_PCNT compile-time flag.
#define GEIGER_FLAG_PULSE   0x01   // bit 0 - hardware pulse input
#define GEIGER_FLAG_SERIAL  0x02   // bit 1 - UART input
#define GEIGER_FLAG_INTPWM  0x04   // bit 2 - internal PWM generation (modifier on PULSE)
// bits 3-6 reserved
#define GEIGER_FLAG_TEST    0x80   // bit 7 - test/simulation build (real <128, test >=128)

#define GEIGER_TYPE_PULSE         (GEIGER_FLAG_PULSE)
#define GEIGER_TYPE_SERIAL        (GEIGER_FLAG_SERIAL)
#define GEIGER_TYPE_TEST          (GEIGER_FLAG_TEST)
#define GEIGER_TYPE_TESTPULSE     (GEIGER_FLAG_TEST | GEIGER_FLAG_PULSE)
#define GEIGER_TYPE_TESTSERIAL    (GEIGER_FLAG_TEST | GEIGER_FLAG_SERIAL)
#define GEIGER_TYPE_TESTPULSEINT  (GEIGER_FLAG_TEST | GEIGER_FLAG_PULSE | GEIGER_FLAG_INTPWM)

#define GEIGER_IS_PULSE(t)   ((t) & GEIGER_FLAG_PULSE)
#define GEIGER_IS_SERIAL(t)  ((t) & GEIGER_FLAG_SERIAL)
#define GEIGER_IS_TEST(t)    ((t) & GEIGER_FLAG_TEST)
#define GEIGER_HAS_INTPWM(t) ((t) & GEIGER_FLAG_INTPWM)

// Central decision for PCNT usage. Previously duplicated in three Type headers;
// now the single source of truth. ESP32-only hardware. Opt-out via -D IGNORE_PCNT.
// Disabled when GEIGER_COUNT_TXPULSE is active (that build counts internal TX
// pulses, not input pin edges, so PCNT isn't relevant).
#if defined(ESP32) && GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(IGNORE_PCNT) && !defined(GEIGER_COUNT_TXPULSE)
#define USE_PCNT
#endif

#define GEIGER_STYPE_GC10 1
#define GEIGER_STYPE_GC10NX 2
#define GEIGER_STYPE_MIGHTYOHM 3
#define GEIGER_STYPE_ESPGEIGER 4

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
#elif GEIGER_SERIALTYPE == GEIGER_STYPE_ESPGEIGER
  #ifndef GEIGER_BAUDRATE
  #define GEIGER_BAUDRATE 115200
  #endif
  #ifndef GEIGER_MODEL
  #define GEIGER_MODEL "ESPG"
  #endif
  #define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
#endif

#ifndef GEIGER_RXPIN
#define GEIGER_RXPIN 13
#endif


#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 500
#endif

#ifndef GEIGER_SERIAL_TYPE
#define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPS
#endif

extern volatile bool _eventFlipFlop;
extern volatile unsigned long _last_blip;
extern volatile int eventCounter1;
extern volatile int eventCounter2;

extern volatile unsigned long _debounce;

#ifdef ESP32
extern portMUX_TYPE timerMux;
#endif

class GeigerInput {
  public:
    virtual ~GeigerInput() {};
    virtual void loop();
    virtual void secondTicker();
    virtual int collect();
    virtual void begin();
    void set_debounce(int debounce) {
      _debounce = debounce;
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
      return _tx_pin;
    };
    virtual void set_pcnt_filter(int val) {};
    virtual void apply_pcnt_filter() {};
    virtual void set_pin_pull(int mode) {};   // 0=floating, 1=up, 2=down
    virtual bool has_pcnt() { return false; }
    void blip_led();
    // Inline: called from the hot Counter::loop() path ~50k/sec.
    // Not virtual - no subclass overrides it.
    unsigned long last_blip() const { return _last_blip; }
    static void IRAM_ATTR countInterrupt();
    void setCounter(int val, bool update = true);
    void setLastBlip();
    double generatePoissonRandom(double lambda);
  private:
    void (*callback)(void) = nullptr; // Member variable to store callback function pointer
  protected:
#ifdef GEIGER_TXPIN
    int _tx_pin = GEIGER_TXPIN;
#else
    int _tx_pin = -1;
#endif
    int _rx_pin = GEIGER_RXPIN;
};

#endif