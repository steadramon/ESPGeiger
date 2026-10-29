/*
  EGModuleRegistry.h - Registry of modules

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
#ifndef EGMODULEREGISTRY_H
#define EGMODULEREGISTRY_H

#include "EGModule.h"
#include "Corium/System/IsrLock.h"

#ifndef EG_MAX_MODULES
#define EG_MAX_MODULES 32
#endif
static_assert(EG_MAX_MODULES <= 32, "post and profile masks are 32 bits");

// a pass stops starting loops past this
#ifndef EG_LOOP_BUDGET_US
#define EG_LOOP_BUDGET_US 2000
#endif

// topics below EG_TOPIC_APP Corium's, an app numbers its own from it
#define EG_TOPIC_APP 2
#define EG_MAX_TOPICS 16
static_assert(EGModule::TOPIC_SENT < EG_TOPIC_APP, "Corium topics sit below EG_TOPIC_APP");

// settle after reconnect, AsyncTCP UAF
#ifndef WIFI_SETTLE_MS
#define WIFI_SETTLE_MS 3000
#endif

// esp32 noinit RAM keeps what the loop was in through a crash reset
#if defined(ESP32) && !defined(EG_CRASH_MARK)
#define EG_CRASH_MARK
#endif

class EGModuleRegistry {
  public:
    static bool add(EGModule* m);
    static bool add(EGModule* m, uint8_t feature);
    static uint16_t features() { return _features; }   // build feature bits, the app numbers them
    static void pre_wifi_all();
    static void begin_all();
    static void loop_all(unsigned long now);
    static inline bool due(uint32_t now) { return _posted.any() || reached(now, _next_loop_due); }
#ifdef LOOP_PROFILE
    static void log_loop_profile_and_reset();
#endif
    static void log_activity_and_reset();
    // a module schedules itself, from the loop task
    static bool every(EGModule* m, uint32_t ms);   // 0 = off
    static bool due_at(EGModule* m, uint32_t t);   // once, keeps every()
    static bool due_in(EGModule* m, uint32_t ms) { return due_at(m, fast_millis() + ms); }
    // due now, from any task, not an isr
    static void post(EGModule* m);

    // topics: emit() posts every module on() it, from the loop task; on() from begin()
    static bool on(uint8_t topic, EGModule* m);
    static void emit(uint8_t topic);
    static uint16_t events(EGModule* m);   // bit per topic since the last call

    static void set_safe_mode(bool on);   // before pre_wifi_all
    static bool quarantined(EGModule* m);

    // Corium's own loop work, between modules
    static constexpr uint8_t IN_TCP = 0xFD, IN_PORTAL = 0xFE, IN_NONE = 0xFF;
#ifdef EG_CRASH_MARK
    // before pre_wifi_all (it clears), a crash reset keeps where the loop was
    static void crash_boot(bool crashed);
    static const char* crashed_in();   // module name, "tcp", "portal", nullptr = not known
    static inline void in(uint8_t what) { _firing = what; }
#else
    static inline void crash_boot(bool) {}
    static inline const char* crashed_in() { return nullptr; }
    static inline void in(uint8_t) {}
#endif

    static uint8_t count();
    static EGModule* get(uint8_t idx);
    static EGModule* find(const char* name);

  private:
    static constexpr uint8_t FLAG_ARMED         = 1 << 0;   // next_due is set
    static constexpr uint8_t FLAG_REQUIRES_WIFI = 1 << 1;
    static constexpr uint8_t FLAG_REQUIRES_NTP  = 1 << 2;
    static constexpr uint8_t FLAG_QUARANTINED   = 1 << 4;
    static constexpr uint8_t FLAG_DURING_OTA    = 1 << 5;
    static constexpr uint8_t FLAG_OFF           = 1 << 6;   // never started
    static bool _safe_mode;

    struct Slot {
      EGModule* module;
      uint32_t loop_last;
      uint32_t next_due;
      uint32_t period;           // 0 = none
      uint16_t topics;           // on()
      uint16_t events;           // emitted, not taken yet
      uint8_t flags;
#ifdef LOOP_PROFILE
      uint32_t max_loop_us;      // 32 bit, can pass 65 ms
      uint32_t total_loop_us;
      uint16_t loop_calls;
#endif
    };
    static Slot _slots[EG_MAX_MODULES];
    static uint8_t _count;
    static uint8_t _overflow;
    static uint16_t _features;
    static uint32_t _next_loop_due;

    // armed slots, sorted by next_due
    static uint8_t _due_order[EG_MAX_MODULES];
    static uint8_t _due_count;
    static bool _resort;          // a slot changed outside its own loop
    static uint8_t _firing;       // slot in its loop, IN_*, 0xFF = none
#ifdef EG_CRASH_MARK
    static uint8_t _crashed_in;   // _firing at a crash reset
#endif
    static IsrLock::Bits _posted;   // bit per slot
    static void rebuild_due();
    static void sort_by_priority();
    static Slot* slot_of(EGModule* m);
    static void changed(Slot& s);
    static void take_posts(unsigned long now);

    // one caller each, folds inline
    static inline uint8_t unmet_requirements();
    static inline void walk(unsigned long now);
    static inline void fire_slot(uint8_t idx, unsigned long now);
    static inline uint8_t sift_due(uint8_t k);
    static inline uint32_t next_wake(unsigned long now);
};

#define EG_REGISTER_MODULE(instance) \
  static bool _reg_##instance = EGModuleRegistry::add(&instance);
#define EG_REGISTER_FEATURE(instance, bit) \
  static bool _reg_##instance = EGModuleRegistry::add(&instance, bit);

#endif
