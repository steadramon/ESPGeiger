/*
  EGModuleRegistry.cpp - Registry of ESPGeiger modules

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

#include "EGModuleRegistry.h"
#include "../Logger/Logger.h"
#include "../Util/Wifi.h"
#include "../Util/DeviceInfo.h"
#include "../NTP/NTP.h"
#include "../ArduinoOTA/ArduinoOTA.h"
#include <string.h>


EGModuleRegistry::Slot EGModuleRegistry::_slots[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_count = 0;
uint8_t EGModuleRegistry::_overflow = 0;
unsigned long EGModuleRegistry::_next_loop_due = 0;

bool EGModuleRegistry::add(EGModule* m) {
  if (_count >= EG_MAX_MODULES) { _overflow++; return false; }
  _slots[_count].module = m;
  _count++;
  return true;
}

void EGModuleRegistry::begin_all() {
  if (_overflow) {
    Log::console(PSTR("Module: Registry overflow, %u module(s) dropped (raise EG_MAX_MODULES)"), _overflow);
  }
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
  if ((long)(now - _next_loop_due) < 0) return;

  if (ota_in_progress) {
    ota.loop(now);
    _next_loop_due = now;
    return;
  }

  bool wifi_ok = Wifi::stable_for(WIFI_SETTLE_MS);
  bool ntp_ok  = ntpclient.synced;
  unsigned long earliest = now + 0x7FFFFFFF;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (!(s.flags & FLAG_HAS_LOOP)) continue;
    if ((s.flags & FLAG_REQUIRES_WIFI) && !wifi_ok) continue;
    if ((s.flags & FLAG_REQUIRES_NTP) && !ntp_ok) continue;

    unsigned long due = s.loop_last + s.loop_interval;
    if ((long)(now - due) >= 0) {
      s.loop_last = now;
#ifdef LOOP_PROFILE
      unsigned long t0 = micros();
      s.module->loop(now);
      uint32_t d = (uint32_t)(micros() - t0);
      if (d > s.max_loop_us) s.max_loop_us = d;
      s.total_loop_us += d;
      if (s.loop_calls < 0xFFFF) s.loop_calls++;
#else
      s.module->loop(now);
#endif
      due = now + s.loop_interval;
    }
    if ((long)(due - earliest) < 0) earliest = due;
  }
  _next_loop_due = earliest;
}

void EGModuleRegistry::tick_all(unsigned long now, unsigned long uptime_seconds) {
  if (ota_in_progress) return;
  bool wifi_ok = Wifi::stable_for(WIFI_SETTLE_MS);
  bool ntp_ok = ntpclient.synced;
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
  uint32_t done = 0;  // bitmask of already-printed slots (sized for EG_MAX_MODULES)
  while (true) {
    uint8_t best = 0xFF;
    uint16_t bestv = 0;
    for (uint8_t i = 0; i < _count && i < EG_MAX_MODULES; i++) {
      if (done & (1u << i)) continue;
      uint16_t v = _slots[i].max_tick_us;
      if (v > bestv) { bestv = v; best = i; }
    }
    if (best == 0xFF || bestv == 0) break;
    done |= (1u << best);
    int n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%u"),
      pos ? " " : "", _slots[best].module->name(), bestv);
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  Log::console(PSTR("Mods max: %s"), buf);
  for (uint8_t i = 0; i < _count; i++) _slots[i].max_tick_us = 0;
}
#endif

#ifdef LOOP_PROFILE
void EGModuleRegistry::log_loop_profile_and_reset() {
  // Sort by total_loop_us so the biggest CPU consumers list first.
  char buf[240];
  int pos = 0;
  uint32_t done = 0;
  while (true) {
    uint8_t best = 0xFF;
    uint32_t bestv = 0;
    for (uint8_t i = 0; i < _count && i < EG_MAX_MODULES; i++) {
      if (done & (1u << i)) continue;
      uint32_t v = _slots[i].total_loop_us;
      if (v > bestv) { bestv = v; best = i; }
    }
    if (best == 0xFF || bestv == 0) break;
    done |= (1u << best);
    Slot& s = _slots[best];
    int n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%lu/%u/%lu"),
      pos ? " " : "", s.module->name(),
      (unsigned long)s.total_loop_us, s.loop_calls, (unsigned long)s.max_loop_us);
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  if (pos > 0) Log::console(PSTR("Loop total/calls/max: %s"), buf);
  for (uint8_t i = 0; i < _count; i++) {
    _slots[i].max_loop_us = 0;
    _slots[i].total_loop_us = 0;
    _slots[i].loop_calls = 0;
  }
}
#endif

void EGModuleRegistry::log_activity_and_reset() {
  char buf[200];
  int pos = 0;
  bool any = false;
  for (uint8_t i = 0; i < _count; i++) {
    EGModule* m = _slots[i].module;
    uint8_t ok = m->take_publish_ok();
    uint8_t err = m->take_publish_err();
    if (ok == 0 && err == 0) continue;
    any = true;
    int n;
    if (err == 0) {
      n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%u"), pos ? " " : "", m->name(), ok);
    } else {
      n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%u/%u"), pos ? " " : "", m->name(), ok, (uint8_t)(ok + err));
    }
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  if (any) Log::console(PSTR("Activity: %s"), buf);
}

void EGModuleRegistry::wake() {
  _next_loop_due = millis();
}

bool EGModuleRegistry::set_loop_interval(EGModule* m, int32_t interval_ms) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module == m) {
      if (interval_ms < 0) {
        _slots[i].flags &= ~FLAG_HAS_LOOP;
      } else {
        _slots[i].flags |= FLAG_HAS_LOOP;
        _slots[i].loop_interval = (interval_ms > 0xFFFF) ? 0xFFFF : (uint16_t)interval_ms;
      }
      _next_loop_due = millis();
      return true;
    }
  }
  return false;
}

bool EGModuleRegistry::set_tick_enabled(EGModule* m, bool enabled) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module == m) {
      if (enabled) _slots[i].flags |=  FLAG_HAS_TICK;
      else         _slots[i].flags &= ~FLAG_HAS_TICK;
      return true;
    }
  }
  return false;
}

// Split 60-bit claim bitmap across two 32-bit halves: inner shift becomes a
// native ESP8266 op rather than a runtime uint64 shift helper.
static uint32_t s_claimed_lo = 0;
static uint32_t s_claimed_hi = 0;

static inline bool slot_taken(uint8_t s) {
  return s < 32 ? ((s_claimed_lo >> s) & 1u) != 0
                : ((s_claimed_hi >> (s - 32)) & 1u) != 0;
}
static inline void slot_claim(uint8_t s) {
  if (s < 32) s_claimed_lo |= (1u << s);
  else        s_claimed_hi |= (1u << (s - 32));
}

uint32_t EGModuleRegistry::initial_offset(const char* mod_name, uint32_t interval_ms) {
  if (interval_ms == 0 || !mod_name) return 0;

  // Cache chipid hash + sub-second jitter once.
  static uint32_t s_chip_h = 0;
  static uint16_t s_jitter = 0;
  if (s_chip_h == 0) {
    s_chip_h = 2166136261u;
    for (const char* p = DeviceInfo::chipid(); p && *p; p++) s_chip_h = (s_chip_h ^ (uint8_t)*p) * 16777619u;
    s_jitter = (uint16_t)((s_chip_h >> 8) % 1000u);
  }
  // Per-module hash: stable per (device, module) across firmware versions.
  uint32_t h = s_chip_h;
  for (const char* p = mod_name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;

  uint16_t interval_s = interval_ms < 1000 ? 0 : (uint16_t)(interval_ms / 1000);
  if (interval_s < 2) return interval_ms >> 1;

  uint8_t avail = interval_s > 63 ? 63 : (uint8_t)(interval_s - 1);
  uint8_t step60 = (uint8_t)(interval_s % 60);
  uint8_t orbit_n = 1;
  if (step60 != 0) {
    uint8_t a = step60, b = 60;
    while (b) { uint8_t t = a % b; a = b; b = t; }
    orbit_n = 60 / a;
  }

  uint8_t sec = (uint8_t)((h % avail) + 1u);
  uint8_t tries = 0;
  while (tries <= avail) {
    bool clash = false;
    uint8_t s = sec; if (s >= 60) s -= 60;
    for (uint8_t k = 0; k < orbit_n; k++) {
      if (s == 0 || slot_taken(s)) { clash = true; break; }
      s += step60; if (s >= 60) s -= 60;
    }
    if (!clash) break;
    sec++; if (sec > avail) sec = 1;
    tries++;
  }
  uint8_t s = sec; if (s >= 60) s -= 60;
  for (uint8_t k = 0; k < orbit_n; k++) {
    slot_claim(s);
    s += step60; if (s >= 60) s -= 60;
  }
  uint32_t off = (uint32_t)sec * 1000UL + s_jitter;
  if (off >= interval_ms) off = (uint32_t)sec * 1000UL;
  return off;
}

unsigned long EGModuleRegistry::initial_ping(const char* mod_name, unsigned long now,
                                              uint32_t interval_ms) {
  if (interval_ms == 0) return now;
  uint32_t offset = initial_offset(mod_name, interval_ms);
  uint32_t until;
  uint32_t secs = interval_ms / 1000;
  if (secs > 0 && ntpclient.synced) {
    uint32_t wall_ms = ((uint32_t)time(NULL) % secs) * 1000UL;
    until = (offset >= wall_ms) ? (offset - wall_ms) : (interval_ms - wall_ms + offset);
  } else {
    until = offset;
  }
  return now + until - interval_ms;
}

bool EGModuleRegistry::sleep_until(EGModule* m, unsigned long now, unsigned long target_ms) {
  long until = (long)(target_ms - now);
  uint16_t interval;
  if (until <= 0)         interval = 100;
  else if (until > 60000) interval = 60000;
  else if (until > 2000)  interval = (uint16_t)(until - 1000);
  else                    interval = 200;
  return set_loop_interval(m, interval);
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
