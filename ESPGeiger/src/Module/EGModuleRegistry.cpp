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
#include "../Counter/Counter.h"
#include "../Prefs/EGPrefs.h"
#include <string.h>


EGModuleRegistry::Slot EGModuleRegistry::_slots[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_count = 0;
uint8_t EGModuleRegistry::_overflow = 0;
unsigned long EGModuleRegistry::_next_loop_due = 0;
uint8_t EGModuleRegistry::_due_order[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_due_count = 0;

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
  bool paused  = Counter::external_paused();
  // Re-check at most this far in the future, even if no module is due.
  unsigned long fallback = now + 1000;

  uint8_t k = 0;
  while (k < _due_count) {
    Slot& s = _slots[_due_order[k]];
    if ((long)(now - s.next_due) < 0) break;
    if (((s.flags & FLAG_REQUIRES_WIFI) && !wifi_ok) ||
        ((s.flags & FLAG_REQUIRES_NTP) && !ntp_ok) ||
        (paused && s.category == (uint8_t)EGP_CAT_UPLOAD)) {
      // Gate closed (wifi/ntp/external-pause). Defer a tick without firing.
      s.next_due = now + 1;
    } else {
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
      s.next_due = now + s.loop_interval;
    }
    // Bubble the just-processed slot back into ascending next_due order.
    uint8_t cur = k;
    while (cur + 1 < _due_count) {
      Slot& a = _slots[_due_order[cur]];
      Slot& b = _slots[_due_order[cur + 1]];
      if ((long)(a.next_due - b.next_due) <= 0) break;
      uint8_t tmp = _due_order[cur];
      _due_order[cur] = _due_order[cur + 1];
      _due_order[cur + 1] = tmp;
      cur++;
    }
    // If the slot moved back, recheck position k; otherwise advance.
    if (cur == k) k++;
  }

  unsigned long next = (_due_count > 0) ? _slots[_due_order[0]].next_due : fallback;
  if ((long)(next - fallback) > 0) next = fallback;
  _next_loop_due = next;
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
  // Bump every queued slot so the sort doesn't early-exit on a stale front.
  unsigned long now = fast_millis();
  for (uint8_t i = 0; i < _due_count; i++) {
    _slots[_due_order[i]].next_due = now;
  }
  _next_loop_due = now;
}

bool EGModuleRegistry::set_loop_interval(EGModule* m, int32_t interval_ms) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module != m) continue;
    Slot& s = _slots[i];
    bool was_loop = (s.flags & FLAG_HAS_LOOP) != 0;
    bool now_loop = (interval_ms >= 0);
    if (now_loop) {
      s.flags |= FLAG_HAS_LOOP;
      s.loop_interval = (interval_ms > 0xFFFF) ? 0xFFFF : (uint16_t)interval_ms;
      s.next_due = s.loop_last + s.loop_interval;
    } else {
      s.flags &= ~FLAG_HAS_LOOP;
    }
    if (was_loop && !now_loop) {
      for (uint8_t k = 0; k < _due_count; k++) {
        if (_due_order[k] == i) {
          for (uint8_t j = k; j + 1 < _due_count; j++) _due_order[j] = _due_order[j + 1];
          _due_count--;
          break;
        }
      }
    } else if (!was_loop && now_loop) {
      if (_due_count < EG_MAX_MODULES) _due_order[_due_count++] = i;
    }
    // Insertion sort over the (small) due index so next_due ordering is
    // restored after any flag/interval change.
    for (uint8_t a = 1; a < _due_count; a++) {
      uint8_t key = _due_order[a];
      unsigned long key_due = _slots[key].next_due;
      uint8_t b = a;
      while (b > 0 && (long)(_slots[_due_order[b - 1]].next_due - key_due) > 0) {
        _due_order[b] = _due_order[b - 1];
        b--;
      }
      _due_order[b] = key;
    }
    _next_loop_due = fast_millis();
    return true;
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
  // :00 kept clear on SD builds for the per-minute CSV flush; non-SD reclaims it.
#ifdef GEIGER_SDCARD
  const uint8_t lo = 1;
#else
  const uint8_t lo = 0;
#endif
  uint8_t span = (uint8_t)(avail - lo + 1);   // slots over [lo, avail]
  uint8_t step60 = (uint8_t)(interval_s % 60);
  uint8_t orbit_n = 1;
  if (step60 != 0) {
    uint8_t a = step60, b = 60;
    while (b) { uint8_t t = a % b; a = b; b = t; }
    orbit_n = 60 / a;
  }

  uint8_t sec = (uint8_t)(lo + (h % span));
  uint8_t tries = 0;
  while (tries < span) {
    bool clash = false;
    uint8_t s = sec; if (s >= 60) s -= 60;
    for (uint8_t k = 0; k < orbit_n; k++) {
      if ((s == 0 && lo != 0) || slot_taken(s)) { clash = true; break; }
      s += step60; if (s >= 60) s -= 60;
    }
    if (!clash) break;
    sec++; if (sec > avail) sec = lo;
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

unsigned long EGModuleRegistry::ping_from_offset(uint32_t offset, unsigned long now,
                                                 uint32_t interval_ms) {
  if (interval_ms == 0) return now;
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

unsigned long EGModuleRegistry::initial_ping(const char* mod_name, unsigned long now,
                                              uint32_t interval_ms) {
  if (interval_ms == 0) return now;
  return ping_from_offset(initial_offset(mod_name, interval_ms), now, interval_ms);
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

  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    EGModule* m = s.module;
    s.loop_interval = m->loop_interval_ms();
    s.loop_last = 0;
    s.next_due = 0;
    s.warmup_seconds = m->warmup_seconds();
    uint8_t f = 0;
    if (m->has_loop())       f |= FLAG_HAS_LOOP;
    if (m->has_tick())       f |= FLAG_HAS_TICK;
    if (m->requires_wifi())  f |= FLAG_REQUIRES_WIFI;
    if (m->requires_ntp())   f |= FLAG_REQUIRES_NTP;
    s.flags = f;
    const EGPrefGroup* g = m->prefs_group();
    s.category = g ? g->category : (uint8_t)EGP_CAT_SYSTEM;
    if (f & FLAG_HAS_LOOP) _due_order[_due_count++] = i;
    m->pre_wifi();
  }
  _next_loop_due = 0;
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
