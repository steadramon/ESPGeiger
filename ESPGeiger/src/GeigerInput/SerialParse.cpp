/*
  GeigerInput/SerialParse.cpp - serial protocol parsers.

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

#include "SerialParse.h"
#include "../Corium/Util/StringUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace SerialFormat {

// cast, char is signed on host
static inline bool is_digit(char c) { return isdigit((unsigned char)c) != 0; }
static inline bool is_printable(char c) {
  const unsigned char u = (unsigned char)c;
  return u >= 0x20 && u <= 0x7E;
}

bool common_validate(const char* in, size_t len) {
  if (!in || len == 0) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = in[i];
    if (!is_printable(c) && c != '\r' && c != '\n') return false;
  }
  return true;
}

// digits and CR/LF only
bool parse_gc10(const char* in, int* out_cpm, int* /*out_cps*/) {
  if (!in || !out_cpm) return false;
  bool any = false;
  for (size_t i = 0; in[i]; i++) {
    const char c = in[i];
    if (is_digit(c)) { any = true; continue; }
    if (c != '\r' && c != '\n') return false;
  }
  if (!any) return false;
  // not sscanf, it wraps
  int32_t cpm;
  const char* end;
  if (!parse_i32(in, &cpm, &end)) return false;
  if (cpm < 0 || cpm > SERIAL_MAX_COUNT) return false;
  *out_cpm = (int)cpm;
  return true;
}

static const char* skip_ws(const char* p) {
  while (isspace((unsigned char)*p)) p++;
  return p;
}

// "CPx,"
static bool cp_tag(const char* p, char c) {
  return p[0] == 'C' && p[1] == 'P' && p[2] == c && p[3] == ',';
}

// as %d, too big is -1. nullptr = no number
static const char* scan_int(const char* p, int32_t* v) {
  const char* end;
  if (parse_i32(p, v, &end)) return end;
  p = skip_ws(p);
  if (*p == '+' || *p == '-') p++;
  if (!is_digit(*p)) return nullptr;
  while (is_digit(*p)) p++;
  *v = -1;
  return p;
}

// as sscanf "CPS, %d, CPM, %d"
bool parse_mightyohm(const char* in, int* out_cpm, int* out_cps) {
  if (!in || !out_cpm) return false;
  int32_t cpm, cps;
  const char* p = in;
  if (!cp_tag(p, 'S') || !(p = scan_int(p + 4, &cps)) || *p != ',') return false;
  p = skip_ws(p + 1);
  if (!cp_tag(p, 'M') || !scan_int(p + 4, &cpm)) return false;
  if (cpm < 0 || cpm > SERIAL_MAX_COUNT) return false;
  // bound cps too, it goes in the counter
  if (out_cps && cps >= 0 && cps <= SERIAL_MAX_COUNT) *out_cps = (int)cps;
  *out_cpm = (int)cpm;
  return true;
}

// "CPM: 123", "CPM=123", "CPS,4"
static bool parse_label_value(const char* in, char tag, int* out) {
  for (const char* p = in; p[0] && p[1] && p[2]; p++) {
    if (p[0] == 'C' && p[1] == 'P' && p[2] == tag) {
      p += 3;
      while (*p == ':' || *p == ',' || *p == '=' || *p == ' ' || *p == '\t') p++;
      if (*p != '-' && !is_digit(*p)) return false;
      // still present, -1 fails the bounds
      int32_t v;
      const char* end;
      *out = parse_i32(p, &v, &end) ? (int)v : -1;
      return true;
    }
  }
  return false;
}

static bool parse_labelled(const char* in, int* out_cpm, int* out_cps, bool loose) {
  if (!in || !out_cpm) return false;
  int cpm = 0;
  if (!parse_label_value(in, 'M', &cpm)) {
    if (!loose) return false;
    // no tag, first number, loose on purpose
    const char* p = in;
    while (*p && !is_digit(*p)) p++;
    int32_t v;
    const char* end;
    if (!*p || !parse_i32(p, &v, &end)) return false;
    cpm = (int)v;
  }
  if (cpm < 0 || cpm > SERIAL_MAX_COUNT) return false;
  *out_cpm = cpm;
  if (out_cps) {
    int cps = 0;
    if (parse_label_value(in, 'S', &cps) && cps >= 0 && cps <= SERIAL_MAX_COUNT) {
      *out_cps = cps;
    }
  }
  return true;
}

// needs the label, boot logs share the uart
bool parse_espgeiger(const char* in, int* out_cpm, int* out_cps) {
  return parse_labelled(in, out_cpm, out_cps, false);
}

bool parse_template(const char* in, int* out_cpm, int* out_cps) {
  return parse_labelled(in, out_cpm, out_cps, true);
}

}
