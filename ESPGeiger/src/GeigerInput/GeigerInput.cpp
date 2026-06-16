/*
  GeigerInput/GeigerInput.cpp - Geiger Counter Input Class
  
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
#include <Arduino.h>
#include "GeigerInput.h"
#include "../Counter/Counter.h"   // Counter::on_pulse() ring hook
#include "../Logger/Logger.h"
#include "../Util/PinSafety.h"

volatile unsigned long _last_blip = 0;
volatile uint32_t s_event_counter = 0;

volatile unsigned long _debounce = GEIGER_DEBOUNCE;

#ifdef ESP32
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

void GeigerInput::begin() {

}

void GeigerInput::set_rx_pin(int pin) {
  if (const char* why = PinSafety::claim_input(pin, PSTR("RX"))) {
    Log::console(PSTR("Counter: rx_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _rx_pin = pin;
}

void GeigerInput::set_tx_pin(int pin) {
  if (const char* why = PinSafety::claim_output(pin, PSTR("TX"))) {
    Log::console(PSTR("Counter: tx_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _tx_pin = pin;
}

int GeigerInput::collect() {
#ifdef ESP32
  portENTER_CRITICAL(&timerMux);
#else
  noInterrupts();
#endif
  uint32_t n = s_event_counter;
  s_event_counter = 0;
#ifdef ESP32
  portEXIT_CRITICAL(&timerMux);
#else
  interrupts();
#endif
  return (int)n;
}

void GeigerInput::loop() {
}

void GeigerInput::secondTicker() {
}

void GeigerInput::setLastBlip()  {
  _last_blip = micros();
}

void IRAM_ATTR GeigerInput::countInterrupt() {
  unsigned long cycles = micros();
  if (cycles - _last_blip < _debounce) {
    return;
  }
#ifdef ESP32
  portENTER_CRITICAL_ISR(&timerMux);
#endif
  s_event_counter++;
#ifdef ESP32
  portEXIT_CRITICAL_ISR(&timerMux);
#endif
  _last_blip = cycles;
  Counter::on_pulse((uint32_t)cycles);
}

void GeigerInput::setCounter(int val, bool update) {
  if (update) {
#ifdef ESP32
    portENTER_CRITICAL(&timerMux);
#else
    noInterrupts();
#endif
  }
  s_event_counter = (uint32_t)val;
  if (update) {
#ifdef ESP32
    portEXIT_CRITICAL(&timerMux);
#else
    interrupts();
#endif
    _last_blip = micros();
  }
}
