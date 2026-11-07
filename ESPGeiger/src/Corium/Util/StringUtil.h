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

#ifndef CORIUM_STRINGUTIL_H
#define CORIUM_STRINGUTIL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Use after every snprintf, it returns the untruncated length
static inline void advancePos(size_t& pos, int n, size_t cap) {
  if (n <= 0) return;
  size_t want = (size_t) n;
  size_t room = (pos < cap) ? (cap - pos - 1) : 0;  // -1 for NUL
  pos += (want < room) ? want : room;
}

// Copies PROGMEM fp to dst, cut at cap. null fp = ""
const char* flashToRam(const char* fp, char* dst, size_t cap);

// writes 2n chars, no NUL
void hexEncode(char* out, const uint8_t* in, size_t n);
// exactly 2n hex chars, anything else is false and out untouched
bool hexDecode(uint8_t* out, const char* in, size_t n);

// No exponent, hex or INF/NAN, keeps newlib's strtof out of the build
static inline float parseF(const char* s, char** endptr = nullptr) {
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

// false leaves *out alone. A null end means the whole string has to parse
bool parseI32(const char* s, int32_t* out, const char** end = nullptr);
bool parseU64(const char* s, uint64_t* out);   // digits only
bool parseCenti(const char* s, int32_t* out);   // "12.34" -> 1234, no sign

void fmtU64(char* out, size_t cap, uint64_t v);   // no %llu in nano libc

// In place, fields point into buf, \r dropped. Missing lines are ""
uint8_t splitLines(char* buf, char** fields, uint8_t n);

// strict "HH:MM", keep the digit check
struct ParsedTime {
  int hour;
  int minute;
  bool isValid;
};

inline ParsedTime parseTime(const char* s) {
  ParsedTime pt = { -1, -1, false };
  if (!s || strlen(s) != 5 || s[2] != ':') return pt;
  for (uint8_t i = 0; i < 5; i++) {
    if (i == 2) continue;
    if (s[i] < '0' || s[i] > '9') return pt;
  }
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h <= 23 && m <= 59) {
    pt.hour = h; pt.minute = m; pt.isValid = true;
  }
  return pt;
}

#endif
