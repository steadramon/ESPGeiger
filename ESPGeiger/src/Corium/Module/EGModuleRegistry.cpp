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
#include "../Net/Wifi.h"
#include "../System/DeviceInfo.h"
#include "../System/Lifecycle.h"
#include "../System/Clock.h"
#include "ExternalPause.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/Fnv.h"
#include <string.h>
#ifdef ESP32
#include <esp_attr.h>
#endif


EGModuleRegistry::Slot EGModuleRegistry::_slots[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_count = 0;
uint8_t EGModuleRegistry::_overflow = 0;
uint32_t EGModuleRegistry::_next_loop_due = 0;
uint8_t EGModuleRegistry::_due_order[EG_MAX_MODULES] = {};
uint8_t EGModuleRegistry::_due_count = 0;
EG_XTASK_VOLATILE bool EGModuleRegistry::_pending = false;
bool EGModuleRegistry::_safe_mode = false;

// min 1 ms, 0 refires in the same pass
static uint16_t loop_interval_from(int32_t ms) {
  if (ms < 1) return 1;
  if (ms > 0xFFFF) return 0xFFFF;
  return (uint16_t)ms;
}

// keep sorted, loop_all reads [0]
void EGModuleRegistry::rebuild_due() {
  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (!(_slots[i].flags & FLAG_HAS_LOOP)) continue;
    uint8_t j = _due_count;
    while (j > 0 && (int32_t)(_slots[_due_order[j - 1]].next_due - _slots[i].next_due) > 0) {
      _due_order[j] = _due_order[j - 1];
      j--;
    }
    _due_order[j] = i;
    _due_count++;
  }
}

bool EGModuleRegistry::add(EGModule* m) {
  if (_count >= EG_MAX_MODULES) { _overflow++; return false; }
  _slots[_count].module = m;
  _slots[_count].pending_ms = NO_REQUEST;
  _count++;
  return true;
}

void EGModuleRegistry::begin_all() {
  if (_overflow) {
    Log::console(PSTR("Module: Registry overflow, %u module(s) dropped (raise EG_MAX_MODULES)"), _overflow);
  }
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (s.flags & FLAG_QUARANTINED) {
      Log::console(PSTR("Module: Skipping %s (quarantined)"), s.module->name());
      continue;
    }
    if ((s.flags & FLAG_REQUIRES_WIFI) && Wifi::disabled) {
      Log::debug(PSTR("Module: Skipping %s (wifi disabled)"), s.module->name());
      continue;
    }
    Log::debug(PSTR("Module: Starting %s"), s.module->name());
    s.module->begin();
    yield();
  }
}

inline uint8_t EGModuleRegistry::unmet_requirements() {
  uint8_t unmet = 0;
  if (!Wifi::stable_for(WIFI_SETTLE_MS)) unmet |= FLAG_REQUIRES_WIFI;
  if (!Clock::synced) unmet |= FLAG_REQUIRES_NTP;
  return unmet;
}

inline void EGModuleRegistry::fire_slot(Slot& s, unsigned long now) {
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

inline uint8_t EGModuleRegistry::sift_due(uint8_t k) {
  uint8_t cur = k;
  while (cur + 1 < _due_count) {
    Slot& a = _slots[_due_order[cur]];
    Slot& b = _slots[_due_order[cur + 1]];
    if ((int32_t)(a.next_due - b.next_due) <= 0) break;
    uint8_t idx = _due_order[cur];
    _due_order[cur] = _due_order[cur + 1];
    _due_order[cur + 1] = idx;
    cur++;
  }
  return cur;
}

// at most 1 s ahead
inline uint32_t EGModuleRegistry::next_wake(unsigned long now) {
  uint32_t fallback = now + 1000;
  uint32_t next = (_due_count > 0) ? _slots[_due_order[0]].next_due : fallback;
  if ((int32_t)(next - fallback) > 0) next = fallback;
  return next;
}

void EGModuleRegistry::loop_all(unsigned long now) {
  if (_pending) apply_pending();
  if ((int32_t)(now - _next_loop_due) < 0) return;

  if (ota_in_progress) {
    for (uint8_t i = 0; i < _count; i++) {
      if (!(_slots[i].flags & FLAG_DURING_OTA)) continue;
      _slots[i].module->loop(now);
    }
    _next_loop_due = now;
    return;
  }

  uint8_t unmet = unmet_requirements();
  bool paused = ExternalPause::active();

  uint8_t k = 0;
  while (k < _due_count) {
    uint8_t idx = _due_order[k];
    Slot& s = _slots[idx];
    if ((int32_t)(now - s.next_due) < 0) break;
    if ((s.flags & unmet) || (paused && s.category == (uint8_t)EGP_CAT_UPLOAD)) {
      // gate closed, retry within 1 s
      s.next_due = now + (s.loop_interval < 1000 ? s.loop_interval : 1000);
    } else {
      fire_slot(s, now);
    }
    if (sift_due(k) == k) k++;
  }
  _next_loop_due = next_wake(now);
}

inline void EGModuleRegistry::tick_slot(Slot& s, unsigned long now_s) {
#ifdef TICK_PROFILE
  unsigned long t0 = micros();
  s.module->s_tick(now_s);
  uint32_t d = (uint32_t)(micros() - t0);
  if (d > s.max_tick_us) s.max_tick_us = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
#else
  s.module->s_tick(now_s);
#endif
}

void EGModuleRegistry::tick_all(unsigned long now, unsigned long uptime_seconds) {
  if (ota_in_progress) return;
  bool wifi_ok = Wifi::stable_for(WIFI_SETTLE_MS);
  bool ntp_ok = Clock::synced;
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
    tick_slot(s, now);
  }
}

