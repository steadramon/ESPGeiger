/*
  QuietWindow.h - A daily quiet window that may cross midnight.

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

#ifndef UTIL_QUIETWINDOW_H
#define UTIL_QUIETWINDOW_H

#include <stdint.h>
#include "../Util/StringUtil.h"

// -1 = blank or invalid
inline int16_t quiet_minute(const char* hhmm) {
  ParsedTime t = parseTime(hhmm);
  return t.isValid ? (int16_t)(t.hour * 60 + t.minute) : -1;
}

// wraps midnight, from == to is off
inline bool quiet_in_window(int16_t m, int16_t from, int16_t to) {
  if (from < 0 || to < 0 || from == to) return false;
  return from < to ? (m >= from && m < to) : (m >= from || m < to);
}

class QuietWindow {
public:
  void set(const char* from, const char* to);
  // never before NTP sync
  bool active();
private:
  int16_t _from = -1;
  int16_t _to = -1;
  bool _cached = false;
  unsigned long _recompute_ms = 0;
};

#endif
