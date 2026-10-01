/*
  EGEscape.cpp - Escape untrusted text for the context it is written into.

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
#include "EGEscape.h"

#include <pgmspace.h>

// Matches CPython json.dumps, html.escape, urllib.parse.quote and the
// prometheus/common label escaper. Invalid UTF-8 becomes U+FFFD per the
// WHATWG UTF-8 decoder. Tables are PROGMEM: .rodata is DRAM on ESP8266.

namespace {

enum Mode { JSON, HTML, URL, PROM };

// WHATWG UTF-8 decoder over one sequence. On error the failing byte is not
// consumed. NUL is never a valid trail, so it stops at the terminator.
unsigned utf8_next(const unsigned char* s, bool* bad) {
  unsigned char c = s[0];
  unsigned need;
  unsigned char lower = 0x80, upper = 0xBF;
  *bad = true;
  if (c < 0x80) { *bad = false; return 1; }
  if (c >= 0xC2 && c <= 0xDF) {
    need = 1;
  } else if (c >= 0xE0 && c <= 0xEF) {
    if (c == 0xE0) lower = 0xA0;   // overlong
    if (c == 0xED) upper = 0x9F;   // surrogates
    need = 2;
  } else if (c >= 0xF0 && c <= 0xF4) {
    if (c == 0xF0) lower = 0x90;   // overlong
    if (c == 0xF4) upper = 0x8F;   // above U+10FFFF
    need = 3;
  } else {
    return 1;
  }
  for (unsigned i = 1; i <= need; i++) {
    if (s[i] < lower || s[i] > upper) return i;
    lower = 0x80;
    upper = 0xBF;
  }
  *bad = false;
  return need + 1;
}

struct Rep { char c; unsigned char modes; char seq[7]; };

const Rep REPS[] PROGMEM = {
  {'"',  1 << JSON | 1 << PROM, "\\\""},
  {'\\', 1 << JSON | 1 << PROM, "\\\\"},
  {'\n', 1 << JSON | 1 << PROM, "\\n"},
  {'\b', 1 << JSON, "\\b"},
  {'\f', 1 << JSON, "\\f"},
  {'\r', 1 << JSON, "\\r"},
  {'\t', 1 << JSON, "\\t"},
  {'&',  1 << HTML, "&amp;"},
  {'<',  1 << HTML, "&lt;"},
  {'>',  1 << HTML, "&gt;"},
  {'"',  1 << HTML, "&quot;"},
  {'\'', 1 << HTML, "&#x27;"},
};

inline char hex(unsigned v, char a) { return (char)(v < 10 ? '0' + v : a + v - 10); }

unsigned encode_ascii(Mode m, unsigned char c, char* rep) {
  for (const Rep& r : REPS) {
    if (pgm_read_byte(&r.c) != c || !(pgm_read_byte(&r.modes) & (1 << m))) continue;
    unsigned n = 0;
    while (n < 6 && (rep[n] = (char)pgm_read_byte(&r.seq[n]))) n++;
    return n;
  }
  if (m == JSON && c < 0x20) {
    rep[0] = '\\'; rep[1] = 'u'; rep[2] = '0'; rep[3] = '0';
    rep[4] = hex(c >> 4, 'a'); rep[5] = hex(c & 0xF, 'a');
    return 6;
  }
  if (m == URL && !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-' || c == '~')) {
    rep[0] = '%'; rep[1] = hex(c >> 4, 'A'); rep[2] = hex(c & 0xF, 'A');
    return 3;
  }
  rep[0] = (char)c;
  return 1;
}

size_t escape(Mode m, char* out, size_t cap, const char* in) {
  size_t need = 0, pos = 0;
  bool full = (cap == 0);
  const unsigned char* s = (const unsigned char*)in;
  while (s && *s) {
    char rep[6];
    const char* src = rep;
    unsigned n, adv;
    if (*s < 0x80 || m == URL) {
      n = encode_ascii(m, *s, rep);
      adv = 1;
    } else {
      bool bad;
      adv = utf8_next(s, &bad);
      if (bad) {
        rep[0] = (char)0xEF; rep[1] = (char)0xBF; rep[2] = (char)0xBD;  // U+FFFD
        n = 3;
      } else {
        src = (const char*)s;
        n = adv;
      }
    }
    s += adv;
    need += n;
    if (full) continue;
    if (pos + n >= cap) { full = true; continue; }
    for (unsigned i = 0; i < n; i++) out[pos++] = src[i];
  }
  if (cap) out[pos] = '\0';
  return need;
}

}  // namespace

size_t egesc_json(char* out, size_t cap, const char* in) { return escape(JSON, out, cap, in); }
size_t egesc_html(char* out, size_t cap, const char* in) { return escape(HTML, out, cap, in); }
size_t egesc_url (char* out, size_t cap, const char* in) { return escape(URL,  out, cap, in); }
size_t egesc_prom(char* out, size_t cap, const char* in) { return escape(PROM, out, cap, in); }
