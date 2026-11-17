/*
  Registry.cpp - registry of modules

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
#include <new>
#include <string.h>
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
__NOINIT_ATTR uint8_t corium::Registry::_firing;   // crashBoot reads it first
#else
uint8_t corium::Registry::_firing = IN_NONE;
#endif
#ifdef CORIUM_CRASH_MARK
uint8_t corium::Registry::_crashed_in = IN_NONE;
#endif
#ifdef CORIUM_TRACE
#ifdef ESP32
__NOINIT_ATTR uint32_t corium::Registry::_trace[TRACE_N];   // keepTrace reads it first
__NOINIT_ATTR uint8_t corium::Registry::_trace_at;
#else
uint32_t corium::Registry::_trace[TRACE_N];
uint8_t corium::Registry::_trace_at;
#endif
uint32_t* corium::Registry::_traced = nullptr;
#endif
corium::IsrLock::Bits corium::Registry::_posted;
bool corium::Registry::_safe_mode = false;

// keep sorted, loopAll reads [0]
// signed diff, safe over the millis wrap
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

void corium::Registry::beginAll() {
  if (_overflow) {
    corium::Log::console(PSTR("Module: Registry overflow, %u module(s) dropped (raise CORIUM_MAX_MODULES)"), _overflow);
  }
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    if (s.flags & FLAG_QUARANTINED) {
      corium::Log::console(PSTR("Module: Skipping %s (quarantined)"), s.module->name());
      continue;
    }
    if ((s.flags & FLAG_REQUIRES_WIFI) && corium::Wifi::disabled) {
      corium::Log::debug(PSTR("Module: Skipping %s (wifi disabled)"), s.module->name());
      s.flags = (s.flags | FLAG_OFF) & ~FLAG_ARMED;
      _resort = true;
      continue;
    }
    corium::Log::debug(PSTR("Module: Starting %s"), s.module->name());
    s.module->begin();
    yield();
  }
}

inline uint8_t corium::Registry::unmet_requirements() {
  uint8_t unmet = 0;
  if (!corium::Wifi::stableFor(WIFI_SETTLE_MS)) unmet |= FLAG_REQUIRES_WIFI;
  if (!corium::Clock::synced) unmet |= FLAG_REQUIRES_NTP;
  return unmet;
}

// period re-armed first, the loop's own dueAt wins
inline void corium::Registry::fire_slot(uint8_t idx, unsigned long now) {
  Slot& s = _slots[idx];
  s.loop_last = now;
  if (s.period_ms) s.next_due = now + s.period_ms;
  else s.flags &= ~FLAG_ARMED;
  _firing = idx;
#ifdef CORIUM_TRACE
  _trace[_trace_at++ % TRACE_N] = ((uint32_t)now << 8) | idx;
#endif
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
  _firing = IN_NONE;
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
static constexpr uint32_t IDLE_WAKE_MS = 60000;
inline uint32_t corium::Registry::next_wake(unsigned long now) {
  return _due_count ? _slots[_due_order[0]].next_due : now + IDLE_WAKE_MS;
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

// wifi or ntp not there yet, try again at its period or 1 s
static constexpr uint32_t GATE_RETRY_MS = 1000;
static inline uint32_t retry_in(uint32_t period) {
  return period && period < GATE_RETRY_MS ? period : GATE_RETRY_MS;
}

inline void corium::Registry::walk(unsigned long now) {
  uint8_t unmet = unmet_requirements();
  uint32_t t0 = micros();
  uint32_t fired = 0;

  uint32_t t = t0;   // after the last fire
  uint8_t k = 0;
  while (k < _due_count) {
    uint8_t idx = _due_order[k];
    Slot& s = _slots[idx];
    if (!corium::reached(now, s.next_due)) break;
    // once a pass, dueIn(0) runs on the next
    if (fired & (1u << idx)) { k++; continue; }
    if (s.flags & unmet) {
      s.next_due = now + retry_in(s.period_ms);
    } else {
      fired |= 1u << idx;
      uint32_t a = micros();
      fire_slot(idx, now);
      t = micros();
      uint32_t d = t - a;
      if (d > s.peak_us) s.peak_us = d > 0xFFFF ? 0xFFFF : (uint16_t)d;
    }
    if (_resort) { rebuild_due(); k = 0; }
    else if (sift_due(k) == k) k++;
    // the rest next pass
    if (fired && t - t0 >= CORIUM_LOOP_BUDGET_US) break;
  }
}

void corium::Registry::loopAll(unsigned long now) {
  if (_posted.any()) take_posts(now);
  if (_resort) rebuild_due();
  if (!corium::reached(now, _next_loop_due)) return;

  if (corium::otaInProgress) {
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
void corium::Registry::logLoopProfileAndReset() {
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
  if (pos > 0) corium::Log::console(PSTR("Loop total/calls/max: %s"), buf);
  for (uint8_t i = 0; i < _count; i++) {
    _slots[i].max_loop_us = 0;
    _slots[i].total_loop_us = 0;
    _slots[i].loop_calls = 0;
  }
}
#endif

// senders only, name=ok or name=ok/total once one failed
void corium::Registry::logActivityAndReset() {
  char buf[200];
  int pos = 0;
  bool any = false;
  for (uint8_t i = 0; i < _count; i++) {
    corium::Module* m = _slots[i].module;
    corium::Sender* s = m->sender();
    if (!s) continue;
    uint8_t ok = s->takePublishOk();
    uint8_t err = s->takePublishErr();
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
  if (any) corium::Log::console(PSTR("Activity: %s"), buf);
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
  _next_loop_due = corium::fastMillis();
}

bool corium::Registry::every(corium::Module* m, uint32_t ms) {
  Slot* s = slot_of(m);
  if (!s) return false;
  s->period_ms = ms;
  if (!ms) {
    s->flags &= ~FLAG_ARMED;
  } else {
    uint32_t now = corium::fastMillis();
    bool start = !(s->flags & FLAG_ARMED);
    bool overdue = (uint32_t)(now - s->loop_last) >= ms;   // or idle past 2^31 ms
    s->next_due = (start || overdue) ? now : s->loop_last + ms;
    s->flags |= FLAG_ARMED;
  }
  changed(*s);
  return true;
}

bool corium::Registry::dueAt(corium::Module* m, uint32_t t) {
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

bool corium::Registry::heard(uint8_t topic) {
  if (topic >= CORIUM_MAX_TOPICS) return false;
  for (uint8_t i = 0; i < _count; i++) {
    if (_slots[i].topics & (1u << topic)) return true;
  }
  return false;
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

// stable, keeps registration order. _slot follows before any preWifi
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
  for (uint8_t i = 0; i < _count; i++) _slots[i].module->_slot = i;
}

// cached, no virtual calls in loopAll(); nothing armed till a module asks
void corium::Registry::preWifiAll() {
  _firing = IN_NONE;   // no crashBoot, junk stays out
#ifdef CORIUM_TRACE
  memset(_trace, 0xFF, sizeof(_trace));   // slot IN_NONE = empty
  _trace_at = 0;
#endif
  sort_by_priority();

  uint32_t now = corium::fastMillis();
  _posted.take();   // bits were registration order
  _due_count = 0;
  for (uint8_t i = 0; i < _count; i++) {
    Slot& s = _slots[i];
    corium::Module* m = s.module;
    s.loop_last = now;
    s.next_due = now;
    s.period_ms = 0;
    s.topics = 0;
    s.events = 0;
    s.peak_us = 0;
    if (_safe_mode && !m->coreModule()) {
      s.flags = FLAG_QUARANTINED | FLAG_OFF;
      continue;
    }
    uint8_t f = 0;
    if (m->requiresWifi())  f |= FLAG_REQUIRES_WIFI;
    if (m->requiresNtp())   f |= FLAG_REQUIRES_NTP;
    if (m->runsDuringOta()) f |= FLAG_DURING_OTA;
    s.flags = f;
    m->preWifi();
    yield();
  }
  _next_loop_due = now;
}

void corium::Registry::setSafeMode(bool on) {
  _safe_mode = on;
}

#ifdef CORIUM_CRASH_MARK
// noinit holds junk after power on, only a crash reset is trusted
void corium::Registry::crashBoot(uint8_t in) {
  _crashed_in = in;
}

// slots are sorted the same each boot
const char* corium::Registry::crashedIn() {
  if (_crashed_in < _count) return _slots[_crashed_in].module->name();
  if (_crashed_in == IN_TCP) return PSTR("tcp");
  if (_crashed_in == IN_PORTAL) return PSTR("portal");
  return nullptr;
}
#endif

#ifdef CORIUM_TRACE
// oldest first; heap on crash boots only
void corium::Registry::keepTrace() {
  if (!_traced) _traced = new (std::nothrow) uint32_t[TRACE_N];
  if (!_traced) return;
  for (uint8_t i = 0; i < TRACE_N; i++) _traced[i] = _trace[(_trace_at + i) % TRACE_N];
}

const uint32_t* corium::Registry::trace() { return _traced; }
#endif

uint16_t corium::Registry::peakUs(uint8_t idx) {
  return idx < _count ? _slots[idx].peak_us : 0;
}

bool corium::Registry::quarantined(corium::Module* m) {
  if (!_safe_mode || !m) return false;
  uint8_t i = m->_slot;
  return i < _count && (_slots[i].flags & FLAG_QUARANTINED);
}
