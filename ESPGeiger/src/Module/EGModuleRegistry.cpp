/*
  EGModuleRegistry.cpp - Registry of ESPGeiger modules

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "EGModuleRegistry.h"
#include "../Logger/Logger.h"
#include "../Status.h"
#include <string.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

extern Status status;

EGModuleRegistry::Slot EGModuleRegistry::_slots[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_count = 0;

bool EGModuleRegistry::add(EGModule* m) {
  if (_count >= EG_MAX_MODULES) return false;
  _slots[_count].module = m;
  _count++;
  return true;
}

void EGModuleRegistry::begin_all() {
  // Array is already sorted by pre_wifi_all()
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if ((s.flags & FLAG_REQUIRES_WIFI) && status.wifi_disabled) {
      Log::debug(PSTR("Module: Skipping %s (wifi disabled)"), s.module->name());
      continue;
    }
    Log::debug(PSTR("Module: Starting %s"), s.module->name());
    s.module->begin();
  }
}

void EGModuleRegistry::loop_all(unsigned long now) {
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!(s.flags & FLAG_HAS_LOOP)) continue;
    // Matches begin_all()'s wifi gate: if wifi was disabled at boot,
    // begin() was skipped so loop() has nothing to poll against.
    if ((s.flags & FLAG_REQUIRES_WIFI) && status.wifi_disabled) continue;
    if (s.loop_interval && (now - s.loop_last < s.loop_interval)) continue;
    s.loop_last = now;
    s.module->loop(now);
  }
}

void EGModuleRegistry::tick_all(unsigned long now, unsigned long uptime_seconds) {
  bool wifi_ok = !status.wifi_disabled && (WiFi.status() == WL_CONNECTED);
  bool ntp_ok = status.ntp_synced;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!(s.flags & FLAG_HAS_TICK)) continue;
    if (uptime_seconds < s.warmup_seconds) continue;
    if ((s.flags & FLAG_REQUIRES_WIFI) && !wifi_ok) continue;
    if ((s.flags & FLAG_REQUIRES_NTP) && !ntp_ok) continue;
    s.module->s_tick(now);
  }
}

uint8_t EGModuleRegistry::count() {
  return _count;
}

EGModule* EGModuleRegistry::get(uint8_t idx) {
  if (idx >= _count) return nullptr;
  return _slots[idx].module;
}

EGModule* EGModuleRegistry::find(const char* name) {
  if (name == nullptr) return nullptr;
  for (uint8_t i = 0; i < _count; i++) {
    if (strcmp(_slots[i].module->name(), name) == 0) {
      return _slots[i].module;
    }
  }
  return nullptr;
}

void EGModuleRegistry::pre_wifi_all() {
  for (uint8_t i = 1; i < _count; i++) {
    Slot cur = _slots[i];
    uint8_t j = i;
    while (j > 0 && _slots[j - 1].module->priority() > cur.module->priority()) {
      _slots[j] = _slots[j - 1];
      j--;
    }
    _slots[j] = cur;
  }

  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    EGModule* m = s.module;
    s.loop_interval = m->loop_interval_ms();
    s.loop_last = 0;
    s.warmup_seconds = m->warmup_seconds();
    uint8_t f = 0;
    if (m->has_loop())       f |= FLAG_HAS_LOOP;
    if (m->has_tick())       f |= FLAG_HAS_TICK;
    if (m->requires_wifi())  f |= FLAG_REQUIRES_WIFI;
    if (m->requires_ntp())   f |= FLAG_REQUIRES_NTP;
    s.flags = f;
    m->pre_wifi();
  }
}
