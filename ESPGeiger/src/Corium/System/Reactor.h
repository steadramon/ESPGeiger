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
#include "Corium/Output/LedSignal.h"
#include "Corium/WebPortal/WebPortal.h"

// hot(now) runs every pass, second() once a second
namespace Reactor {

#ifdef ESP32
  // loop runs the tick like esp8266, due on the ms edge
  extern uint32_t s_next_sec;
#endif
  extern uint32_t s_ms_last_us;
  extern uint32_t s_ms_acc_us;

  // before wifi, the libs time from it
  void clock(void (*ms)());

  // the ms ticker, extra(now_us) is the app's; 8266 = Ticker (SYS), esp32 = esp_timer task, no log or yield
  template <void (*extra)(uint32_t)>
  void ms() {
    uint32_t now_us = micros();
    s_ms_acc_us += (now_us - s_ms_last_us);
    while (s_ms_acc_us >= 1000) {
      _fast_ms_counter++;
      s_ms_acc_us -= 1000;
    }
    s_ms_last_us = now_us;
    if (LedSignal::due()) LedSignal::poll();
    extra(now_us);
  }

  // end of setup, app(up) each second, nullptr = none; 8266 = Ticker (SYS), esp32 = loop
  void begin(void (*app)(uint32_t up));
  void second();

  template <void (*hot)(unsigned long)>
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
      EGModuleRegistry::in(EGModuleRegistry::IN_TCP);
      EGTcp::poll();
      EGModuleRegistry::in(EGModuleRegistry::IN_PORTAL);
      // web ota needs it too
      if (reached(now, portal.next_tick_due())) portal.tick(now);
      EGModuleRegistry::in(EGModuleRegistry::IN_NONE);
      if (EGModuleRegistry::due(now)) EGModuleRegistry::loop_all(now);
    }
#ifdef LOOP_PROFILE
    TickProfile::addLoopBody((uint32_t)(micros() - _body_t0));
#endif
  }

  // 8266: the core runs preloop_update_frequency() every pass, the app throttles it
  template <void (*hot)(unsigned long)>
  inline void run(WebPortal& portal) {
#if defined(ESP32) && !CONFIG_FREERTOS_UNICORE
    // never returns
    for (;;) pass<hot>(portal);
#else
    pass<hot>(portal);
#endif
  }

}

#endif
