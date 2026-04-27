/*
  Schedule.h - Per-boot random offset, module-name biased so outputs don't clash.

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
#ifndef ESPGEIGER_SCHEDULE_H
#define ESPGEIGER_SCHEDULE_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace Schedule {

constexpr uint32_t FNV_PRIME = 0x01000193ul;  // FNV-1a 32-bit prime

inline uint32_t offsetFor(const char* module_name, uint32_t interval_ms) {
  if (!interval_ms) return 0;
  uint32_t h = (uint32_t)random();
  if (module_name) {
    for (const char* p = module_name; *p; ++p) {
      h = (h ^ (uint8_t)*p) * FNV_PRIME;
    }
  }
  return h % interval_ms;
}

}

#endif