#ifdef TICK_PROFILE
void EGModuleRegistry::log_profile_and_reset() {
  char buf[200];
  int pos = 0;
  uint32_t done = 0;  // printed slots
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
      pos ? PSTR(" ") : PSTR(""), _slots[best].module->name(), bestv);
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  Log::console(PSTR("Mods max: %s"), buf);
  for (uint8_t i = 0; i < _count; i++) _slots[i].max_tick_us = 0;
}
#endif

#ifdef LOOP_PROFILE
// biggest total_loop_us first
void EGModuleRegistry::log_loop_profile_and_reset() {
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
      pos ? PSTR(" ") : PSTR(""), s.module->name(),
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
    PGM_P sep = pos ? PSTR(" ") : PSTR("");
    int n;
    if (err == 0) {
      n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%u"), sep, m->name(), ok);
    } else {
      n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%s%s=%u/%u"), sep, m->name(), ok, (uint8_t)(ok + err));
    }
    if (n <= 0 || pos + n >= (int)sizeof(buf)) break;
    pos += n;
  }
  if (any) Log::console(PSTR("Activity: %s"), buf);
}

void EGModuleRegistry::wake() {
  // all of them, a stale front stops the walk
  unsigned long now = fast_millis();
  for (uint8_t i = 0; i < _due_count; i++) {
    _slots[_due_order[i]].next_due = now;
  }
  _next_loop_due = now;
}

bool EGModuleRegistry::set_loop_interval(EGModule* m, int32_t interval_ms) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module != m) continue;
    if (_slots[i].flags & FLAG_QUARANTINED) return false;
    EGREG_LOCK();
    _slots[i].pending_ms = interval_ms;
    _pending = true;
    EGREG_UNLOCK();
    return true;
  }
  return false;
}

void EGModuleRegistry::apply_pending() {
  EGREG_LOCK();
  _pending = false;
  EGREG_UNLOCK();
  uint32_t now = fast_millis();
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    EGREG_LOCK();
    int32_t interval_ms = s.pending_ms;
    s.pending_ms = NO_REQUEST;
    EGREG_UNLOCK();
    if (interval_ms == NO_REQUEST) continue;
    if (interval_ms >= 0) {
      s.flags |= FLAG_HAS_LOOP;
      s.loop_interval = loop_interval_from(interval_ms);
      // overdue, or idle past 2^31 ms
      if ((uint32_t)(now - s.loop_last) >= s.loop_interval) s.next_due = now;
      else s.next_due = s.loop_last + s.loop_interval;
    } else {
      s.flags &= ~FLAG_HAS_LOOP;
    }
  }
  rebuild_due();
  _next_loop_due = now;
}

bool EGModuleRegistry::set_tick_enabled(EGModule* m, bool enabled) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module == m) {
      if (_slots[i].flags & FLAG_QUARANTINED) return false;
      if (enabled) _slots[i].flags |=  FLAG_HAS_TICK;
      else         _slots[i].flags &= ~FLAG_HAS_TICK;
      return true;
    }
  }
  return false;
}

// two halves, no uint64 shift helper
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

static uint32_t chip_hash_cached(uint16_t* jitter) {
  static uint32_t s_chip_h = 0;
  static uint16_t s_jitter = 0;
  if (s_chip_h == 0) {
    s_chip_h = fnv1a(DeviceInfo::chipid());
    s_jitter = (uint16_t)((s_chip_h >> 8) % 1000u);
  }
  *jitter = s_jitter;
  return s_chip_h;
}

// 60 / gcd(step, 60)
static uint8_t orbit_length(uint8_t step60) {
  if (step60 == 0) return 1;
  uint8_t a = step60, b = 60;
  while (b) { uint8_t t = a % b; a = b; b = t; }
  return 60 / a;
}

static bool orbit_clashes(uint8_t sec, uint8_t step60, uint8_t orbit_n, uint8_t lo) {
  uint8_t s = sec; if (s >= 60) s -= 60;
  for (uint8_t k = 0; k < orbit_n; k++) {
    if ((s == 0 && lo != 0) || slot_taken(s)) return true;
    s += step60; if (s >= 60) s -= 60;
  }
  return false;
}

