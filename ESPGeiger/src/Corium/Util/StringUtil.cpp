/*
  StringUtil.cpp - Small string / buffer helpers shared across modules.

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

#include "StringUtil.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool parse_i32(const char* s, int32_t* out, const char** end) {
  if (!s || !*s) return false;
  char* stop;
  errno = 0;
  long v = strtol(s, &stop, 10);
  if (stop == s || errno == ERANGE) return false;
#if LONG_MAX > INT32_MAX
  if (v < INT32_MIN || v > INT32_MAX) return false;
#endif
  if (end) *end = stop;
  else if (*stop) return false;
  *out = (int32_t)v;
  return true;
}

void hex_encode(char* out, const uint8_t* in, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t hi = in[i] >> 4, lo = in[i] & 0xF;
    out[2 * i]     = (char)(hi < 10 ? '0' + hi : 'a' - 10 + hi);
    out[2 * i + 1] = (char)(lo < 10 ? '0' + lo : 'a' - 10 + lo);
  }
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hex_decode(uint8_t* out, const char* in, size_t n) {
  // stops at the NUL, it isn't hex
  for (size_t i = 0; i < 2 * n; i++) {
    if (hex_val(in[i]) < 0) return false;
  }
  if (in[2 * n]) return false;
  for (size_t i = 0; i < n; i++) {
    out[i] = (uint8_t)(hex_val(in[2 * i]) << 4 | hex_val(in[2 * i + 1]));
  }
  return true;
}

static void strip_cr(char* s) {
  size_t l = strlen(s);
  if (l > 0 && s[l - 1] == '\r') s[l - 1] = '\0';
}

uint8_t split_lines(char* buf, char** fields, uint8_t n) {
  uint8_t got = 0;
  char* line = buf;
  for (char* p = buf; *p; p++) {
    if (*p != '\n') continue;
    *p = '\0';
    if (got < n) fields[got++] = line;
    line = p + 1;
  }
  if (*line && got < n) fields[got++] = line;   // no \n at the end
  for (uint8_t i = 0; i < got; i++) strip_cr(fields[i]);
  for (uint8_t i = got; i < n; i++) fields[i] = (char*)"";
  return got;
}
