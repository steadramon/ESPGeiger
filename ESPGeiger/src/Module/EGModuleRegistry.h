/*
  EGModuleRegistry.h - Registry of ESPGeiger modules

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef EGMODULEREGISTRY_H
#define EGMODULEREGISTRY_H

#include "EGModule.h"

#define EG_MAX_MODULES 16

class EGModuleRegistry {
  public:
    static bool add(EGModule* m);
    static void pre_wifi_all();
    static void begin_all();
    static void loop_all(unsigned long now);
    static void tick_all(unsigned long now, unsigned long uptime_seconds);
    static uint8_t count();
    static EGModule* get(uint8_t idx);
    static EGModule* find(const char* name);
    static bool has(const char* name) { return find(name) != nullptr; }

  private:
    static constexpr uint8_t FLAG_HAS_LOOP      = 1 << 0;
    static constexpr uint8_t FLAG_REQUIRES_WIFI = 1 << 1;
    static constexpr uint8_t FLAG_REQUIRES_NTP  = 1 << 2;
    static constexpr uint8_t FLAG_HAS_TICK      = 1 << 3;

    struct Slot {
      EGModule* module;          // 4
      unsigned long loop_last;   // 4 — last millis() loop() ran
      uint16_t loop_interval;    // 2 — 0 = every iteration
      uint16_t warmup_seconds;   // 2 — cached, tick_all skips if uptime < this
      uint8_t flags;             // 1 — packed module flags (see above)
    };
    static Slot _slots[EG_MAX_MODULES];
    static uint8_t _count;
};

#define EG_REGISTER_MODULE(instance) \
  static bool _reg_##instance = EGModuleRegistry::add(&instance);

#endif
