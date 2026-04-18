/*
  TickProfile.cpp - sTickerCB timing + loop iteration counter.

  Copyright (C) 2026 @steadramon
*/
#include "TickProfile.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

namespace TickProfile {
  uint32_t tick_us = 0;
  uint32_t tick_max_us = 0;
  uint32_t lps = 0;
  volatile uint32_t _lps_count = 0;
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
  TickProfile::lps = _lps_count;
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
#endif
  if (++tick_max_age >= 60) {
    tick_max_age = 0;
#ifdef TICK_PROFILE
    Log::console(PSTR("Tick profile: total=%u ctr=%u wifi=%u mods=%u lps=%u"),
      TickProfile::tick_max_us, max_counter_us, max_wifi_us, max_modules_us, TickProfile::lps);
    EGModuleRegistry::log_profile_and_reset();
    max_counter_us = max_wifi_us = max_modules_us = 0;
#endif
    TickProfile::tick_max_us = this_tick;
  }
}
