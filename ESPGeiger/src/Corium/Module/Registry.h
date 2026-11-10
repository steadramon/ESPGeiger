/*
  Registry.h -Registry of modules

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
#ifndef CORIUM_REGISTRY_H
#define CORIUM_REGISTRY_H

#include "Module.h"
#include "Corium/System/IsrLock.h"

#ifndef CORIUM_MAX_MODULES
#define CORIUM_MAX_MODULES 32
#endif
static_assert(CORIUM_MAX_MODULES <= 32, "post and profile masks are 32 bits");

// a pass stops starting loops past this
#ifndef CORIUM_LOOP_BUDGET_US
#define CORIUM_LOOP_BUDGET_US 2000
#endif

// topics below CORIUM_TOPIC_APP Corium's, an app numbers its own from it
#define CORIUM_TOPIC_APP 2
#define CORIUM_MAX_TOPICS 16
static_assert(corium::Module::TOPIC_SENT < CORIUM_TOPIC_APP, "Corium topics sit below CORIUM_TOPIC_APP");

// settle after reconnect
#ifndef WIFI_SETTLE_MS
#define WIFI_SETTLE_MS 3000
#endif

// esp32 noinit RAM keeps what the loop was in through a crash reset
#if defined(ESP32) && !defined(CORIUM_CRASH_MARK)
#define CORIUM_CRASH_MARK
#endif

namespace corium {

class Registry {
  public:
    static bool add(Module* m);
    static bool add(Module* m, uint8_t feature);
    static uint16_t features() { return _features; }   // build feature bits, the app numbers them
    static void preWifiAll();
    static void beginAll();
    static void loopAll(unsigned long now);
    static inline bool due(uint32_t now) { return _posted.any() || corium::reached(now, _next_loop_due); }
#ifdef LOOP_PROFILE
    static void logLoopProfileAndReset();
#endif
    static void logActivityAndReset();
    // a module schedules itself, from the loop task
    static bool every(Module* m, uint32_t ms);   // 0 = off
    static bool dueAt(Module* m, uint32_t t);   // once, keeps every()
    static bool dueIn(Module* m, uint32_t ms) { return dueAt(m, corium::fastMillis() + ms); }
    // due now, from any task, not an isr
    static void post(Module* m);

    // topics: emit() posts every module on() it, from the loop task; on() from begin()
    static bool on(uint8_t topic, Module* m);
    static void emit(uint8_t topic);
    static uint16_t events(Module* m);   // bit per topic since the last call
    static bool heard(uint8_t topic);    // anyone on() it

    static void setSafeMode(bool on);   // before preWifiAll
    static bool quarantined(Module* m);

    // Corium's own loop work, between modules
    static constexpr uint8_t IN_TCP = 0xFD, IN_PORTAL = 0xFE, IN_NONE = 0xFF;
#ifdef CORIUM_CRASH_MARK
    // before preWifiAll (it clears), a crash reset keeps where the loop was
    static void crashBoot(bool crashed);
    static const char* crashedIn();   // module name, "tcp", "portal", nullptr = not known
    static inline void in(uint8_t what) { _firing = what; }
#else
    static inline void crashBoot(bool) {}
    static inline const char* crashedIn() { return nullptr; }
    static inline void in(uint8_t) {}
#endif

    static uint8_t count();
    static Module* get(uint8_t idx);
    static Module* find(const char* name);

  private:
    static constexpr uint8_t FLAG_ARMED         = 1 << 0;   // next_due is set
    static constexpr uint8_t FLAG_REQUIRES_WIFI = 1 << 1;
    static constexpr uint8_t FLAG_REQUIRES_NTP  = 1 << 2;
    static constexpr uint8_t FLAG_QUARANTINED   = 1 << 4;
    static constexpr uint8_t FLAG_DURING_OTA    = 1 << 5;
    static constexpr uint8_t FLAG_OFF           = 1 << 6;   // never started
    static bool _safe_mode;

    struct Slot {
      Module* module;
      uint32_t loop_last;
      uint32_t next_due;
      uint32_t period_ms;        // 0 = none
      uint16_t topics;           // on()
      uint16_t events;           // emitted, not taken yet
      uint8_t flags;
#ifdef LOOP_PROFILE
      uint32_t max_loop_us;      // 32 bit, can pass 65 ms
      uint32_t total_loop_us;
      uint16_t loop_calls;
#endif
    };
    static Slot _slots[CORIUM_MAX_MODULES];
    static uint8_t _count;
    static uint8_t _overflow;
    static uint16_t _features;
    static uint32_t _next_loop_due;

    // armed slots, sorted by next_due
    static uint8_t _due_order[CORIUM_MAX_MODULES];
    static uint8_t _due_count;
    static bool _resort;          // a slot changed outside its own loop
    static uint8_t _firing;       // slot in its loop, IN_*, 0xFF = none
#ifdef CORIUM_CRASH_MARK
    static uint8_t _crashed_in;   // _firing at a crash reset
#endif
    static corium::IsrLock::Bits _posted;   // bit per slot
    static void rebuild_due();
    static void sort_by_priority();
    static Slot* slot_of(Module* m);
    static void changed(Slot& s);
    static void take_posts(unsigned long now);

    // one caller each, folds inline
    static inline uint8_t unmet_requirements();
    static inline void walk(unsigned long now);
    static inline void fire_slot(uint8_t idx, unsigned long now);
    static inline uint8_t sift_due(uint8_t k);
    static inline uint32_t next_wake(unsigned long now);
};

// static init, before setup; FEATURE also sets a features() bit
#define CORIUM_REGISTER_MODULE(instance) \
  static bool _reg_##instance = corium::Registry::add(&instance);
#define CORIUM_REGISTER_FEATURE(instance, bit) \
  static bool _reg_##instance = corium::Registry::add(&instance, bit);

}  // namespace corium

#endif
