/*
  TickProfile.h - sTickerCB timing + loop iteration counter.

  Copyright (C) 2026 @steadramon
*/
#ifndef UTIL_TICKPROFILE_H
#define UTIL_TICKPROFILE_H

#include <Arduino.h>

namespace TickProfile {
  extern uint32_t tick_us;      // sTickerCB duration, EMA-smoothed (alpha = 1/8)
  extern uint32_t tick_max_us;  // peak tick_us since last 60s window reset
  extern uint32_t lps;          // loop() iterations counted in last second
  extern volatile uint32_t _lps_count;

  inline void countIter() { ++_lps_count; }

  void beginTick();
#ifdef TICK_PROFILE
  void markCounter();
  void markWifi();
  void markModules();
#endif
  void endTick();
}

#endif
