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

// Clamp `v` into [lo, hi]. constexpr so literal-bound calls fold at compile time.
template <typename T>
constexpr T clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Poisson standard deviation = sqrt(N). 0 when N is non-positive.
inline float poisson_std(float N) {
  return N > 0.0f ? sqrtf(N) : 0.0f;
}

// Deviation of `observed` from `expected` in Poisson standard deviations.
// 0 when expected is too small for a meaningful z-score.
inline float poisson_z(float observed, float expected) {
  float s = poisson_std(expected);
  return s > 0.0f ? (observed - expected) / s : 0.0f;
}

#endif
