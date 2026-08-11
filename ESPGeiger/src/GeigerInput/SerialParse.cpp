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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace SerialFormat {

// ctype takes an int that must be representable as unsigned char. Every ESP
// toolchain has unsigned char, hosts do not, so cast rather than rely on it.
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

// Digits only, plus CR/LF. Rejects garbage that happens to lead with a digit.
bool parse_gc10(const char* in, int* out_cpm, int* /*out_cps*/) {
  if (!in || !out_cpm) return false;
  bool any = false;
  for (size_t i = 0; in[i]; i++) {
    const char c = in[i];
    if (is_digit(c)) { any = true; continue; }
    if (c != '\r' && c != '\n') return false;
  }
  if (!any) return false;
  int cpm = 0;
  if (sscanf(in, "%d", &cpm) != 1) return false;
  if (cpm < 0 || cpm > SERIAL_MAX_COUNT) return false;
  *out_cpm = cpm;
  return true;
}

bool parse_mightyohm(const char* in, int* out_cpm, int* out_cps) {
  if (!in || !out_cpm) return false;
  int cpm = 0, cps = 0;
  if (sscanf(in, "CPS, %d, CPM, %d", &cps, &cpm) != 2) return false;
  if (cpm < 0 || cpm > SERIAL_MAX_COUNT) return false;
  // cps is bounded on the same terms as cpm. It is added straight to
  // partial_clicks and handed to Counter::on_pulse_batch, so an unbounded
  // value off the wire lands in the counter.
  if (out_cps && cps >= 0 && cps <= SERIAL_MAX_COUNT) *out_cps = cps;
  *out_cpm = cpm;
  return true;
}

// "CPM: 123", "CPM=123", "CPS,4". Returns false when the tag is absent or is
// not followed by a number.
//
// Presence and value are reported separately on purpose. Folding them into a
// negative return conflates "no tag" with "the device said -5", and the
// caller then falls through to its untagged path and reads the 5.
static bool parse_label_value(const char* in, char tag, int* out) {
  for (const char* p = in; p[0] && p[1] && p[2]; p++) {
    if (p[0] == 'C' && p[1] == 'P' && p[2] == tag) {
      p += 3;
      while (*p == ':' || *p == ',' || *p == '=' || *p == ' ' || *p == '\t') p++;
      if (*p == '-' || is_digit(*p)) { *out = atoi(p); return true; }
      return false;
    }
  }
  return false;
}

bool parse_template(const char* in, int* out_cpm, int* out_cps) {
  if (!in || !out_cpm) return false;
  int cpm = 0;
  if (!parse_label_value(in, 'M', &cpm)) {
    // No CPM tag: fall back to the first number on the line. Deliberately
    // loose, because the user template is arbitrary.
    const char* p = in;
    while (*p && !is_digit(*p)) p++;
    if (!*p) return false;
    cpm = atoi(p);
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

}
