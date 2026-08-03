/*
  SevenSeg.cpp - Chip-neutral 7-segment digit codes and segment mapping.

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
#include "SevenSeg.h"

static const uint8_t SEG7[10] PROGMEM =
  { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

uint8_t codeToSeg(uint8_t code) {
  if (code < 10)          return pgm_read_byte(&SEG7[code]);
  if (code == DIGIT_DASH) return 0x40;
  return 0x00;
}

// codes[0] = rightmost
void codesToSegs(const uint8_t* codes, const bool* dps, uint8_t n, uint8_t* segs) {
  for (uint8_t i = 0; i < n; i++) {
    uint8_t d = (uint8_t)(n - 1 - i);
    segs[i] = (uint8_t)(codeToSeg(codes[d]) | (dps[d] ? 0x80 : 0));
  }
}

void letteredSegs(uint8_t letter, float value, uint8_t n, uint8_t decimals, uint8_t* segs) {
  uint8_t codes[8];
  bool dps[8];
  if (n > 8) n = 8;
  if (n < 2) {   // no room for the letter
    computeDigits(value, n, 0, 0, codes, dps);
    codesToSegs(codes, dps, n, segs);
    return;
  }
  uint8_t m = (uint8_t)(n - 1);
  computeDigits(value, m, decimals, 0, codes, dps);
  if (codes[0] == DIGIT_DASH && decimals) computeDigits(value, m, 0, 0, codes, dps);
  if (codes[0] == DIGIT_DASH) {
    computeDigits(value, n, 0, 0, codes, dps);   // letter goes
    codesToSegs(codes, dps, n, segs);
  } else {
    segs[0] = letter;
    codesToSegs(codes, dps, m, segs + 1);
  }
  for (uint8_t i = 0; i < n; i++)
    if (i != 1) segs[i] &= 0x7F;
}

// int32_t not long, host long is 64-bit
static void plainDigits(float value, uint8_t ndigits, uint8_t decimals,
                        uint8_t dp_bar_level, uint8_t* codes, bool* dps) {
  if (!(value > 0)) value = 0;   // NaN too
  // 10^9 is the most int32 holds
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals && i < 9; i++) scale *= 10;
  int32_t limit = 1;
  for (uint8_t i = 0; i < ndigits && i < 9; i++) limit *= 10;
  // compare as float, a cast past int32 is UB
  float f = value * (float)scale + 0.5f;
  bool overflow = f >= (float)limit;
  int32_t iv = overflow ? 0 : (int32_t)f;

  for (uint8_t i = 0; i < ndigits; i++) {
    if (overflow) { codes[i] = DIGIT_DASH; dps[i] = false; continue; }
    if (iv > 0 || i <= decimals) codes[i] = (uint8_t)(iv % 10);
    else                         codes[i] = DIGIT_BLANK;
    iv /= 10;
    dps[i] = (decimals > 0 && i == decimals) ||
             (decimals == 0 && dp_bar_level && i < dp_bar_level);
  }
}

void computeDigits(float value, uint8_t ndigits, uint8_t decimals,
                   uint8_t dp_bar_level, uint8_t* codes, bool* dps) {
  if (!(value < 0) || ndigits < 2) {
    plainDigits(value, ndigits, decimals, dp_bar_level, codes, dps);
    return;
  }
  // minus takes a digit
  uint8_t n = (uint8_t)(ndigits - 1);
  plainDigits(-value, n, decimals, dp_bar_level, codes, dps);
  codes[n] = DIGIT_BLANK;
  dps[n] = false;
  // after the last digit, overflow is all dashes
  uint8_t m = 0;
  while (m < n && codes[m] < 10) m++;
  if (m == 0) m = n;
  codes[m] = DIGIT_DASH;
  dps[m] = false;
}
