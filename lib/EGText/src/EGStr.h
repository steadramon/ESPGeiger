/*
  EGStr.h - String compare where either side may be PROGMEM.

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

#include <stdint.h>
#include <pgmspace.h>

// SRAM or PROGMEM on both sides, strcmp_P only takes b in flash
inline bool streq_PP(PGM_P a, PGM_P b) {
  if (a == b) return true;
  for (;; a++, b++) {
    uint8_t c = pgm_read_byte(a);
    if (c != pgm_read_byte(b)) return false;
    if (!c) return true;
  }
}
