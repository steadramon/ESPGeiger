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

EGHttpCLResult eghttp_content_length(const char* hdr, size_t headerEnd, size_t* out) {
  *out = 0;
  for (size_t i = 0; i + 16 < headerEnd; i++) {
    if (strncasecmp(hdr + i, "Content-Length:", 15) != 0) continue;
    const char* p = hdr + i + 15;
    const char* end = hdr + headerEnd;
    while (p < end && *p == ' ') p++;
    size_t v = 0, nd = 0;
    while (p < end && *p >= '0' && *p <= '9' && nd < 9) {
      v = v * 10 + (size_t)(*p - '0');
      p++; nd++;
    }
    while (p < end && *p == ' ') p++;
    if (nd == 0 || nd > 8 || p >= end || *p != '\r') return EGHTTP_CL_BAD;
    *out = v;
    return EGHTTP_CL_OK;
  }
  return EGHTTP_CL_ABSENT;
}
