/*
  EGHttpForm.h - application/x-www-form-urlencoded lookup.

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
#pragma once

#include <stddef.h>

// First value for name, form-decoded (WHATWG), blank values kept
// false when absent, too long for cap or decodes to a NUL; name may be PROGMEM
bool eghttp_form_find(const char* region, size_t len, const char* name,
                      char* out, size_t cap);

// Same, decoded in place for big values; region needs len + 1 bytes
// nullptr when absent or on a NUL byte, else points into region
char* eghttp_form_take(char* region, size_t len, const char* name, size_t* out_len);

// True when every value decodes within cap, so find() never reads a long or NUL one as absent
bool eghttp_form_fits(const char* region, size_t len, size_t cap);
