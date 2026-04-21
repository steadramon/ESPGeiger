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

#include <Arduino.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// advance_pos - safe snprintf-advance.
//
// snprintf returns what it WOULD have written, not what it did. Naively
// doing `pos += n` after a truncated write walks `pos` past the buffer,
// and the next `snprintf(buf + pos, ...)` scribbles over stack frames —
// which has caused garbled pages + reboots. Always use this helper after
// every `pos += snprintf(buf + pos, sizeof(buf) - pos, ...)`-style call.
//
// Usage:
//   int n = snprintf(buf + pos, sizeof(buf) - pos, "...");
//   advance_pos(pos, n, sizeof(buf));
// ---------------------------------------------------------------------------
static inline void advance_pos(size_t& pos, int nret, size_t bufsz) {
  if (nret <= 0) return;
  size_t want = (size_t) nret;
  size_t room = (pos < bufsz) ? (bufsz - pos - 1) : 0;  // -1 for NUL
  pos += (want < room) ? want : room;
}

// ---------------------------------------------------------------------------
// format_f - Fast integer-scaled float-to-string helper.
//
// Avoids Arduino's %f (pulls in soft-float print) by doing fixed-point
// conversion: multiply by 10^decimals, round, print as integer.decimal.
// ---------------------------------------------------------------------------
static inline int format_f(char* buf, size_t bufsz, float v, uint8_t decimals = 2) {
  if (v < 0) v = 0;
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  int32_t s = (int32_t)(v * (float)scale + 0.5f);
  return snprintf(buf, bufsz, "%ld.%0*ld", (long)(s / scale), (int)decimals, (long)(s % scale));
}

// ---------------------------------------------------------------------------
// parseTime - "HH:MM" → {hour, minute, isValid}.
//
// Strict format: exactly 5 chars with `:` at position 2, hour 0-23,
// minute 0-59. Anything else returns isValid=false.
// ---------------------------------------------------------------------------
struct ParsedTime {
  int hour;
  int minute;
  bool isValid;
};

inline ParsedTime parseTime(const char* timeStr) {
  ParsedTime pt = { -1, -1, false };
  if (!timeStr || strlen(timeStr) != 5 || timeStr[2] != ':') return pt;
  char tmp[6];
  strncpy(tmp, timeStr, 5);
  tmp[5] = '\0';
  char* h = strtok(tmp, ":");
  char* m = strtok(NULL, ":");
  if (!h || !m) return pt;
  int hv = atoi(h), mv = atoi(m);
  if (hv >= 0 && hv <= 23 && mv >= 0 && mv <= 59) {
    pt.hour = hv; pt.minute = mv; pt.isValid = true;
  }
  return pt;
}

#endif
