/*
  OutputVars.cpp - Central registry of variables exposed to output channels.

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
#include "OutputVars.h"
#include <string.h>

namespace OutputVars {

// 0 = row's own
struct Spec {
  char    unit;      // temp rows only
  uint8_t decimals;  // float rows only
};

// [C|F|K][1-9]
static bool parse_spec(const char* s, size_t n, Spec* out) {
  size_t i = 0;
  if (i < n && (s[i] == 'C' || s[i] == 'F' || s[i] == 'K')) out->unit = s[i++];
  if (i < n && s[i] >= '1' && s[i] <= '9') out->decimals = (uint8_t)(s[i++] - '0');
  return i > 0 && i == n;
}

static bool lookup(const char* id, size_t n, EGMeasure* row, EGMValue* val) {
  if (n == 0 || n >= sizeof(row->id)) return false;
  for (EGMeasureIter it(EGM_OUT_VARS); it.next(); ) {
    const EGMeasure& m = it.row();
    if (strncmp(m.id, id, n) == 0 && m.id[n] == '\0') {
      *row = m;
      *val = it.value();
      return true;
    }
  }
  return false;
}

static uint8_t unit_code(char unit) {
  return unit == 'F' ? EGMT_F : unit == 'K' ? EGMT_K : EGMT_C;
}

static bool apply(EGMeasure& m, const Spec& s) {
  if (s.unit && m.unit != EGMU_CELSIUS) return false;
  if (s.decimals) {
    if (m.kind != EGM_F32) return false;
    m.decimals = s.decimals;
  }
  return true;
}

size_t render(const char* key, size_t klen, char* buf, size_t cap) {
  if (!key || !buf || cap == 0) return 0;
  const char* colon = (const char*)memchr(key, ':', klen);
  size_t idlen = colon ? (size_t)(colon - key) : klen;
  Spec spec = {0, 0};
  if (colon && !parse_spec(colon + 1, klen - idlen - 1, &spec)) return 0;
  EGMeasure m;
  EGMValue v;
  if (!lookup(key, idlen, &m, &v) || !apply(m, spec)) return 0;
  uint8_t unit = spec.unit ? unit_code(spec.unit) : egm_temp_unit();
  return egm_format_unit(m, v, unit, buf, cap);
}

static bool unescape(char in, char* out) {
  switch (in) {
    case 'n':  *out = '\n'; return true;
    case 'r':  *out = '\r'; return true;
    case 't':  *out = '\t'; return true;
    case '\\': *out = '\\'; return true;
    case '{':  *out = '{';  return true;
    case '}':  *out = '}';  return true;
    case '0':  *out = '\0'; return true;
  }
  return false;
}

// p on '\', false = full. Unknown escapes pass through
static bool put_escape(const char*& p, char* buf, size_t& pos, size_t cap) {
  p++;
  char c = 0;
  if (unescape(*p, &c)) { buf[pos++] = c; p++; return true; }
  if (*p == '\0') { buf[pos++] = '\\'; return true; }
  if (pos + 2 >= cap) return false;
  buf[pos++] = '\\';
  buf[pos++] = *p++;
  return true;
}

// p on '{', false = full
static bool put_var(const char*& p, char* buf, size_t& pos, size_t cap) {
  const char* k = p + 1;
  const char* e = strchr(k, '}');
  if (!e) { buf[pos++] = *p++; return true; }
  size_t klen = (size_t)(e - k);
  size_t n = render(k, klen, buf + pos, cap - pos);
  if (n > 0) {
    pos += n;
  } else {
    // unknown, print {name} as is
    size_t need = klen + 2;
    if (pos + need >= cap) return false;
    buf[pos++] = '{';
    memcpy(buf + pos, k, klen);
    pos += klen;
    buf[pos++] = '}';
  }
  p = e + 1;
  return true;
}

size_t renderTemplate(const char* tpl, char* buf, size_t cap) {
  if (!tpl || !buf || cap == 0) return 0;
  size_t pos = 0;
  const char* p = tpl;
  while (*p) {
    if (pos + 1 >= cap) break;  // leave room for NUL
    bool more = true;
    if (*p == '\\')     more = put_escape(p, buf, pos, cap);
    else if (*p == '{') more = put_var(p, buf, pos, cap);
    else                buf[pos++] = *p++;
    if (!more) break;
  }
  buf[pos] = '\0';
  return pos;
}

}  // namespace OutputVars
