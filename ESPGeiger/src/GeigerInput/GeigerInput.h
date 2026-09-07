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
#include "../Corium/System/Globals.h"
#include "../Corium/Measure/EGMeasure.h"

// GEIGER_TYPE bits, test builds >= 128
#define GEIGER_FLAG_PULSE   0x01
#define GEIGER_FLAG_SERIAL  0x02
#define GEIGER_FLAG_INTPWM  0x04   // modifier on PULSE
#define GEIGER_FLAG_UDPRX   0x08
// bits 4-6 reserved
#define GEIGER_FLAG_TEST    0x80

#define GEIGER_TYPE_PULSE         (GEIGER_FLAG_PULSE)
#define GEIGER_TYPE_SERIAL        (GEIGER_FLAG_SERIAL)
#define GEIGER_TYPE_UDPRX         (GEIGER_FLAG_UDPRX)
#define GEIGER_TYPE_TEST          (GEIGER_FLAG_TEST)
#define GEIGER_TYPE_TESTPULSE     (GEIGER_FLAG_TEST | GEIGER_FLAG_PULSE)
#define GEIGER_TYPE_TESTSERIAL    (GEIGER_FLAG_TEST | GEIGER_FLAG_SERIAL)
#define GEIGER_TYPE_TESTPULSEINT  (GEIGER_FLAG_TEST | GEIGER_FLAG_PULSE | GEIGER_FLAG_INTPWM)

#define GEIGER_IS_PULSE(t)   ((t) & GEIGER_FLAG_PULSE)
#define GEIGER_IS_SERIAL(t)  ((t) & GEIGER_FLAG_SERIAL)
#define GEIGER_IS_UDPRX(t)   ((t) & GEIGER_FLAG_UDPRX)
#define GEIGER_IS_TEST(t)    ((t) & GEIGER_FLAG_TEST)
#define GEIGER_HAS_INTPWM(t) ((t) & GEIGER_FLAG_INTPWM)

#ifdef ESP32
#include <soc/soc_caps.h>
#endif

// TXPULSE counts internal pulses, C3 has no PCNT
#if defined(ESP32) && SOC_PCNT_SUPPORTED && GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(IGNORE_PCNT) && !defined(GEIGER_COUNT_TXPULSE)
#define USE_PCNT
#endif

// esp8266 test pulses count in loop()
#if defined(ESP8266) && GEIGER_IS_PULSE(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
#define GEIGER_COUNTS_IN_ISR
#elif defined(ESP32) && GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
#define GEIGER_COUNTS_IN_ISR
#endif

// no tube, no click pin
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  #ifndef RXPIN_BLOCKED
    #define RXPIN_BLOCKED
  #endif
  #ifndef TXPIN_BLOCKED
    #define TXPIN_BLOCKED
  #endif
  #ifndef GEIGER_MODEL_FIXED
    #define GEIGER_MODEL_FIXED
  #endif
#endif

#include "SerialFormat.h"

#ifndef GEIGER_SERIALTYPE
#define GEIGER_SERIALTYPE GEIGER_STYPE_GC10
#endif

#ifdef GEIGER_COUNT_TXPULSE
#define GEIGER_RXPIN -1
#endif

#ifndef GEIGER_MODEL
  #if GEIGER_SERIALTYPE == GEIGER_STYPE_GC10
    #define GEIGER_MODEL "GC10"
  #elif GEIGER_SERIALTYPE == GEIGER_STYPE_GC10NX
    #define GEIGER_MODEL "GC10Next"
  #elif GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
    #define GEIGER_MODEL "MightyOhm"
  #elif GEIGER_SERIALTYPE == GEIGER_STYPE_ESPGEIGER
    #define GEIGER_MODEL "ESPG"
  #endif
#endif

#ifndef GEIGER_RXPIN
#define GEIGER_RXPIN 13
#endif

#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 200
#endif

extern volatile unsigned long _last_blip;   // micros()
extern volatile uint32_t s_event_counter;   // isr writes, loop reads and clears

extern volatile unsigned long _debounce;

#ifdef ESP32
extern portMUX_TYPE timerMux;
#endif

class EGHttpResponse;
class EGHttpServer;

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
    void set_rx_pin(int pin);
    void set_tx_pin(int pin);
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
    virtual bool isHealthy() const { return true; }
    virtual uint32_t last_data_ms() const { return (uint32_t)(_last_blip / 1000UL); }
    virtual void registerRoutes(EGHttpServer&) {}
    virtual EGMeasureRows measurements() { return EGMeasureRows(); }
    virtual void appendClicksExtra(EGHttpResponse&) {}
    virtual uint32_t boundary_seconds() const { return 3600; }   // 60 in test
    virtual void stopForOTA() {}
    virtual void restartAfterOTA() {}
    void blip_led();
    unsigned long last_blip() const { return _last_blip; }
    static void IRAM_ATTR countInterrupt();
    // main code only, interrupts off
    static void countAt(unsigned long us);
    void checkIsrStorm();
    void setCounter(int val, bool update = true);
    void setLastBlip();
    // inline gate
    bool poll_due() {
      if (_poll_c < _poll_skip) {
        _poll_c++;
        return false;
      }
      _poll_c = 0;
      return true;
    }
  private:
    void (*callback)(void) = nullptr;
  protected:
    uint16_t _poll_c = 0;
    uint16_t _poll_skip = 0;   // passes between polls
#ifdef GEIGER_TXPIN
    int _tx_pin = GEIGER_TXPIN;
#else
    int _tx_pin = -1;
#endif
    int _rx_pin = GEIGER_RXPIN;
};

#endif
