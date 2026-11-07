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

static constexpr uint16_t ISR_STORM_COOLDOWN_S = 5;
static constexpr uint16_t ISR_STORM_MAX_S = 300;
static uint16_t s_cooldown_s = 0;
static uint16_t s_park_s = ISR_STORM_COOLDOWN_S;   // doubles per storm, a quiet second resets
static bool     s_isr_stopped = false;   // ota, the cooldown leaves it off

corium::IsrLock::Mux timerMux;

void GeigerInput::begin() {
  s_park_s = ISR_STORM_COOLDOWN_S;
}

void GeigerInput::set_rx_pin(int pin) {
  const char* why = nullptr;
#ifdef ESP8266
  if (pin == 16) why = PSTR("no interrupt on GPIO16");
#endif
  if (!why) why = corium::PinSafety::claimInput(pin, PSTR("RX"));
  if (why) {
    corium::Log::console(PSTR("Counter: rx_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _rx_pin = pin;
}

void GeigerInput::set_tx_pin(int pin) {
  if (const char* why = corium::PinSafety::claimOutput(pin, PSTR("TX"))) {
    corium::Log::console(PSTR("Counter: tx_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _tx_pin = pin;
}

int GeigerInput::collect() {
  corium::IsrLock::enter(timerMux);
  uint32_t n = s_event_counter;
  s_event_counter = 0;
  corium::IsrLock::leave(timerMux);
  return (int)n;
}

std::atomic<bool> GeigerInput::s_posted{false};

void GeigerInput::secondTicker() {
}

void GeigerInput::setLastBlip()  {
  _last_blip = micros();
}

// inline, isr stays one IRAM function
static inline __attribute__((always_inline)) void count_at(unsigned long us) {
  s_isr_entries++;
  if (us - _last_blip < _debounce) {
    return;
  }
  corium::IsrLock::enterIsr(timerMux);
  s_event_counter++;
  corium::IsrLock::leaveIsr(timerMux);
  _last_blip = us;
  Counter::on_pulse((uint32_t)us);
}

void CORIUM_ISR_ATTR GeigerInput::countInterrupt() {
  count_at(micros());
}

void GeigerInput::countAt(unsigned long us) {
  count_at(us);
}

// floating pin, EMI
void GeigerInput::checkIsrStorm() {
  if (_rx_pin < 0) return;
  corium::IsrLock::enter(timerMux);
  uint32_t n = s_isr_entries;
  s_isr_entries = 0;
  corium::IsrLock::leave(timerMux);

  if (s_cooldown_s > 0) {
    if (--s_cooldown_s == 0 && !s_isr_stopped) {
      corium::IsrLock::pinOnFall(_rx_pin, countInterrupt);
      corium::Log::console(PSTR("GeigerPulse: re-armed after storm cooldown"));
    }
    return;
  }

  if (n > STORM_COUNTS) {
    corium::IsrLock::pinOff(_rx_pin);
    Counter::drop_pulses(s_event_counter);   // and the ring's
    setCounter(0, false);   // junk, collect() hands 0
    s_cooldown_s = s_park_s;
    corium::Log::console(PSTR("GeigerPulse: ISR storm (%u/s) - parked %us"),
                 (unsigned)n, (unsigned)s_park_s);
    s_park_s = s_park_s >= ISR_STORM_MAX_S / 2 ? ISR_STORM_MAX_S : s_park_s * 2;
  } else {
    s_park_s = ISR_STORM_COOLDOWN_S;
  }
}

void GeigerInput::stopIsr() {
  s_isr_stopped = true;
  corium::IsrLock::pinOff(_rx_pin);
}

// a storm cooldown re-arms it when done
void GeigerInput::restartIsr() {
  s_isr_stopped = false;
  if (s_cooldown_s == 0) corium::IsrLock::pinOnFall(_rx_pin, countInterrupt);
}

void GeigerInput::setCounter(int val, bool update) {
  if (update) corium::IsrLock::enter(timerMux);
  s_event_counter = (uint32_t)val;
  if (update) {
    corium::IsrLock::leave(timerMux);
    _last_blip = micros();
  }
}