static void orbit_claim(uint8_t sec, uint8_t step60, uint8_t orbit_n) {
  uint8_t s = sec; if (s >= 60) s -= 60;
  for (uint8_t k = 0; k < orbit_n; k++) {
    slot_claim(s);
    s += step60; if (s >= 60) s -= 60;
  }
}

uint32_t EGModuleRegistry::initial_offset(const char* mod_name, uint32_t interval_ms) {
  if (interval_ms == 0 || !mod_name) return 0;

  uint16_t jitter;
  uint32_t h = fnv1a_P(mod_name, chip_hash_cached(&jitter));

  uint16_t interval_s = interval_ms < 1000 ? 0 : (uint16_t)(interval_ms / 1000);
  if (interval_s < 2) return interval_ms >> 1;

  uint8_t avail = interval_s > 63 ? 63 : (uint8_t)(interval_s - 1);
  // app holds :00
#ifdef APP_SEC0_BUSY
  const uint8_t lo = 1;
#else
  const uint8_t lo = 0;
#endif
  uint8_t span = (uint8_t)(avail - lo + 1);
  uint8_t step60 = (uint8_t)(interval_s % 60);
  uint8_t orbit_n = orbit_length(step60);

  uint8_t sec = (uint8_t)(lo + (h % span));
  for (uint8_t tries = 0; tries < span; tries++) {
    if (!orbit_clashes(sec, step60, orbit_n, lo)) break;
    sec++; if (sec > avail) sec = lo;
  }
  orbit_claim(sec, step60, orbit_n);
  uint32_t off = (uint32_t)sec * 1000UL + jitter;
  if (off >= interval_ms) off = (uint32_t)sec * 1000UL;
  return off;
}

unsigned long EGModuleRegistry::ping_from_offset(uint32_t offset, unsigned long now,
                                                 uint32_t interval_ms) {
  if (interval_ms == 0) return now;
  uint32_t until;
  uint32_t secs = interval_ms / 1000;
  if (secs > 0 && Clock::synced) {
    uint32_t wall_ms = (Clock::epoch() % secs) * 1000UL;
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
  int32_t until = (int32_t)(target_ms - now);
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
    if (strcmp_P(name, _slots[i].module->name()) == 0) {
      return _slots[i].module;
    }
  }
  return nullptr;
}

// stable, keeps registration order
void EGModuleRegistry::sort_by_priority() {
  for (uint8_t i = 1; i < _count; i++) {
    Slot cur = _slots[i];
    uint8_t j = i;
    while (j > 0 && _slots[j - 1].module->priority() > cur.module->priority()) {
      _slots[j] = _slots[j - 1];
      j--;
    }
    _slots[j] = cur;
  }
}

// cached, no virtual calls in loop_all()
void EGModuleRegistry::pre_wifi_all() {
  sort_by_priority();

  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    EGModule* m = s.module;
    s.loop_last = 0;
    s.next_due = 0;
    s.pending_ms = NO_REQUEST;
    if (_safe_mode && !m->core_module()) {
      s.flags = FLAG_QUARANTINED;
      s.category = (uint8_t)EGP_CAT_SYSTEM;
      continue;
    }
    s.loop_interval = loop_interval_from(m->loop_interval_ms());
    s.warmup_seconds = m->warmup_seconds();
    uint8_t f = 0;
    if (m->has_loop())       f |= FLAG_HAS_LOOP;
    if (m->has_tick())       f |= FLAG_HAS_TICK;
    if (m->requires_wifi())  f |= FLAG_REQUIRES_WIFI;
    if (m->requires_ntp())   f |= FLAG_REQUIRES_NTP;
    if (m->runs_during_ota()) f |= FLAG_DURING_OTA;
    s.flags = f;
    const EGPrefGroup* g = m->prefs_group();
    s.category = g ? g->category.get() : (uint8_t)EGP_CAT_SYSTEM;
    if (f & FLAG_HAS_LOOP) _due_order[_due_count++] = i;
    m->pre_wifi();
    yield();
  }
  _next_loop_due = 0;
}

void EGModuleRegistry::set_safe_mode(bool on) {
  _safe_mode = on;
}

bool EGModuleRegistry::quarantined(EGModule* m) {
  if (!_safe_mode) return false;
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module == m) return _slots[i].flags & FLAG_QUARANTINED;
  }
  return false;
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
    size_t n;
    if (s.flags & FLAG_QUARANTINED) {
      n = snprintf_P(buf + pos, cap - pos, PSTR("\"%s\":{\"quarantined\":true}"), s.module->name());
    } else {
      n = s.module->status_json(buf + pos, cap - pos, now);
    }
    if (n == 0) {
      pos = before;
    } else if (n >= cap - pos) {
      // truncated, drop it
      pos = before;
      break;
    } else {
      pos += n;
      first = false;
    }
  }
  return pos;
}
