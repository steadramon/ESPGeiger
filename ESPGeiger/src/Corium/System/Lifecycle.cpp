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
#include "FastMillis.h"

namespace corium {
bool otaInProgress = false;

namespace Lifecycle {

static constexpr uint8_t MAX_HANDLERS = CORIUM_LIFECYCLE_MAX_HANDLERS;
static Handler s_handlers[MAX_HANDLERS];
static uint8_t s_count = 0;

bool on(Handler h) {
  if (!h) return false;
  for (uint8_t i = 0; i < s_count; i++) {
    if (s_handlers[i] == h) return true;   // a second begin()
  }
  if (s_count >= MAX_HANDLERS) return false;
  s_handlers[s_count++] = h;
  return true;
}

// the one writer, web and espota alike
void send(Event e) {
  otaInProgress = e == OTA_START;
  for (uint8_t i = 0; i < s_count; i++) s_handlers[i](e);
}

static Deadline s_restart;

void requestRestart(uint32_t now, uint32_t delay_ms) {
  s_restart.set(now + delay_ms);
}

bool restartDue(uint32_t now) {
  return s_restart.due(now);
}

}
}  // namespace corium
