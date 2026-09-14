/*
  EGHttpHeaders.h - request header recognisers, no String, no AsyncTCP.

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

#ifndef EGHTTP_HEADERS_H
#define EGHTTP_HEADERS_H

#include <stddef.h>

enum EGHttpCLResult : unsigned char {
  EGHTTP_CL_ABSENT = 0,   // no header, length is 0
  EGHTTP_CL_OK,
  EGHTTP_CL_BAD           // sign, junk, empty, or more than 8 digits
};

// Scans [hdr, hdr+headerEnd) for the first Content-Length header.
EGHttpCLResult eghttp_content_length(const char* hdr, size_t headerEnd, size_t* out);

#endif
