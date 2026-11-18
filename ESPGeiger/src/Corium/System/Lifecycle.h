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

#ifndef CORIUM_LIFECYCLE_H
#define CORIUM_LIFECYCLE_H

#include <stdint.h>
#include "App/AppConfig.h"

#ifndef CORIUM_LIFECYCLE_MAX_HANDLERS
#define CORIUM_LIFECYCLE_MAX_HANDLERS 8
#endif

namespace corium {
namespace Lifecycle {

enum Event : uint8_t {
  SHUTDOWN,     // finish writes and stop
  OTA_START,
  OTA_FAILED,
};

typedef void (*Handler)(Event);

// Loop task only, handlers run inside send()
bool on(Handler h);   // false = full
void send(Event e);

// WebPortal::poll does the restart, never mid write. No portal means no restart
void requestRestart(uint32_t now, uint32_t delay_ms);
void retryAfter(uint32_t now, uint32_t delay_ms);   // safe mode's retry, waits until an update is done
bool restartDue(uint32_t now);
bool restartPending();

}

extern bool otaInProgress;
}  // namespace corium

#endif
