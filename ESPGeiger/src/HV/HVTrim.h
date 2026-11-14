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

// 1 s readings in the trim average
static constexpr uint8_t HV_TRIM_N = 8;

// volts, 2 ADC counts
inline int hvtrim_band(int ratio) {
  return (2 * ratio) >> 10;
}

// ADC samples a reading, median taken
static constexpr uint8_t HV_ADC_MAX = 10;

// volts = adc * ratio >> 10; sum keeps the fraction
inline int hv_volts(uint32_t ratio, uint32_t sum, uint8_t n, int offset) {
  if (!n) return offset;
  return (int)((ratio * sum / n) >> 10) + offset;
}

// esp32_hv before 0.14: raw counts, ~4/3 a mV; 0 and the default stay
inline int hv_ratio_to_mv(int ratio, int def, int max) {
  if (ratio <= 0 || ratio == def) return ratio;
  int r = ratio * 4 / 3;
  return r < max ? r : max;
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

// readings over the limit in a row, 0 limit = off
inline uint8_t hv_over_count(uint8_t count, int volts, int limit) {
  if (limit <= 0 || volts <= limit) return 0;
  return count < UINT8_MAX ? count + 1 : count;
}

// a board's cut-off: the pref can only lower it, 0 = the board's
inline uint16_t hv_ov_limit(uint32_t pref, uint16_t board) {
  if (board && (!pref || pref > board)) return board;
  return (uint16_t)pref;
}

// most duty (of 1024) that keeps the on-time under max_us
inline int hv_duty_cap(int freq, int max_us) {
  return (int)((uint64_t)max_us * (uint32_t)freq * 1024u / 1000000u);   // 200 us at 21 kHz is past 32 bits
}

// soft start, step of steps
inline int hv_ramp_duty(int duty, uint8_t step, uint8_t steps) {
  if (step >= steps) return duty;
  return 1 + (duty - 1) * step / steps;
}

// err = target - volts, past the band with no trim left, in a row
inline uint8_t hv_low_count(uint8_t count, int err, int band, bool top) {
  return top ? hv_over_count(count, err, band) : 0;
}

// ~0 while driving or the ADC top, in a row
inline uint8_t hv_bad_count(uint8_t count, int mv, bool driving, int low_mv, int high_mv) {
  if (mv < high_mv && (!driving || mv >= low_mv)) return 0;
  return count < UINT8_MAX ? count + 1 : count;
}

#endif
