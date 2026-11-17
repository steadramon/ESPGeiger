/*
  Health.h - The tube and its input in one word, 0 = fine.

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
#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include "Corium/System/StatusWord.h"

// WebAPI sends the raw number, so only ever add bits. Counter sets 0-3, HV 4-6
enum : uint8_t { H_SILENT, H_STORM, H_CHATTER, H_NO_INPUT, H_HV_LOW, H_HV_HIGH, H_HV_FB };
static const uint16_t H_TUBE_MASK = 0x00FF;   // 8-15 are the device's
static const uint16_t H_COUNTER = 0x000F;
static const uint16_t H_HV      = 0x0070;

inline corium::StatusWord& health() { return corium::healthWord(); }

// Text like "ok" or "chatter,hv_low", lives in CounterMeasures.cpp
size_t health_text(char* buf, size_t cap, uint16_t bits);

#endif
