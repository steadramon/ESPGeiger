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
volatile uint32_t s_isr_entries = 0;  // counted pre-debounce for storm-detect

volatile unsigned long _debounce = GEIGER_DEBOUNCE;

// 10000 ISR/s is ~600x any physically possible tube rate.
static constexpr uint32_t ISR_STORM_THRESHOLD = 10000;
static constexpr uint8_t  ISR_STORM_COOLDOWN_S = 5;
static uint8_t s_isr_cooldown = 0;

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
  s_isr_entries++;
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

// Park the ISR for a few seconds if it's storming (floating pin / EMI),
// so the rest of the firmware keeps running and the WDT gets fed.
void GeigerInput::checkIsrStorm() {
  if (_rx_pin < 0) return;
  uint32_t n = s_isr_entries;
  s_isr_entries = 0;

  if (s_isr_cooldown > 0) {
    if (--s_isr_cooldown == 0) {
      attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
      Log::console(PSTR("GeigerPulse: re-armed after storm cooldown"));
    }
    return;
  }

  if (n > ISR_STORM_THRESHOLD) {
    detachInterrupt(digitalPinToInterrupt(_rx_pin));
    s_isr_cooldown = ISR_STORM_COOLDOWN_S;
    Log::console(PSTR("GeigerPulse: ISR storm (%u/s) - parked %us"),
                 (unsigned)n, (unsigned)ISR_STORM_COOLDOWN_S);
  }
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
