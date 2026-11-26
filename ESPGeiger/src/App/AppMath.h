/*
  AppMath.h - Jitter and the Poisson helpers.

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
#ifndef APP_MATH_H
#define APP_MATH_H

#include <math.h>
#include <stdint.h>

// byte 0-255 -> 1 +/- span, 128 = 1
inline float jitter_factor(uint8_t b, float span) {
  return 1.0f + ((int32_t)b - 128) * (span / 128.0f);
}

inline float poisson_std(float N) {
  return N > 0.0f ? sqrtf(N) : 0.0f;
}

inline float poisson_z(float observed, float expected) {
  float s = poisson_std(expected);
  return s > 0.0f ? (observed - expected) / s : 0.0f;
}

#endif
