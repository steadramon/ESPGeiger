/*
  HVTrim.h - HV trim deadband and overvoltage count

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

#ifndef HV_TRIM_H
#define HV_TRIM_H
#include <stdint.h>

static constexpr uint8_t HV_TRIM_N = 8;

// volts, 2 ADC counts
inline int hvtrim_band(int ratio) {
  return (2 * ratio) >> 10;
}

static constexpr uint8_t HV_ADC_MAX = 10;

// sum keeps the fraction
inline int hv_volts(uint32_t ratio, uint32_t sum, uint8_t n, int offset) {
  if (!n) return offset;
  return (int)((ratio * sum / n) >> 10) + offset;
}

// x2, sorts in place
inline uint32_t hv_median2(uint16_t* r, uint8_t n) {
  if (!n) return 0;
  for (uint8_t i = 1; i < n; i++) {
    uint16_t v = r[i];
    uint8_t j = i;
    for (; j > 0 && r[j - 1] > v; j--) r[j] = r[j - 1];
    r[j] = v;
  }
  return (uint32_t)r[(n - 1) / 2] + r[n / 2];
}

inline uint8_t hv_over_count(uint8_t count, int volts, int limit) {
  if (limit <= 0 || volts <= limit) return 0;
  return count < UINT8_MAX ? count + 1 : count;
}

#endif
