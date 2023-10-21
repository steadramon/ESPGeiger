/*
  timezones.cpp - Timezone objects for NTP
  
  Copyright (C) 2023 @steadramon

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
#include <Arduino.h>
#include "timezones.h"

const uint32_t mask = 0x1fffff;

const uint32_t NumZones = sizeof(zones) / sizeof(struct TZoneH);

const uint32_t FNV_PRIME = 16777619u;
const uint32_t OFFSET_BASIS = 2166136261u;

// this function computes the "fnv" hash of a string, ignoring the nul
// termination
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
uint32_t fnvHash(const char *str) {
  uint32_t hash = OFFSET_BASIS;
  while (*str) {
    hash ^= uint32_t(*str++);
    hash *= FNV_PRIME;
  }
  return hash;
}

// this function, given a tz name in "Olson" format (like "Australia/Melbourne")
// returns the tz in "Posix" format (like "AEST-10AEDT,M10.1.0,M4.1.0/3"), which
// is what setenv("TZ",...) and settz wants. It does a binary search on the
// hashed values. It assumes that the "olson" string is a valid timezone name
// from the same version of the tzdb as it was compiled for. If passed an
// invalid string the behaviour is undefined.
const char *getPosixTZforOlson(const char *olson) {
  static_assert(NumZones > 0, "zones should not be empty");
  auto olsonHash = fnvHash(olson) & mask;
  auto i = &zones[0];
  auto x = &zones[NumZones];

  while (x - i > 1) {
    auto m = i + ((x - i) / 2);
    if (m->hash > olsonHash) {
      x = m;
    } else {
      i = m;
    }
  }
  if (i->hash == olsonHash) {
    static TZoneH temp;
    memcpy_P (&temp, i, sizeof (TZoneH));
    return posix[temp.posix];
  }
  return PSTR("UTC0"); // couldn't find it, use default
}