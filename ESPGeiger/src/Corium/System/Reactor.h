/*
  Reactor.h - the loop pass: hot work every pass, the rest once a ms.

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
#ifndef REACTOR_H
#define REACTOR_H

#include <Arduino.h>
#include <EGTcp.h>
#ifdef ESP32
#include "esp_task_wdt.h"
#endif
#include "Corium/System/FastMillis.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/TickProfile.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/WebPortal/WebPortal.h"

// hot(now) runs every pass, second() once a second
namespace Reactor {

#ifdef ESP32
  // loop runs the tick like esp8266, due on the ms edge
  extern uint32_t s_next_sec;
#endif

  // end of setup
  void begin(void (*second)());

  template <void (*hot)(unsigned long), void (*second)()>
  inline void pass(WebPortal& portal) {
#ifdef LOOP_PROFILE
    uint32_t _body_t0 = micros();
#endif
    TickProfile::countIter();
    unsigned long now = fast_millis();
    hot(now);
    // the rest runs on ms, once a ms is plenty
    static unsigned long s_ms;
    if (now != s_ms) {
      s_ms = now;
#ifdef ESP32
      // one a ms, a stall catches up
      if (reached(now, s_next_sec)) {
        s_next_sec += 1000;
        second();
        esp_task_wdt_reset();
      }
#endif
      // tcp callbacks, ota needs them too
      EGTcp::poll();
      if (!ota_in_progress && reached(now, portal.next_tick_due())) portal.tick(now);
      if (EGModuleRegistry::due(now)) EGModuleRegistry::loop_all(now);
    }
#ifdef LOOP_PROFILE
    TickProfile::addLoopBody((uint32_t)(micros() - _body_t0));
#endif
  }

  template <void (*hot)(unsigned long), void (*second)()>
  inline void run(WebPortal& portal) {
#if defined(ESP32) && !CONFIG_FREERTOS_UNICORE
    // never returns
    for (;;) pass<hot, second>(portal);
#else
    pass<hot, second>(portal);
#endif
  }

}

#endif
