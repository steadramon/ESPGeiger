/*
  StringUtil.h - Small string / buffer helpers shared across modules.

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

#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Use after every snprintf, it returns the untruncated length
static inline void advance_pos(size_t& pos, int nret, size_t bufsz) {
  if (nret <= 0) return;
  size_t want = (size_t) nret;
  size_t room = (pos < bufsz) ? (bufsz - pos - 1) : 0;  // -1 for NUL
  pos += (want < room) ? want : room;
}

// 2n chars, no NUL
void hex_encode(char* out, const uint8_t* in, size_t n);
// exactly 2n hex chars, anything else is false and out untouched
bool hex_decode(uint8_t* out, const char* in, size_t n);

// no exponent, hex or INF/NAN, keeps newlib strtof out
static inline float parse_f(const char* s, char** endptr = nullptr) {
  const char* p = s;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  else if (*p == '+') { p++; }
  bool seen_digit = false;
  float mag = 0.0f;
  while (*p >= '0' && *p <= '9') {
    mag = mag * 10.0f + (*p - '0');
    p++; seen_digit = true;
  }
  if (*p == '.') {
    p++;
    uint32_t frac_int = 0, frac_div = 1;
    uint8_t fdigits = 0;
    while (*p >= '0' && *p <= '9') {
      if (fdigits < 9) {
        frac_int = frac_int * 10 + (*p - '0');
        frac_div *= 10;
        fdigits++;
      }
      p++; seen_digit = true;
    }
    if (frac_div > 1) mag += (float)frac_int / (float)frac_div;
  }
  if (!seen_digit) {
    if (endptr) *endptr = (char*)s;
    return 0.0f;
  }
  if (endptr) *endptr = (char*)p;
  return neg ? -mag : mag;
}

// false leaves *out alone. end null = whole string
bool parse_i32(const char* s, int32_t* out, const char** end = nullptr);

// In place, fields point into buf. Missing lines are ""
uint8_t split_lines(char* buf, char** fields, uint8_t n);

// strict "HH:MM", keep the digit check
struct ParsedTime {
  int hour;
  int minute;
  bool isValid;
};

inline ParsedTime parseTime(const char* timeStr) {
  ParsedTime pt = { -1, -1, false };
  if (!timeStr || strlen(timeStr) != 5 || timeStr[2] != ':') return pt;
  for (uint8_t i = 0; i < 5; i++) {
    if (i == 2) continue;
    if (timeStr[i] < '0' || timeStr[i] > '9') return pt;
  }
  int hv = (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
  int mv = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');
  if (hv <= 23 && mv <= 59) {
    pt.hour = hv; pt.minute = mv; pt.isValid = true;
  }
  return pt;
}

#endif
