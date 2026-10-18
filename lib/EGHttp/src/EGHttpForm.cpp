/*
  EGHttpForm.cpp - application/x-www-form-urlencoded lookup.

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
#include "EGHttpForm.h"
#include "RespParse.h"

#include <string.h>
#include <pgmspace.h>

namespace {

inline unsigned char next_byte(const char*& p, const char* end) {
  if (*p == '+') { p++; return ' '; }
  if (*p == '%' && end - p >= 3) {
    int hi = eg_http_hex(p[1]), lo = eg_http_hex(p[2]);
    if (hi >= 0 && lo >= 0) { p += 3; return (unsigned char)(hi << 4 | lo); }
  }
  return (unsigned char)*p++;
}

inline bool name_matches(const char* p, const char* name_end, const char* name) {
  const char* q = p;
  const char* t = name;
  while (q < name_end && pgm_read_byte(t) && next_byte(q, name_end) == pgm_read_byte(t)) t++;
  return q == name_end && pgm_read_byte(t) == '\0';
}

inline bool decode_value(const char* q, const char* seg_end, char* out, size_t cap) {
  size_t o = 0;
  while (q < seg_end) {
    unsigned char c = next_byte(q, seg_end);
    if (c == 0 || o + 1 >= cap) return false;
    out[o++] = (char)c;
  }
  out[o] = '\0';
  return true;
}

}  // namespace

bool eghttp_form_find(const char* region, size_t len, const char* name,
                      char* out, size_t cap) {
  if (!region || !name || !out || cap == 0) return false;
  const char* p   = region;
  const char* end = region + len;
  while (p < end) {
    const char* amp = (const char*)memchr(p, '&', (size_t)(end - p));
    const char* seg_end = amp ? amp : end;
    const char* eq = (const char*)memchr(p, '=', (size_t)(seg_end - p));
    const char* name_end = eq ? eq : seg_end;

    if (name_matches(p, name_end, name) && seg_end != p)
      return decode_value(eq ? eq + 1 : seg_end, seg_end, out, cap);
    p = amp ? amp + 1 : end;
  }
  return false;
}
