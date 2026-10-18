/*
  EGHttpHeaders.cpp - see header.

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

#include "EGHttpHeaders.h"
#include <string.h>
#include <strings.h>
#include <pgmspace.h>

const char* eghttp_find_header(const char* hdr, size_t headerEnd, const char* name) {
  size_t n = strlen_P(name);
  for (size_t i = 1; i + n + 1 < headerEnd; i++) {
    if (hdr[i - 1] != '\n') continue;
    if (strncasecmp_P(hdr + i, name, n) == 0) return hdr + i + n;
  }
  return nullptr;
}

static const char* skip_spaces(const char* p, const char* end) {
  while (p < end && *p == ' ') p++;
  return p;
}

// max 8 digits, junk is BAD
EGHttpCLResult eghttp_content_length(const char* hdr, size_t headerEnd, size_t* out) {
  *out = 0;
  const char* p = eghttp_find_header(hdr, headerEnd, PSTR("Content-Length:"));
  if (!p) return EGHTTP_CL_ABSENT;
  const char* end = hdr + headerEnd;
  p = skip_spaces(p, end);
  size_t v = 0, nd = 0;
  while (p < end && *p >= '0' && *p <= '9' && nd < 9) {
    v = v * 10 + (size_t)(*p - '0');
    p++; nd++;
  }
  p = skip_spaces(p, end);
  if (nd == 0 || nd > 8 || p >= end || *p != '\r') return EGHTTP_CL_BAD;
  *out = v;
  return EGHTTP_CL_OK;
}
