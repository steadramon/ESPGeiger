/*
  TickProfile.cpp - sTickerCB timing + loop iteration counter.

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
#include "TickProfile.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#ifdef TICK_PROFILE
#include "DeviceInfo.h"
#endif

namespace TickProfile {
  uint32_t tick_us = 0;
  uint32_t tick_max_us = 0;
  uint32_t lps = 0;
  volatile uint32_t _lps_count = 0;
#ifdef LOOP_PROFILE
  uint32_t loop_body_us = 0;
#endif
}

static unsigned long t_start = 0;
static uint8_t tick_max_age = 0;
#ifdef TICK_PROFILE
static unsigned long t_after_counter = 0;
static unsigned long t_after_wifi = 0;
static unsigned long t_after_modules = 0;
static uint32_t max_counter_us = 0;
static uint32_t max_wifi_us = 0;
static uint32_t max_modules_us = 0;
static uint32_t min_free_heap = 0xFFFFFFFFu;
#endif

void TickProfile::beginTick() {
  t_start = micros();
}

#ifdef TICK_PROFILE
void TickProfile::markCounter() { t_after_counter = micros(); }
void TickProfile::markWifi()    { t_after_wifi = micros(); }
void TickProfile::markModules() { t_after_modules = micros(); }
#endif

void TickProfile::endTick() {
  // EMA (a=1/8) in-place: smooths single-second dips so readers don't see phantom drops.
  TickProfile::lps = (TickProfile::lps * 7 + _lps_count) >> 3;
  _lps_count = 0;
  uint32_t this_tick = (uint32_t)(micros() - t_start);
  TickProfile::tick_us = (TickProfile::tick_us * 7 + this_tick) >> 3;
  if (this_tick > TickProfile::tick_max_us) TickProfile::tick_max_us = this_tick;
#ifdef TICK_PROFILE
  uint32_t counter_us = (uint32_t)(t_after_counter - t_start);
  uint32_t wifi_us    = (uint32_t)(t_after_wifi - t_after_counter);
  uint32_t modules_us = (uint32_t)(t_after_modules - t_after_wifi);
  if (counter_us > max_counter_us) max_counter_us = counter_us;
  if (wifi_us    > max_wifi_us)    max_wifi_us    = wifi_us;
  if (modules_us > max_modules_us) max_modules_us = modules_us;
  uint32_t cur_free = DeviceInfo::freeHeap();
  if (cur_free < min_free_heap) min_free_heap = cur_free;
#endif
  if (++tick_max_age >= 60) {
    tick_max_age = 0;
#ifdef TICK_PROFILE
    Log::console(PSTR("Tick profile: total=%u ctr=%u wifi=%u mods=%u lps=%u free=%u min_free=%u frag=%u%%"),
      TickProfile::tick_max_us, max_counter_us, max_wifi_us, max_modules_us, TickProfile::lps,
      (unsigned)cur_free, (unsigned)min_free_heap, (unsigned)DeviceInfo::heapFrag());
    EGModuleRegistry::log_profile_and_reset();
    max_counter_us = max_wifi_us = max_modules_us = 0;
    min_free_heap = 0xFFFFFFFFu;
#endif
#ifdef LOOP_PROFILE
    EGModuleRegistry::log_loop_profile_and_reset();
    // Wall-time accounting: body_us is time spent inside loop() body.
    // 60,000,000 us window - body = time stolen by interrupts/async/yield.
    uint32_t body = loop_body_us;
    loop_body_us = 0;
    uint32_t stolen = (body > 60000000UL) ? 0 : (60000000UL - body);
    Log::console(PSTR("Loop wall: body=%lums stolen=%lums (%lu%% async)"),
      (unsigned long)(body / 1000UL),
      (unsigned long)(stolen / 1000UL),
      (unsigned long)(stolen / 600000UL));
#endif
    EGModuleRegistry::log_activity_and_reset();
    TickProfile::tick_max_us = this_tick;
  }
}
