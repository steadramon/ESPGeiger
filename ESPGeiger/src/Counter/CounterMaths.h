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

// Rate, dose and health arithmetic, split from Counter so it can be tested
// without an input type, a clock or a network stack.
//
// Nothing here reads a clock, a pref or a log. Uptime and click totals arrive
// as arguments; Counter supplies them.
//
// Bodies in-class. These run only in the 1 Hz tick, which is cold code where
// a call to another TU costs an instruction fetch. Inlining measured ~30 us
// better on ctr with no LPS cost, despite the header reaching 22 TUs.
//
// Non-virtual base: Counter has no vtable and must not gain one.

#ifndef COUNTERMATHS_H
#define COUNTERMATHS_H

#include <stdint.h>

#ifndef GEIGER_RATIO
  #define GEIGER_RATIO 151.0
#endif

// Counter.h picks this from the input type before including us. Standalone
// (host tests) gets no correction until a caller sets one.
#ifndef GEIGER_DEAD_TIME_DEFAULT
  #define GEIGER_DEAD_TIME_DEFAULT 0
#endif

// Silence equal to this many expected counts reads as "not counting".
// P(zero counts) = e^-10, about 5e-5.
#define GEIGER_MISSING_COUNTS 10
// Assumed background until the tube has counted anything, uSv/h.
#define GEIGER_ASSUMED_BG_USVH 0.1f
// Floor for the dead-tube timeout. Shielded or low-CPM setups false-alarm on
// the bare formula.
#define GEIGER_TUBE_TIMEOUT_MIN_S 1800

class CounterMaths {
  public:
    // CPM per uSv/h. Non-positive values are ignored; also refreshes the
    // dead-tube timeout, which is derived from the ratio.
    void set_ratio(float ratio) {
      if (ratio <= 0) return;
      _ratio = ratio;
      _ratio_inv = 1.0f / ratio;
      uint32_t t = (uint32_t)(12000.0 / (double)ratio);
      _tube_timeout_s = (t < GEIGER_TUBE_TIMEOUT_MIN_S) ? GEIGER_TUBE_TIMEOUT_MIN_S : t;
    }
    float get_ratio() const { return _ratio; }

    void     set_dead_time_us(uint16_t us) { _dead_time_us = us; _dead_time_sec = us * 1e-6f; }
    uint16_t get_dead_time_us() const { return _dead_time_us; }

    uint32_t get_tube_timeout_s() const { return _tube_timeout_s; }

    // Non-paralyzable correction, capped at 10x. Below the skip threshold the
    // input is returned unchanged, so the output steps at that boundary by
    // whatever the correction would have been there. The 50 cps constant is
    // calibrated for the 100 us default; a longer dead time makes the step
    // proportionally larger.
    float apply_dead_time(float cps) const {
      if (_dead_time_us == 0 || cps <= 50.0f) return cps;
      float x = cps * _dead_time_sec;
      if (x > 0.9f) x = 0.9f;
      return cps / (1.0f - x);
    }

    float usv_from_cpm(float cpm) const { return cpm * _ratio_inv; }

    // Raw cps only. Corrected values feed back on themselves near the cap.
    bool saturated_at(float cps) const {
      if (_dead_time_us == 0) return false;
      return cps * _dead_time_sec > 0.875f;
    }

    // Silence shorter than the dead-tube timeout. True when no ratio is set:
    // without one there is no basis to call a tube dead. Silence is measured
    // from the last collected count in uptime seconds, so a tube that has
    // never counted since boot also goes dead once past timeout.
    bool tube_alive(uint32_t uptime_s, uint32_t last_count_up_s) const {
      if (_ratio <= 0.0f) return true;
      return (uptime_s - last_count_up_s) < _tube_timeout_s;
    }

    // Seconds of silence that should be treated as "not counting". Expected
    // rate is the assumed background until the first count, then the observed
    // lifetime rate, so unset ratios and quiet sites self-calibrate. Never
    // exceeds the dead-tube timeout: the advisory must not be slower than the
    // hard latch.
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

    // False when no ratio is set, or below a 60 s floor where no threshold can
    // fire, so the healthy-station path skips the soft-float and 64-bit divides.
    bool counts_missing(uint32_t uptime_s, uint32_t last_count_up_s,
                        uint32_t total_clicks) const {
      if (_ratio <= 0.0f) return false;
      uint32_t silence = uptime_s - last_count_up_s;
      if (silence < 60) return false;
      return silence >= missing_threshold_s(uptime_s, total_clicks);
    }

    // (N-1)/T in Hz across N timestamps spanning span_us. 0 if under two
    // samples or the span collapsed.
    static float cps_from_span(uint16_t count, uint32_t span_us) {
      if (count < 2 || span_us == 0) return 0.0f;
      return (float)(count - 1) * 1.0e6f / (float)span_us;
    }

  protected:
    float    _ratio = GEIGER_RATIO;
    float    _ratio_inv = 1.0f / GEIGER_RATIO;
    uint32_t _tube_timeout_s = GEIGER_TUBE_TIMEOUT_MIN_S;
    uint16_t _dead_time_us = GEIGER_DEAD_TIME_DEFAULT;
    float    _dead_time_sec = GEIGER_DEAD_TIME_DEFAULT * 1e-6f;
};

#endif
