/*
  Lifecycle.h - restart and OTA events for whatever has to stop or show it

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

#ifndef LIFECYCLE_H
#define LIFECYCLE_H

#include <stdint.h>
#include "App/AppConfig.h"

#ifndef LIFECYCLE_MAX_HANDLERS
#define LIFECYCLE_MAX_HANDLERS 8
#endif

namespace Lifecycle {

enum Event : uint8_t {
  SHUTDOWN,     // finish writes and stop
  OTA_START,
  OTA_FAILED,
};

typedef void (*Handler)(Event);

// loop task only, handlers stay till restart and run inside send
bool on(Handler h);   // false = full
void send(Event e);

// WebPortal::tick does the restart, never mid-write; no portal, no restart
void request_restart(uint32_t now, uint32_t delay_ms);
bool restart_due(uint32_t now);

}

extern bool ota_in_progress;

#endif
