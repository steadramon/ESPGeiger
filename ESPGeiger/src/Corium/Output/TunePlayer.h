/*
  TunePlayer.h - plays an EGTune on the Buzzer, one call at a time.

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

#ifndef TUNEPLAYER_H
#define TUNEPLAYER_H

#include <stdint.h>
#include "EGTune.h"
#include "Corium/Output/Buzzer.h"

namespace TunePlayer {

static constexpr uint32_t SWEEP_US = 10000;
static constexpr uint16_t MAX_TEXT = 255;

// the app's call
struct Config {
  uint8_t  duty_pct;   // steps with no @
  uint16_t burst_hz;   // t
  uint8_t  gap_pct;    // rtttl: quiet at the end of each note
};
static constexpr Config DEFAULTS = {25, 2000, 12};

// one buzzer call, REST = nothing to send
struct Slice {
  uint8_t  op;
  uint16_t hz;
  uint16_t n;      // tone, burst: cycles. pulse: us
  uint8_t  duty;
  uint32_t us;     // till the next one
};

// next call for st, done_us into it. false = step over
bool slice(const EGTune::Step& st, uint32_t done_us, bool rtttl, const Config& c, Slice& out);

#if defined(BUZZER_ENGINE) && defined(ESP32) && defined(SOC_RMT_SUPPORT_TX_LOOP_COUNT)
void begin(Config c);
// copies the text; dropped unless it outranks what's playing
bool play(uint32_t now_us, const char* s, uint16_t len, uint8_t prio);
void stop();
bool busy();
// from the loop: us till the next call, 0 = done
uint32_t poll(uint32_t now_us);
#endif

}

#endif
