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

// First value for name, decoded as the WHATWG urlencoded parser and CPython
// parse_qsl(keep_blank_values=True) do. False when absent, when the value
// does not fit in cap, or when it decodes to a NUL byte.
bool eghttp_form_find(const char* region, size_t len, const char* name,
                      char* out, size_t cap);
