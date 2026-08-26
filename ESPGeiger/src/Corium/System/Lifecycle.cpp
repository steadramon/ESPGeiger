/*
  Lifecycle.cpp - see header.

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
#include "Lifecycle.h"

volatile bool ota_in_progress = false;

namespace Lifecycle {

static constexpr uint8_t MAX_HANDLERS = 6;
static Handler s_handlers[MAX_HANDLERS];
static uint8_t s_count = 0;

bool on(Handler h) {
  if (!h || s_count >= MAX_HANDLERS) return false;
  s_handlers[s_count++] = h;
  return true;
}

void send(Event e) {
  for (uint8_t i = 0; i < s_count; i++) s_handlers[i](e);
}

static volatile uint32_t s_restart_at = 0;   // 0 = none

void request_restart(uint32_t now, uint32_t delay_ms) {
  uint32_t at = now + delay_ms;
  s_restart_at = at ? at : 1;
}

bool restart_due(uint32_t now) {
  uint32_t at = s_restart_at;
  return at != 0 && (int32_t)(now - at) >= 0;
}

}
