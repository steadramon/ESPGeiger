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
#include "../Util/Wifi.h"
#include <string.h>


extern Status status;

EGModuleRegistry::Slot EGModuleRegistry::_slots[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_count = 0;
unsigned long EGModuleRegistry::_next_loop_due = 0;

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
    if ((s.flags & FLAG_REQUIRES_WIFI) && Wifi::disabled) {
      Log::debug(PSTR("Module: Skipping %s (wifi disabled)"), s.module->name());
      continue;
    }
    Log::debug(PSTR("Module: Starting %s"), s.module->name());
    s.module->begin();
  }
}

void EGModuleRegistry::loop_all(unsigned long now) {
  // Fast-path: if no module's loop() is due yet, skip the walk entirely.
  // Called from main loop ~50k/s so this check saves a lot of cycles.
  // Signed compare handles millis() wrap safely.
  if ((long)(now - _next_loop_due) < 0) return;

  unsigned long earliest = now + 0x7FFFFFFF;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!(s.flags & FLAG_HAS_LOOP)) continue;
    // Matches begin_all()'s wifi gate: if wifi was disabled at boot,
    // begin() was skipped so loop() has nothing to poll against.
    if ((s.flags & FLAG_REQUIRES_WIFI) && Wifi::disabled) continue;

    unsigned long due = s.loop_last + s.loop_interval;
    if ((long)(now - due) >= 0) {
      s.loop_last = now;
      s.module->loop(now);
      due = now + s.loop_interval;
    }
    if ((long)(due - earliest) < 0) earliest = due;
  }
  _next_loop_due = earliest;
}

void EGModuleRegistry::tick_all(unsigned long now, unsigned long uptime_seconds) {
  bool wifi_ok = !Wifi::disabled && Wifi::connected;
  bool ntp_ok = status.ntp_synced;
  bool seconds = false;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!(s.flags & FLAG_HAS_TICK)) continue;
    if (uptime_seconds < s.warmup_seconds) continue;
    if ((s.flags & FLAG_REQUIRES_WIFI) && !wifi_ok) continue;
    if ((s.flags & FLAG_REQUIRES_NTP) && !ntp_ok) continue;
    if (!seconds) {
      now = now / 1000;
      seconds = true;
    }
#ifdef TICK_PROFILE
    unsigned long t0 = micros();
    s.module->s_tick(now);
    uint32_t d = (uint32_t)(micros() - t0);
    if (d > s.max_tick_us) s.max_tick_us = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
#else
    s.module->s_tick(now);
#endif
  }
}

#ifdef TICK_PROFILE
void EGModuleRegistry::log_profile_and_reset() {
  // Emit every module that ticked this window, sorted slowest first.
  char buf[200];
  int pos = 0;
  uint16_t done = 0;  // bitmask of already-printed slots (up to 16 - matches EG_MAX_MODULES)
  while (true) {
    uint8_t best = 0xFF;
    uint16_t bestv = 0;
    for (uint8_t i = 0; i < _count && i < 16; i++) {
      if (done & (1u << i)) continue;
      uint16_t v = _slots[i].max_tick_us;
      if (v > bestv) { bestv = v; best = i; }
    }
    if (best == 0xFF || bestv == 0) break;
    done |= (1u << best);
    int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s=%u",
      pos ? " " : "", _slots[best].module->name(), bestv);
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  Log::console(PSTR("Mods max: %s"), buf);
  for (uint8_t i = 0; i < _count; i++) _slots[i].max_tick_us = 0;
}
#endif

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

size_t EGModuleRegistry::collect_status_json(char* buf, size_t cap, unsigned long now) {
  size_t pos = 0;
  bool first = true;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!s.module) continue;
    if (cap <= pos + 2) break;
    size_t before = pos;
    if (!first) buf[pos++] = ',';
    size_t n = s.module->status_json(buf + pos, cap - pos, now);
    if (n == 0) {
      pos = before;
    } else {
      pos += n;
      first = false;
    }
  }
  return pos;
}
