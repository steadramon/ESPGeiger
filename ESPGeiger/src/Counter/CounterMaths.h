/*
  CounterMaths.h - counting arithmetic, independent of input and I/O.

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

// No clock, prefs or log, no vtable

#ifndef COUNTERMATHS_H
#define COUNTERMATHS_H

#include <stdint.h>
#include <string.h>

#ifndef GEIGER_RATIO
  #define GEIGER_RATIO 151.0  // pref default is STR() of it
#endif

// set by Counter.h
#ifndef GEIGER_DEAD_TIME_DEFAULT
  #define GEIGER_DEAD_TIME_DEFAULT 0
#endif

// P(no counts) = e^-10
#ifndef GEIGER_MISSING_COUNTS
#define GEIGER_MISSING_COUNTS 10
#endif
// longer than missing, drives the banner
#ifndef GEIGER_DEAD_COUNTS
#define GEIGER_DEAD_COUNTS 20
#endif
#define GEIGER_TUBE_DEAD_MAX_S 21600
// uSv/h, until the first count
#ifndef GEIGER_ASSUMED_BG_USVH
#define GEIGER_ASSUMED_BG_USVH 0.1f
#endif
// floor, low cpm setups false alarm
#ifndef GEIGER_TUBE_TIMEOUT_MIN_S
#define GEIGER_TUBE_TIMEOUT_MIN_S 1800
#endif

#define GEIGER_CPM5_COUNT 60
#define GEIGER_CPM15_COUNT 60
// same as a 60 sample avg
#ifndef GEIGER_EMA_FACTOR
#define GEIGER_EMA_FACTOR ((GEIGER_CPM5_COUNT + 1) / 2)
#endif

class CounterMaths {
  public:
    static constexpr uint8_t  HIST_BUCKETS = 25;
    static constexpr uint32_t HIST_BASE_US = 64;
    // x10 dead time, x60 cpm, still an int
    static constexpr float CPS_MAX = (float)(INT32_MAX / 600);

    // floating pin
    static int clamp_counts(int n) {
      if (n < 0) return 0;
      if (n > (int)CPS_MAX) return (int)CPS_MAX;
      return n;
    }

    // int compares, a float compare is a soft-float call
    static int32_t fbits(float v) { int32_t b; memcpy(&b, &v, 4); return b; }

    // NaN and below 0 read 0
    static int to_int(float v) {
      int32_t b = fbits(v);
      if (b <= 0 || b > 0x7F800000) return 0;   // <= 0, NaN
      if (b >= 0x4F000000) return INT32_MAX;     // 2^31
      return (int)(v + 0.5f);
    }

    // cpm x10, 0xFFFF cap
    static uint16_t cps_x600(float cps) {
      int t = to_int(cps * 600.0f);
      return t > 0xFFFF ? 0xFFFF : (uint16_t)t;
    }

    // CPM per uSv/h
    void set_ratio(float ratio) {
      if (!(ratio >= 1.0f)) return;   // pref min, NaN too
      _ratio = ratio;
      _ratio_inv = 1.0f / ratio;
      float bg_cpm = GEIGER_ASSUMED_BG_USVH * ratio;
      uint32_t t = (uint32_t)(GEIGER_DEAD_COUNTS * 60.0f / bg_cpm);
      _tube_timeout_s = (t < GEIGER_TUBE_TIMEOUT_MIN_S) ? GEIGER_TUBE_TIMEOUT_MIN_S : t;
    }
    float get_ratio() const { return _ratio; }

    void     set_dead_time_us(uint16_t us) { _dead_time_us = us; _dead_time_sec = us * 1e-6f; }
    uint16_t get_dead_time_us() const { return _dead_time_us; }

    // Non-paralyzable, 10x cap, off below 50 cps
    float apply_dead_time(float cps) const {
      int32_t b = fbits(cps);
      if (b <= 0 || b > 0x7F800000) return 0.0f;
      if (b >= 0x4A000000 && cps > CPS_MAX) cps = CPS_MAX;   // 2^21 first
      if (_dead_time_us == 0 || cps <= 50.0f) return cps;
      float x = cps * _dead_time_sec;
      if (x > 0.9f) x = 0.9f;
      return cps / (1.0f - x);
    }

    // raw cps only
    bool saturated_at(float cps) const {
      if (_dead_time_us == 0) return false;
      return cps * _dead_time_sec > 0.875f;
    }

    bool tube_alive(uint32_t uptime_s, uint32_t last_count_up_s,
                    uint32_t total_clicks) const {
      if (_ratio <= 0.0f) return true;
      uint32_t silence = uptime_s - last_count_up_s;
      if (silence < _tube_timeout_s) return true;
      if (total_clicks == 0) return false;
      uint32_t obs = (uint32_t)(((uint64_t)GEIGER_DEAD_COUNTS * uptime_s) / total_clicks);
      if (obs > GEIGER_TUBE_DEAD_MAX_S) obs = GEIGER_TUBE_DEAD_MAX_S;
      return silence < obs;
    }

    // capped at tube timeout
    uint32_t missing_threshold_s(uint32_t uptime_s, uint32_t total_clicks) const {
      if (_ratio <= 0.0f) return 0;
      uint32_t thresh = (uint32_t)((GEIGER_MISSING_COUNTS * 60.0f)
                                   / (GEIGER_ASSUMED_BG_USVH * _ratio));
      if (total_clicks > 0) {
        uint32_t obs = (uint32_t)(((uint64_t)GEIGER_MISSING_COUNTS * uptime_s) / total_clicks);
        if (obs > thresh) thresh = obs;
      }
      if (thresh > _tube_timeout_s) thresh = _tube_timeout_s;
      return thresh;
    }

    bool counts_missing(uint32_t uptime_s, uint32_t last_count_up_s,
                        uint32_t total_clicks) const {
      // int guard first
      uint32_t silence = uptime_s - last_count_up_s;
      if (silence < 60) return false;
      if (_ratio <= 0.0f) return false;
      return silence >= missing_threshold_s(uptime_s, total_clicks);
    }

    // cold window reads high
    static bool alarm_over(int cpm, int limit, bool warm) { return warm && limit < cpm; }

    // log2 from 64us, isr, no clz
    __attribute__((always_inline)) static uint8_t interval_bucket(uint32_t us) {
      uint32_t d = us / HIST_BASE_US;
      uint8_t b = 0;
      while (d && b < HIST_BUCKETS - 1) { d >>= 1; b++; }
      return b;
    }

  protected:
    float    _ratio = (float)GEIGER_RATIO;
    float    _ratio_inv = 1.0f / (float)GEIGER_RATIO;
    uint32_t _tube_timeout_s = GEIGER_TUBE_TIMEOUT_MIN_S;
    uint16_t _dead_time_us = GEIGER_DEAD_TIME_DEFAULT;
    float    _dead_time_sec = GEIGER_DEAD_TIME_DEFAULT * 1e-6f;
};

#endif
