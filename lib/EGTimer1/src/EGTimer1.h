/*
  EGTimer1.h - the one Timer1 owner for ESP8266 builds

  Copyright (C) 2026 @steadramon

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

// Replaces the core waveform generator, nothing else touches Timer1
// one PWM pin for HV, the rest are soft channels

#ifndef EG_TIMER1_H
#define EG_TIMER1_H

#if defined(ESP8266) && defined(EG_TIMER1)

#include <stddef.h>
#include <stdint.h>

namespace EGTimer1 {

bool pwm_begin(uint8_t pin);   // false = can't drive it
void pwm_set(uint32_t freq, uint32_t duty, uint32_t range);
void pwm_stop();
bool pwm_running();
uint32_t entries();
void restart();   // when entries() stops moving

// fn runs in the NMI: IRAM only, short, nothing noInterrupts() guards
// ticks at 80 MHz, fn returns ticks to next call, 0 stops
void timer_start(uint32_t (*fn)(), uint32_t first);
void timer_stop();

#ifdef EG_TIMER1_STATS
void stats_tick();   // bench, once a second
int stats_json(char* buf, size_t cap, uint8_t part);   // 0 past the last part
#endif

#ifdef EG_SERIAL_NMI
// a dropped byte reads as EGRX_MARK
struct RxStats {
  uint32_t starts;
  uint32_t resyncs;
  uint32_t framing;
  uint32_t framing_first;
  uint32_t late;           // over half a bit
  uint32_t already_low;
  uint32_t ovf;
  uint32_t bytes;
};
bool rx_begin(int8_t pin, uint32_t baud);
int rx_available();
int rx_read();
void rx_pause();
void rx_resume();
void rx_stats(RxStats* out);
#endif

}

#endif
#endif
