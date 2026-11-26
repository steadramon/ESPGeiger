/*
  Outputs.cpp - the /outputs body, each module's status as one JSON object.

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
#include "Outputs.h"
#include "Corium/Module/Registry.h"

size_t corium::Outputs::module(uint8_t i, char* buf, size_t cap) {
  corium::Module* m = corium::Registry::get(i);
  size_t n = corium::Registry::quarantined(m)
      ? snprintf_P(buf, cap, PSTR("\"%s\":{\"quarantined\":true}"), m->name())
      : m->statusJson(buf, cap);
  return n < cap ? n : 0;   // truncated, left out
}

size_t corium::Outputs::json(char* buf, size_t cap) {
  size_t pos = 0;
  for (uint8_t i = 0; i < corium::Registry::count() && pos + 2 < cap; i++) {
    size_t at = pos ? pos + 1 : 0;   // room for the comma
    size_t n = module(i, buf + at, cap - at);
    if (!n) continue;
    if (pos) buf[pos] = ',';
    pos = at + n;
  }
  return pos;
}
