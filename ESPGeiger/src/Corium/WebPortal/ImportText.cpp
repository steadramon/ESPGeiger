/*
  ImportText.cpp - plain text config import, module.key=value per line.

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

#include "ImportText.h"
#include <string.h>
#include "Corium/Prefs/EGPrefs.h"

// too long = false
template <size_t N>
static bool take(char (&out)[N], const char* p, const char* e) {
  size_t n = e - p;
  if (n >= N) return false;
  memcpy(out, p, n);
  out[n] = '\0';
  return true;
}

// value may hold '=' and '.'
static bool put_line(const char* p, const char* e, ImportText::PutFn put) {
  if (memchr(p, '\0', e - p)) return false;   // a NUL would cut the value
  const char* eq = (const char*)memchr(p, '=', e - p);
  if (!eq) return false;
  const char* dot = (const char*)memchr(p, '.', eq - p);
  if (!dot || dot == p || dot + 1 == eq) return false;
  char mid[EGPREF_ID_MAX + 1];
  char key[EGPREF_ID_MAX + 1];
  char value[EGPREFS_MAX_VALUE + 1];
  if (!take(mid, p, dot) || !take(key, dot + 1, eq) || !take(value, eq + 1, e)) return false;
  return put(mid, key, value);
}

static void one_line(const char* p, const char* e, ImportText::PutFn put, ImportText::Result& r,
                     int line) {
  while (p < e && (*p == ' ' || *p == '\t')) p++;
  if (e > p && e[-1] == '\r') e--;
  if (p == e || *p == '#') return;
  if (put_line(p, e, put)) {
    r.applied++;
    return;
  }
  if (!r.skipped) r.first_skip = line;
  r.skipped++;
}

bool ImportText::is_blob(const char* text, size_t len) {
  return len >= 4 && memcmp_P(text, PSTR("ESPG"), 4) == 0;
}

ImportText::Result ImportText::apply(const char* text, size_t len, PutFn put) {
  Result r = {0, 0, 0};
  const char* end = text + len;
  int line = 0;
  while (text < end) {
    const char* nl = (const char*)memchr(text, '\n', end - text);
    const char* e = nl ? nl : end;
    one_line(text, e, put, r, ++line);
    text = nl ? nl + 1 : end;
  }
  return r;
}

const char* ImportText::blob_error(const uint8_t* p, size_t len) {
  if (len < 1) return PSTR("too short");
  size_t off = 0;
  uint8_t gcount = p[off++];
  for (uint8_t gi = 0; gi < gcount; gi++) {
    if (off + 1 > len) return PSTR("truncated group hdr");
    uint8_t mid_len = p[off++];
    if (off + mid_len + 1 > len) return PSTR("bad group hdr");
    off += mid_len;
    uint8_t pcount = p[off++];
    for (uint8_t pi = 0; pi < pcount; pi++) {
      if (off + 3 > len) return PSTR("truncated pref");
      uint8_t klen = p[off++];
      if (off + klen + 2 > len) return PSTR("bad pref key");
      off += klen;
      uint16_t vlen = ((uint16_t)p[off] << 8) | p[off + 1];
      off += 2;
      if (off + vlen > len) return PSTR("truncated pref value");
      off += vlen;
    }
  }
  return nullptr;
}

// too long = "", matches no pref
static void take_id(char (&out)[EGPREF_ID_MAX + 1], const uint8_t* p, uint8_t n) {
  if (n > EGPREF_ID_MAX) n = 0;
  memcpy(out, p, n);
  out[n] = '\0';
}

// -1 = not moved
static int moved_index(const char* mid) {
  for (size_t i = 0; EGPrefs::moved_from(i); i++) {
    if (strcmp(EGPrefs::moved_from(i), mid) == 0) return (int)i;
  }
  return -1;
}

// only the groups whose moved_index is pass
static int apply_pass(const uint8_t* p, int pass, ImportText::PutFn put) {
  size_t off = 0;
  uint8_t gcount = p[off++];
  int applied = 0;
  for (uint8_t gi = 0; gi < gcount; gi++) {
    uint8_t mid_len = p[off++];
    char mid[EGPREF_ID_MAX + 1];
    take_id(mid, p + off, mid_len); off += mid_len;
    bool mine = moved_index(mid) == pass;
    uint8_t pcount = p[off++];
    for (uint8_t pi = 0; pi < pcount; pi++) {
      uint8_t klen = p[off++];
      char key[EGPREF_ID_MAX + 1];
      take_id(key, p + off, klen); off += klen;
      uint16_t vlen = ((uint16_t)p[off] << 8) | p[off + 1];
      off += 2;
      char value[EGPREFS_MAX_VALUE + 1];
      if (!mine || vlen >= sizeof(value)) { off += vlen; continue; }
      memcpy(value, p + off, vlen); off += vlen;
      value[vlen] = '\0';
      if (put(mid, key, value)) applied++;
    }
  }
  return applied;
}

// moved groups last listed first, then the rest, as at boot
int ImportText::apply_blob(const uint8_t* p, size_t len, PutFn put) {
  if (blob_error(p, len)) return -1;
  int n = 0;
  while (EGPrefs::moved_from(n)) n++;
  int applied = 0;
  for (int pass = n - 1; pass >= -1; pass--) applied += apply_pass(p, pass, put);
  return applied;
}
