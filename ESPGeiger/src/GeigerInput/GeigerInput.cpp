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
#include "GeigerInput.h"
#include "Counter/Counter.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinSafety.h"

volatile unsigned long _last_blip = 0;
volatile uint32_t s_event_counter = 0;
volatile uint32_t s_isr_entries = 0;  // before debounce

volatile unsigned long _debounce = GEIGER_DEBOUNCE;

// ~600x any real tube
static constexpr uint32_t ISR_STORM_THRESHOLD = 10000;
static constexpr uint8_t  ISR_STORM_COOLDOWN_S = 5;
static uint8_t s_isr_cooldown = 0;

IsrLock::Mux timerMux;

void GeigerInput::begin() {

}

void GeigerInput::set_rx_pin(int pin) {
  const char* why = nullptr;
#ifdef ESP8266
  if (pin == 16) why = PSTR("no interrupt on GPIO16");
#endif
  if (!why) why = PinSafety::claim_input(pin, PSTR("RX"));
  if (why) {
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
  IsrLock::enter(timerMux);
  uint32_t n = s_event_counter;
  s_event_counter = 0;
  IsrLock::leave(timerMux);
  return (int)n;
}

std::atomic<bool> GeigerInput::s_posted{false};

void GeigerInput::secondTicker() {
}

void GeigerInput::setLastBlip()  {
  _last_blip = micros();
}

// inline, isr stays one IRAM function
static inline __attribute__((always_inline)) void count_at(unsigned long cycles) {
  s_isr_entries++;
  if (cycles - _last_blip < _debounce) {
    return;
  }
  IsrLock::enter_isr(timerMux);
  s_event_counter++;
  IsrLock::leave_isr(timerMux);
  _last_blip = cycles;
  Counter::on_pulse((uint32_t)cycles);
}

void EG_ISR_ATTR GeigerInput::countInterrupt() {
  count_at(micros());
}

void GeigerInput::countAt(unsigned long us) {
  count_at(us);
}

// floating pin, EMI
void GeigerInput::checkIsrStorm() {
  if (_rx_pin < 0) return;
  IsrLock::enter(timerMux);
  uint32_t n = s_isr_entries;
  s_isr_entries = 0;
  IsrLock::leave(timerMux);

  if (s_isr_cooldown > 0) {
    if (--s_isr_cooldown == 0) {
      IsrLock::pin_on_fall(_rx_pin, countInterrupt);
      Log::console(PSTR("GeigerPulse: re-armed after storm cooldown"));
    }
    return;
  }

  if (n > ISR_STORM_THRESHOLD) {
    IsrLock::pin_off(_rx_pin);
    s_isr_cooldown = ISR_STORM_COOLDOWN_S;
    Log::console(PSTR("GeigerPulse: ISR storm (%u/s) - parked %us"),
                 (unsigned)n, (unsigned)ISR_STORM_COOLDOWN_S);
  }
}

void GeigerInput::setCounter(int val, bool update) {
  if (update) IsrLock::enter(timerMux);
  s_event_counter = (uint32_t)val;
  if (update) {
    IsrLock::leave(timerMux);
    _last_blip = micros();
  }
}
