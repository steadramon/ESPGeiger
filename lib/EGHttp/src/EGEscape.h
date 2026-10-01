/*
  EGEscape.h - Escape untrusted text for JSON, HTML, URLs and Prometheus.

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

// snprintf semantics: returns the full escaped length; output is complete
// only when that is < cap. A short output never splits a sequence.
// Worst case growth: json 6x, html 6x, url 3x, prom 3x.
size_t egesc_json(char* out, size_t cap, const char* in);  // json.dumps body
size_t egesc_html(char* out, size_t cap, const char* in);  // html.escape
size_t egesc_url (char* out, size_t cap, const char* in);  // quote(safe='')
size_t egesc_prom(char* out, size_t cap, const char* in);  // label value
