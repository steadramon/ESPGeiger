/*
  FormatFloat.h - Fast integer-scaled float-to-string helper

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef FORMATFLOAT_H
#define FORMATFLOAT_H

#include <Arduino.h>

static inline int format_f(char* buf, size_t bufsz, float v, uint8_t decimals = 2) {
  if (v < 0) v = 0;
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  int32_t s = (int32_t)(v * (float)scale + 0.5f);
  return snprintf(buf, bufsz, "%ld.%0*ld", (long)(s / scale), (int)decimals, (long)(s % scale));
}

#endif
