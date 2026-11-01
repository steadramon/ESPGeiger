/*
  corium::Registry.cpp - Registry of modules

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

#include "Registry.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/Clock.h"
#include "Corium/System/IsrLock.h"
#ifdef ESP32
#include <esp_attr.h>
#endif


corium::Registry::Slot corium::Registry::_slots[CORIUM_MAX_MODULES] = {};
uint8_t corium::Registry::_count = 0;
uint8_t corium::Registry::_overflow = 0;
uint16_t corium::Registry::_features = 0;
uint32_t corium::Registry::_next_loop_due = 0;
uint8_t corium::Registry::_due_order[CORIUM_MAX_MODULES] = {};
uint8_t corium::Registry::_due_count = 0;
bool corium::Registry::_resort = false;
#ifdef ESP32
__NOINIT_ATTR uint8_t corium::Registry::_firing;   // crash_boot reads it first
#else
uint8_t corium::Registry::_firing = 0xFF;
#endif
#ifdef CORIUM_CRASH_MARK
uint8_t corium::Registry::_crashed_in = 0xFF;
#endif
IsrLock::Bits corium::Registry::_posted;
bool corium::Registry::_safe_mode = false;

// keep sorted, loop_all reads [0]
void corium::Registry::rebuild_due() {
  _resort = false;
  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (!(_slots[i].flags & FLAG_ARMED)) continue;
    uint8_t j = _due_count;
    while (j > 0 && (int32_t)(_slots[_due_order[j - 1]].next_due - _slots[i].next_due) > 0) {
      _due_order[j] = _due_order[j - 1];
      j--;
    }
    _due_order[j] = i;
    _due_count++;
  }
}

bool corium::Registry::add(corium::Module* m) {
  if (_count >= CORIUM_MAX_MODULES) { _overflow++; return false; }
  _slots[_count].module = m;
  m->_slot = _count;
  _count++;
  return true;
}

// built in, slot or not
bool corium::Registry::add(corium::Module* m, uint8_t feature) {
  _features |= (uint16_t)(1u << feature);
  return add(m);
}

void corium::Registry::begin_all() {
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
      s.flags = (s.flags | FLAG_OFF) & ~FLAG_ARMED;
      _resort = true;
      continue;
    }
    Log::debug(PSTR("Module: Starting %s"), s.module->name());
    s.module->begin();
    yield();
  }
}

inline uint8_t corium::Registry::unmet_requirements() {
  uint8_t unmet = 0;
  if (!Wifi::stable_for(WIFI_SETTLE_MS)) unmet |= FLAG_REQUIRES_WIFI;
  if (!Clock::synced) unmet |= FLAG_REQUIRES_NTP;
  return unmet;
}

// period re-armed first, the loop's own due_at wins
inline void corium::Registry::fire_slot(uint8_t idx, unsigned long now) {
  Slot& s = _slots[idx];
  s.loop_last = now;
  if (s.period) s.next_due = now + s.period;
  else s.flags &= ~FLAG_ARMED;
  _firing = idx;
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
  _firing = 0xFF;
  if (!(s.flags & FLAG_ARMED)) _resort = true;
}

// later only, all before [k] are due already; equal stays
inline uint8_t corium::Registry::sift_due(uint8_t k) {
  uint8_t idx = _due_order[k];
  uint32_t due = _slots[idx].next_due;
  while (k + 1 < _due_count && (int32_t)(due - _slots[_due_order[k + 1]].next_due) > 0) {
    _due_order[k] = _due_order[k + 1];
    k++;
  }
  _due_order[k] = idx;
  return k;
}

// nothing armed, look again in a minute
inline uint32_t corium::Registry::next_wake(unsigned long now) {
  return _due_count ? _slots[_due_order[0]].next_due : now + 60000;
}

void corium::Registry::take_posts(unsigned long now) {
  uint32_t p = _posted.take();
  for (uint8_t i = 0; p && i < _count; i++, p >>= 1) {
    Slot& s = _slots[i];
    // off drops it, a closed gate holds it
    if (!(p & 1) || (s.flags & FLAG_OFF)) continue;
    s.next_due = now;
    s.flags |= FLAG_ARMED;
  }
  _resort = true;
  _next_loop_due = now;
}

// gate closed, retry within 1 s
static inline uint32_t retry_in(uint32_t period) {
  return period && period < 1000 ? period : 1000;
}

inline void corium::Registry::walk(unsigned long now) {
  uint8_t unmet = unmet_requirements();
  uint32_t t0 = micros();
  uint32_t fired = 0;

  uint8_t k = 0;
  while (k < _due_count) {
    uint8_t idx = _due_order[k];
    Slot& s = _slots[idx];
    if (!reached(now, s.next_due)) break;
    // once a pass, due_in(0) runs on the next
    if (fired & (1u << idx)) { k++; continue; }
    if (s.flags & unmet) {
      s.next_due = now + retry_in(s.period);
    } else {
      fired |= 1u << idx;
      fire_slot(idx, now);
    }
    if (_resort) { rebuild_due(); k = 0; }
    else if (sift_due(k) == k) k++;
    // the rest next pass
    if (fired && (uint32_t)(micros() - t0) >= CORIUM_LOOP_BUDGET_US) break;
  }
}

void corium::Registry::loop_all(unsigned long now) {
  if (_posted.any()) take_posts(now);
  if (_resort) rebuild_due();
  if (!reached(now, _next_loop_due)) return;

  if (ota_in_progress) {
    for (uint8_t i = 0; i < _count; i++) {
      if (!(_slots[i].flags & FLAG_DURING_OTA)) continue;
      _slots[i].module->loop(now);
    }
    _next_loop_due = now;
    return;
  }
  walk(now);
  if (_resort) rebuild_due();
  _next_loop_due = next_wake(now);
}

#ifdef LOOP_PROFILE
// biggest total_loop_us first
void corium::Registry::log_loop_profile_and_reset() {
  char buf[240];
  int pos = 0;
  uint32_t done = 0;
  while (true) {
    uint8_t best = 0xFF;
    uint32_t bestv = 0;
    for (uint8_t i = 0; i < _count && i < CORIUM_MAX_MODULES; i++) {
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

void corium::Registry::log_activity_and_reset() {
  char buf[200];
  int pos = 0;
  bool any = false;
  for (uint8_t i = 0; i < _count; i++) {
    corium::Module* m = _slots[i].module;
    corium::Sender* s = m->sender();
    if (!s) continue;
    uint8_t ok = s->take_publish_ok();
    uint8_t err = s->take_publish_err();
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

corium::Registry::Slot* corium::Registry::slot_of(corium::Module* m) {
  uint8_t i = m->_slot;
  if (i >= _count || (_slots[i].flags & FLAG_OFF)) return nullptr;
  return &_slots[i];
}

// the walk sorts its own slot
void corium::Registry::changed(Slot& s) {
  if (&s - _slots == _firing) return;
  _resort = true;
  _next_loop_due = fast_millis();
}

bool corium::Registry::every(corium::Module* m, uint32_t ms) {
  Slot* s = slot_of(m);
  if (!s) return false;
  s->period = ms;
  if (!ms) {
    s->flags &= ~FLAG_ARMED;
  } else {
    uint32_t now = fast_millis();
    // off to on runs now; overdue, or idle past 2^31 ms
    bool start = !(s->flags & FLAG_ARMED);
    s->next_due = start || (uint32_t)(now - s->loop_last) >= ms ? now : s->loop_last + ms;
    s->flags |= FLAG_ARMED;
  }
  changed(*s);
  return true;
}

bool corium::Registry::due_at(corium::Module* m, uint32_t t) {
  Slot* s = slot_of(m);
  if (!s) return false;
  s->next_due = t;
  s->flags |= FLAG_ARMED;
  changed(*s);
  return true;
}

void corium::Registry::post(corium::Module* m) {
  uint8_t i = m->_slot;
  if (i >= CORIUM_MAX_MODULES) return;
  _posted.set(1u << i);
}

bool corium::Registry::on(uint8_t topic, corium::Module* m) {
  Slot* s = slot_of(m);
  if (!s || topic >= CORIUM_MAX_TOPICS) return false;
  s->topics |= 1u << topic;
  return true;
}

void corium::Registry::emit(uint8_t topic) {
  if (topic >= CORIUM_MAX_TOPICS) return;
  uint16_t bit = 1u << topic;
  uint32_t to = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (!(_slots[i].topics & bit)) continue;
    _slots[i].events |= bit;
    to |= 1u << i;
  }
  if (!to) return;
  _posted.set(to);
}

uint16_t corium::Registry::events(corium::Module* m) {
  Slot* s = slot_of(m);
  if (!s) return 0;
  uint16_t e = s->events;
  s->events = 0;
  return e;
}

uint8_t corium::Registry::count() {
  return _count;
}

corium::Module* corium::Registry::get(uint8_t idx) {
  if (idx >= _count) return nullptr;
  return _slots[idx].module;
}

corium::Module* corium::Registry::find(const char* name) {
  if (name == nullptr) return nullptr;
  for (uint8_t i = 0; i < _count; i++) {
    if (strcmp_P(name, _slots[i].module->name()) == 0) {
      return _slots[i].module;
    }
  }
  return nullptr;
}

// stable, keeps registration order
void corium::Registry::sort_by_priority() {
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

// cached, no virtual calls in loop_all(); nothing armed till a module asks
void corium::Registry::pre_wifi_all() {
  _firing = IN_NONE;   // no crash_boot, junk stays out
  sort_by_priority();

  uint32_t now = fast_millis();
  _posted.take();   // bits were registration order
  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    corium::Module* m = s.module;
    m->_slot = i;
    s.loop_last = now;
    s.next_due = now;
    s.period = 0;
    s.topics = 0;
    s.events = 0;
    if (_safe_mode && !m->core_module()) {
      s.flags = FLAG_QUARANTINED | FLAG_OFF;
      continue;
    }
    uint8_t f = 0;
    if (m->requires_wifi())  f |= FLAG_REQUIRES_WIFI;
    if (m->requires_ntp())   f |= FLAG_REQUIRES_NTP;
    if (m->runs_during_ota()) f |= FLAG_DURING_OTA;
    s.flags = f;
    m->pre_wifi();
    yield();
  }
  _next_loop_due = now;
}

void corium::Registry::set_safe_mode(bool on) {
  _safe_mode = on;
}

#ifdef CORIUM_CRASH_MARK
// noinit holds junk after power on, only a crash reset is trusted
void corium::Registry::crash_boot(bool crashed) {
  _crashed_in = crashed ? _firing : IN_NONE;
}

// slots are sorted the same each boot
const char* corium::Registry::crashed_in() {
  if (_crashed_in < _count) return _slots[_crashed_in].module->name();
  if (_crashed_in == IN_TCP) return PSTR("tcp");
  if (_crashed_in == IN_PORTAL) return PSTR("portal");
  return nullptr;
}
#endif

bool corium::Registry::quarantined(corium::Module* m) {
  if (!_safe_mode) return false;
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].module == m) return _slots[i].flags & FLAG_QUARANTINED;
  }
  return false;
}
