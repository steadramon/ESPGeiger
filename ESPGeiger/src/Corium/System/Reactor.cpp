/*
  Reactor.cpp - ms and 1 s ticks

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
#ifdef ESP32
#include <Ticker.h>
#else
#include <osapi.h>
extern "C" void* umm_last_fail_alloc_addr;   // the core's
#endif
#include "Corium/Net/Wifi.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/Clock.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/StatusWord.h"
#include "Corium/Module/Registry.h"

namespace corium { volatile uint32_t _fast_ms_counter = 0; }
uint32_t corium::Reactor::s_ms_last_us = 0;
uint32_t corium::Reactor::s_ms_acc_us = 0;
uint32_t corium::Reactor::s_next_house = 0;

#ifdef ESP32
static Ticker s_ms_ticker;
#else
// os_timer straight, Ticker drags in std::function (~1 KB)
static os_timer_t s_ms_timer;
static os_timer_t s_sec_timer;
static void every(os_timer_t* t, uint32_t ms, void (*fn)()) {
  os_timer_setfn(t, [](void* f) { ((void (*)())f)(); }, (void*)fn);
  os_timer_arm(t, ms, true);
}
#endif
static void (*s_app)(uint32_t) = nullptr;
// boot guard marks, once each
static bool s_lived = false;
static bool s_day_marked = false;
static uint32_t s_up = 0;

void corium::Reactor::clock(void (*ms)()) {
  // else first tick sees all of uptime
  s_ms_last_us = micros();
  corium::_fast_ms_counter = CORIUM_MS_START + s_ms_last_us / 1000;
  EGClock::set(fastMillis);
#ifdef ESP32
  s_ms_ticker.attach_ms(1, ms);
#else
  every(&s_ms_timer, 1, ms);
#endif
}

void corium::Reactor::second() {
#ifdef ESP32
  // a stall runs it once, as the 8266 ticker
  uint32_t now = corium::fastMillis();
  if (corium::reached(now, s_next_sec)) s_next_sec = now + 1000;
#else
  corium::Log::hold(true);   // an os_timer, SYS context: no serial writes here
  // the core keeps the last failed alloc forever: an abort is oom only if it failed this second
  umm_last_fail_alloc_addr = nullptr;
#endif
  corium::TickProfile::beginTick();
  s_up = (uint32_t)corium::Uptime::tick();
  if (s_app) s_app(s_up);
#ifdef TICK_PROFILE
  corium::TickProfile::markApp();
  corium::TickProfile::markWifi();
#endif

  if (corium::TickProfile::endTick()) corium::Registry::logActivityAndReset();
#ifndef ESP32
  corium::Log::hold(false);   // the console's loop prints them
#endif
}

void corium::Reactor::house() {
  corium::statusTick(s_up, corium::Clock::synced, !corium::Wifi::disabled, corium::DeviceInfo::heapNow());
  static uint16_t hour_s = 0;
  if (++hour_s >= 3600) {
    hour_s = 0;
    corium::Registry::agePeaks();
  }
#ifdef ESP32
  corium::DeviceInfo::chipTempSample();
  corium::Clock::stamp();
#endif
  if (!s_day_marked && corium::fastMillis() - CORIUM_MS_START > corium::BootGuard::DAY_MS) {
    corium::BootGuard::markDay();
    s_day_marked = true;
  }
  // wifi off counts as up, else safe mode
  if (!s_lived && corium::Uptime::since(corium::Uptime::start) >= corium::BootGuard::LIVED_S &&
      (corium::Wifi::stableFor(corium::BootGuard::STABLE_MS) || corium::Wifi::disabled)) {
    corium::BootGuard::markLived();
    s_lived = true;
  }
}

#ifdef ESP32
uint32_t corium::Reactor::s_next_sec = 0;
#endif

void corium::Reactor::begin(void (*app)(uint32_t up)) {
  s_app = app;
  corium::Uptime::start = corium::Uptime::tick() + 1;
  s_next_house = corium::fastMillis() + 1500;
#ifdef ESP32
  s_next_sec = corium::fastMillis() + 1000;
  // the tick in loop feeds it
  esp_task_wdt_add(NULL);
#else
  every(&s_sec_timer, 1000, second);
#endif
}
