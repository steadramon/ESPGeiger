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
#ifndef CORIUM_SENDSLOT_H
#define CORIUM_SENDSLOT_H

#include <stdint.h>
#include "App/AppConfig.h"

// first second a send may take, the app keeps the ones before
#ifndef CORIUM_SENDSLOT_FIRST
#define CORIUM_SENDSLOT_FIRST 0
#endif

namespace corium {
namespace SendSlot {
  uint32_t initialOffset(const char* mod_name, uint32_t interval_ms);
  // wall clock once ntp synced
  unsigned long initialPing(const char* mod_name, unsigned long now, uint32_t interval_ms);
  unsigned long pingFromOffset(uint32_t offset, unsigned long now, uint32_t interval_ms);
  inline uint32_t untilPhase(uint32_t target, uint32_t cur, uint32_t period) {
    return target >= cur ? target - cur : period - cur + target;
  }
}
}  // namespace corium

#endif
