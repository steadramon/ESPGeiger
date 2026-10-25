/*
  ChipVoice.h - Per chip click and blip shape, so two units sound apart.

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
#ifndef CHIP_VOICE_H
#define CHIP_VOICE_H

#include <stdint.h>
#include "Corium/Util/MathUtil.h"
#include "App/AppMath.h"

// AudioTick click pitch, +/-3%
inline float chip_click_pitch(uint64_t hwid) {
  uint32_t f = fold32(hwid);
  uint8_t mixed = (uint8_t)(f ^ (f >> 8) ^ (f >> 16) ^ (f >> 24));
  return jitter_factor(mixed, 0.03f);
}

struct ChipBlip {
  float pulse;
  float freq;
};

// PulseOut, +/-15% width, +/-3% freq
inline ChipBlip chip_blip(uint64_t hwid) {
  ChipBlip v;
  uint32_t r = xorshift32(fold32(hwid));
  v.pulse = jitter_factor((uint8_t)r, 0.15f);
  r = xorshift32(r);
  v.freq = jitter_factor((uint8_t)r, 0.03f);
  return v;
}

#endif
