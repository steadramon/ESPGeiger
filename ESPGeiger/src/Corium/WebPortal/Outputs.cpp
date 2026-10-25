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
#include "Corium/Module/EGModuleRegistry.h"

size_t Outputs::json(char* buf, size_t cap, unsigned long now) {
  size_t pos = 0;
  bool first = true;
  for (uint8_t i = 0; i < EGModuleRegistry::count(); i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (cap <= pos + 2) break;
    size_t before = pos;
    if (!first) buf[pos++] = ',';
    size_t n;
    if (EGModuleRegistry::quarantined(m)) {
      n = snprintf_P(buf + pos, cap - pos, PSTR("\"%s\":{\"quarantined\":true}"), m->name());
    } else {
      n = m->status_json(buf + pos, cap - pos, now);
    }
    if (n == 0) {
      pos = before;
    } else if (n >= cap - pos) {
      // truncated, drop it
      pos = before;
      break;
    } else {
      pos += n;
      first = false;
    }
  }
  return pos;
}
