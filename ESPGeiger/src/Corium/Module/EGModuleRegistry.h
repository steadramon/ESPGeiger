/*
  EGModuleRegistry.h - Registry of ESPGeiger modules

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

#ifndef EG_MAX_MODULES
#define EG_MAX_MODULES 32
#endif
#if defined(TICK_PROFILE) || defined(LOOP_PROFILE)
static_assert(EG_MAX_MODULES <= 32, "profile done mask is 32 bits");
#endif

// settle after reconnect, AsyncTCP UAF
#ifndef WIFI_SETTLE_MS
#define WIFI_SETTLE_MS 3000
#endif

class EGModuleRegistry {
  public:
    static bool add(EGModule* m);
    static void pre_wifi_all();
    static void begin_all();
    static void loop_all(unsigned long now);
    static void tick_all(unsigned long now, unsigned long uptime_seconds);
    static inline uint32_t next_loop_due() { return _next_loop_due; }
#ifdef TICK_PROFILE
    static void log_profile_and_reset();
#endif
#ifdef LOOP_PROFILE
    static void log_loop_profile_and_reset();
#endif
    static void log_activity_and_reset();
    static void wake();
    // <0 = off, else ms 1-65535. Any task
    static bool set_loop_interval(EGModule* m, int32_t interval_ms);
    static bool sleep_until(EGModule* m, unsigned long now, unsigned long target_ms);
    static bool set_tick_enabled(EGModule* m, bool enabled);

    static void set_safe_mode(bool on);   // before pre_wifi_all
    static bool quarantined(EGModule* m);

    static uint32_t initial_offset(const char* mod_name, uint32_t interval_ms);
    // wall clock once ntp synced
    static unsigned long initial_ping(const char* mod_name, unsigned long now,
                                      uint32_t interval_ms);
    static unsigned long ping_from_offset(uint32_t offset, unsigned long now,
                                          uint32_t interval_ms);
    static uint8_t count();
    static EGModule* get(uint8_t idx);
    static EGModule* find(const char* name);
    static bool has(const char* name) { return find(name) != nullptr; }

    static size_t collect_status_json(char* buf, size_t cap, unsigned long now);

  private:
    static constexpr uint8_t FLAG_HAS_LOOP      = 1 << 0;
    static constexpr uint8_t FLAG_REQUIRES_WIFI = 1 << 1;
    static constexpr uint8_t FLAG_REQUIRES_NTP  = 1 << 2;
    static constexpr uint8_t FLAG_HAS_TICK      = 1 << 3;
    static constexpr uint8_t FLAG_QUARANTINED   = 1 << 4;
    static constexpr uint8_t FLAG_DURING_OTA    = 1 << 5;
    static bool _safe_mode;

    struct Slot {
      EGModule* module;
      uint32_t loop_last;
      uint32_t next_due;
      uint16_t loop_interval;
      uint16_t warmup_seconds;
      uint8_t flags;
      uint8_t category;
      int32_t pending_ms;        // NO_REQUEST if none
#ifdef TICK_PROFILE
      uint16_t max_tick_us;
#endif
#ifdef LOOP_PROFILE
      uint32_t max_loop_us;      // 32 bit, can pass 65 ms
      uint32_t total_loop_us;
      uint16_t loop_calls;
#endif
    };
    static Slot _slots[EG_MAX_MODULES];
    static uint8_t _count;
    static uint8_t _overflow;
    static uint32_t _next_loop_due;

    // sorted by next_due
    static uint8_t _due_order[EG_MAX_MODULES];
    static uint8_t _due_count;
    static void rebuild_due();
    static void sort_by_priority();

    // one caller each, folds inline
    static inline uint8_t unmet_requirements();
    static inline void fire_slot(Slot& s, unsigned long now);
    static inline uint8_t sift_due(uint8_t k);
    static inline uint32_t next_wake(unsigned long now);
    static inline void tick_slot(Slot& s, unsigned long now_s);

    // RMW, lock on esp32
    static constexpr int32_t NO_REQUEST = INT32_MIN;
    static EG_XTASK_VOLATILE bool _pending;
    static void apply_pending();
#ifdef ESP32
  public:
    static portMUX_TYPE& _mux() {
      static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
      return m;
    }
#endif
};

#ifdef ESP32
#define EGREG_LOCK()   portENTER_CRITICAL(&EGModuleRegistry::_mux())
#define EGREG_UNLOCK() portEXIT_CRITICAL(&EGModuleRegistry::_mux())
#else
#define EGREG_LOCK()
#define EGREG_UNLOCK()
#endif

#define EG_REGISTER_MODULE(instance) \
  static bool _reg_##instance = EGModuleRegistry::add(&instance);

#endif
