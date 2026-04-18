/*
  ParseTime.h - Simple HH:MM time string parser.

  Copyright (C) 2025 @steadramon
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
