/*
  Fnv.h - FNV-1a, station slots are hashed with it so it never changes

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

#ifndef FNV_H
#define FNV_H

#include <Arduino.h>

static const uint32_t FNV_BASIS = 2166136261u;
static const uint32_t FNV_PRIME = 16777619u;

// pass h on to hash more than one string
inline uint32_t fnv1a(const char* s, uint32_t h = FNV_BASIS) {
  for (; s && *s; s++) h = (h ^ (uint8_t)*s) * FNV_PRIME;
  return h;
}

inline uint32_t fnv1a_P(PGM_P s, uint32_t h = FNV_BASIS) {
  for (; pgm_read_byte(s); s++) h = (h ^ pgm_read_byte(s)) * FNV_PRIME;
  return h;
}

#endif
