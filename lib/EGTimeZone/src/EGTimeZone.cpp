/*
  EGTimeZone.cpp - Olson name to POSIX TZ rule.

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
#include <Arduino.h>
#include "EGTimeZone.h"
#include "EGTimeZoneTable.h"

namespace EGTimeZone {
namespace {

// Names are not stored: 21 bits of hash, then an offset into TZ_RULES.
const uint32_t HASH_MASK = 0x1fffff;
const uint32_t NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint32_t)(uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

}  // namespace

const char *posixFor(const char *olson) {
  uint32_t want = fnv1a(olson) & HASH_MASK;
  uint32_t lo = 0, hi = NUM_ZONES;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    TZoneH probe;
    memcpy_P(&probe, &zones[mid], sizeof(probe));
    if (probe.hash == want) return TZ_RULES + probe.offset;
    if (probe.hash < want) lo = mid + 1;
    else                   hi = mid;
  }
  return nullptr;
}

}  // namespace EGTimeZone
