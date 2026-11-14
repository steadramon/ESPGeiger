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

// 17 bit hash, then rule number, no names
const uint32_t HASH_MASK = 0x1ffff;
const uint32_t RULE_BITS = 7;
const uint32_t NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u ^ TZ_SEED;
  while (*s) {
    h ^= (uint32_t)(uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

const char *nthRule(uint8_t n) {
  const char *p = TZ_RULES;
  while (n--) {
    while (pgm_read_byte(p)) p++;
    p++;
  }
  return p;
}

}  // namespace

const char *posixFor(const char *olson) {
  if (!olson) return nullptr;
  uint32_t want = fnv1a(olson) & HASH_MASK;
  uint32_t lo = 0, hi = NUM_ZONES;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    uint32_t row = (uint32_t)pgm_read_byte(&zones[mid][0]) << 16 |
                   (uint32_t)pgm_read_byte(&zones[mid][1]) << 8 |
                   pgm_read_byte(&zones[mid][2]);
    uint32_t hash = row >> RULE_BITS;
    if (hash == want) return nthRule(row & ((1 << RULE_BITS) - 1));
    if (hash < want) lo = mid + 1;
    else             hi = mid;
  }
  return nullptr;
}

static bool ruleChar(char c) {
  return isalnum((uint8_t)c) || c == '+' || c == '-' || c == ',' || c == '.' ||
         c == ':' || c == '/' || c == '<' || c == '>';
}

const char *rule(const char *tz) {
  const char *p = posixFor(tz);
  if (p || !tz) return p;
  bool digit = false;
  for (const char *c = tz; *c; c++) {
    if (!ruleChar(*c)) return nullptr;
    if (isdigit((uint8_t)*c)) digit = true;
  }
  return digit ? tz : nullptr;
}

}  // namespace EGTimeZone
