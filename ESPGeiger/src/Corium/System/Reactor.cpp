/*
  Reactor.cpp - the ms and 1 s ticks: a Ticker each, the second due in the loop on esp32.

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
#include "Reactor.h"
#include <EGClock.h>
#include <Ticker.h>
#include "Corium/Net/Wifi.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/Uptime.h"
#ifdef ESP32
#include "Corium/System/DeviceInfo.h"
#endif

namespace corium { volatile uint32_t _fast_ms_counter = 0; }
uint32_t corium::Reactor::s_ms_last_us = 0;
uint32_t corium::Reactor::s_ms_acc_us = 0;

static Ticker s_ms_ticker;
static void (*s_app)(uint32_t) = nullptr;
static bool s_lived = false;
static bool s_day = false;

void corium::Reactor::clock(void (*ms)()) {
  // else first tick sees all of uptime
  s_ms_last_us = micros();
  corium::_fast_ms_counter = s_ms_last_us / 1000;
  EGClock::set(fastMillis);
  s_ms_ticker.attach_ms(1, ms);
}

void corium::Reactor::second() {
  corium::TickProfile::beginTick();
  if (!s_day && corium::fastMillis() > corium::BootGuard::DAY_MS) {
    corium::BootGuard::markDay();
    s_day = true;
  }
  uint32_t up = (uint32_t)corium::Uptime::tick();
  if (s_app) s_app(up);
#ifdef TICK_PROFILE
  corium::TickProfile::markApp();
#endif

#ifdef ESP32
  corium::DeviceInfo::chipTempSample();
#endif

  // wifi off counts as up, else safe mode
  if (!s_lived && corium::Uptime::since(corium::Uptime::start) >= corium::BootGuard::LIVED_S &&
      (corium::Wifi::stableFor(corium::BootGuard::STABLE_MS) || corium::Wifi::disabled)) {
    corium::BootGuard::markLived();
    s_lived = true;
  }
#ifdef TICK_PROFILE
  corium::TickProfile::markWifi();
#endif

  if (corium::TickProfile::endTick()) corium::Registry::logActivityAndReset();
}

#ifdef ESP32
uint32_t corium::Reactor::s_next_sec = 0;
#else
static Ticker s_sec_ticker;
#endif

void corium::Reactor::begin(void (*app)(uint32_t up)) {
  s_app = app;
  corium::Uptime::start = corium::Uptime::tick() + 1;
#ifdef ESP32
  s_next_sec = corium::fastMillis() + 1000;
  // the tick in loop feeds it
  esp_task_wdt_add(NULL);
#else
  s_sec_ticker.attach(1, second);
#endif
}
