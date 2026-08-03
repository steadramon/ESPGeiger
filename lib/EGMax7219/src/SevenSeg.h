/*
  SevenSeg.h - Chip-neutral 7-segment digit codes and segment mapping.

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
#ifndef EG_SEVENSEG_H
#define EG_SEVENSEG_H
#include <Arduino.h>

// MAX7219 Code B
enum : uint8_t {
  DIGIT_DASH  = 0x0A,
  DIGIT_BLANK = 0x0F,
};

// bit a=0 b=1 c=2 d=3 e=4 f=5 g=6
uint8_t codeToSeg(uint8_t code);

// left to right, bit 7 = DP
void codesToSegs(const uint8_t* codes, const bool* dps, uint8_t n, uint8_t* segs);

enum : uint8_t {
  SEG_C = 0x39,
  SEG_U = 0x3E,
  SEG_t = 0x78,
  SEG_H = 0x76,
};

// colon boards: "U0:26", colon = segs[1] bit 7
void letteredSegs(uint8_t letter, float value, uint8_t n, uint8_t decimals, uint8_t* segs);

// codes[0] = rightmost, decimals 0 = DP row is the bar
void computeDigits(float value, uint8_t ndigits, uint8_t decimals,
                   uint8_t dp_bar_level, uint8_t* codes, bool* dps);

#endif
