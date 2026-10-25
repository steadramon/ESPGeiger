/*
  SendSlot.h - Spreads periodic sends round the minute, per chip.

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
#ifndef EG_SENDSLOT_H
#define EG_SENDSLOT_H

#include <stdint.h>
#include "App/AppConfig.h"

// first second a send may take, the app keeps the ones before
#ifndef SENDSLOT_FIRST
#define SENDSLOT_FIRST 0
#endif

namespace SendSlot {
  uint32_t initial_offset(const char* mod_name, uint32_t interval_ms);
  // wall clock once ntp synced
  unsigned long initial_ping(const char* mod_name, unsigned long now, uint32_t interval_ms);
  unsigned long ping_from_offset(uint32_t offset, unsigned long now, uint32_t interval_ms);
}

#endif
