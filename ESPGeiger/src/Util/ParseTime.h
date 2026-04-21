/*
  ParseTime.h - Simple HH:MM time string parser.

  Copyright (C) 2025 @steadramon

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
#ifndef PARSETIME_H
#define PARSETIME_H

#include <Arduino.h>

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
