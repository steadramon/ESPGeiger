/*
  MathUtil.h - Small numeric helpers shared across modules.

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

#ifndef MATHUTIL_H
#define MATHUTIL_H

#include <math.h>
#include <stdint.h>
#include <string.h>

template <typename T>
constexpr T clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// 0-100 -> 0..top, squared: eyes see the low end
inline uint32_t brightness_curve(uint32_t pct, uint32_t top) {
  if (pct > 100) pct = 100;
  return (pct * pct * top + 5000) / 10000;
}

// not NaN or inf, int test, no soft-float call
inline bool is_finite(float v) {
  uint32_t b;
  memcpy(&b, &v, sizeof(b));
  return (b & 0x7F800000u) != 0x7F800000u;
}

// 0-1000, part past whole is 1000
inline uint16_t per_mille(uint32_t part, uint32_t whole) {
  if (whole == 0) return 0;
  if (part >= whole) return 1000;
  return (uint16_t)(((uint64_t)part * 1000u) / whole);
}

inline int16_t sat16(int32_t v) {
  if (v >  32767) v =  32767;
  if (v < -32768) v = -32768;
  return (int16_t)v;
}

// 7/8 old, 1/8 new
inline uint32_t ema8(uint32_t avg, uint32_t v) {
  return (avg * 7 + v) >> 3;
}

// NAN avg takes v, NAN v keeps avg
inline float ema_nan(float avg, float v, float alpha) {
  if (isnan(v)) return avg;
  if (isnan(avg)) return v;
  return avg * (1.0f - alpha) + v * alpha;
}

// marsaglia 13/17/5, 0 stays 0
inline uint32_t xorshift32(uint32_t x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

// hwid seed, hi ^ lo
inline uint32_t fold32(uint64_t v) {
  return (uint32_t)(v ^ (v >> 32));
}

#endif
