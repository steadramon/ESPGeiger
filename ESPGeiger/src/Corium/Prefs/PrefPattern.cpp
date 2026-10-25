/*
  PrefPattern.cpp - The part of an HTML pattern the device checks itself.

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
#include "PrefPattern.h"
#include <Arduino.h>

namespace {

inline char pat(const char* p, size_t i) { return (char)pgm_read_byte(p + i); }

bool in_class(const char* p, size_t from, size_t to, char c) {
  for (size_t i = from; i < to; i++) {
    char lo = pat(p, i);
    if (lo == '\\' && i + 1 < to) lo = pat(p, ++i);
    char hi = lo;
    if (i + 2 < to && pat(p, i + 1) == '-') {
      i += 2;
      hi = pat(p, i);
      if (hi == '\\' && i + 1 < to) hi = pat(p, ++i);
    }
    if (c >= lo && c <= hi) return true;
  }
  return false;
}

// index of the ], or of the NUL if there isn't one
size_t class_end(const char* p) {
  size_t i = 1;
  for (; pat(p, i) && pat(p, i) != ']'; i++) {
    if (pat(p, i) == '\\' && pat(p, i + 1)) i++;
  }
  return i;
}

unsigned read_num(const char* p, size_t* q) {
  unsigned n = 0;
  for (; pat(p, *q) >= '0' && pat(p, *q) <= '9'; (*q)++) n = n * 10 + (pat(p, *q) - '0');
  return n;
}

// + * {m} {m,} {m,n} or nothing, then the end
bool parse_count(const char* p, size_t q, unsigned* min, unsigned* max) {
  char c = pat(p, q);
  *min = *max = 1;
  if (c == '+' || c == '*') {
    *min = c == '+';
    *max = ~0u;
    q++;
  } else if (c == '{') {
    q++;
    *min = *max = read_num(p, &q);
    if (pat(p, q) == ',') {
      q++;
      *max = pat(p, q) == '}' ? ~0u : read_num(p, &q);
    }
    if (pat(p, q) != '}') return false;
    q++;
  }
  return pat(p, q) == '\0';
}

bool literal(char c) {
  switch (c) {
    case '\0': case '^': case '$': case '.': case '|': case '(': case ')': case '*':
    case '+': case '?': case '{': case '}': case '[': case ']': case '\\':
      return false;
  }
  return true;
}

}  // namespace

// no backtracking: x? takes x when it's there
int pref_pattern(const char* p, const char* value) {
  if (!p) return 1;
  const char* v = value;
  bool ok = true;
  size_t i = 0;
  for (; literal(pat(p, i)); i++) {
    char c = pat(p, i);
    bool opt = pat(p, i + 1) == '?';
    if (opt) i++;
    if (ok && *v == c) v++;
    else if (!opt) ok = false;
  }
  p += i;

  bool digits = pat(p, 0) == '\\' && pat(p, 1) == 'd';
  size_t to = 0, q = 2;
  if (!digits) {
    if (pat(p, 0) != '[') return -1;
    to = class_end(p);
    if (pat(p, to) != ']') return -1;
    q = to + 1;
  }
  unsigned min, max;
  if (!parse_count(p, q, &min, &max)) return -1;
  if (!value[0]) return 1;
  if (!ok) return 0;

  unsigned n = 0;
  for (; *v; v++, n++) {
    bool hit = digits ? (*v >= '0' && *v <= '9') : in_class(p, 1, to, *v);
    if (!hit) return 0;
  }
  return n >= min && n <= max;
}
