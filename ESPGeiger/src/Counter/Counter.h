/*
  Counter.h - Geiger Counter class

  Copyright (C) 2023 @steadramon

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

#ifndef COUNTER_H
#define COUNTER_H
#include <Arduino.h>
#include <CircularBuffer.hpp>
#include "../Corium/System/Globals.h"
#include "../Corium/Util/EGSmoothed.h"
#include "../Corium/Output/QuietWindow.h"

#include "../GeigerInput/GeigerInput.h"
#include "../Corium/System/FastMillis.h"

#if defined(USE_PCNT) && !GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  #define COUNTER_DRAINS_PCNT
#endif

class GeigerUdpRx;

#ifndef GEIGER_DEAD_TIME_US
  #define GEIGER_DEAD_TIME_US 100
#endif

// before CounterMaths.h
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  #define GEIGER_DEAD_TIME_DEFAULT GEIGER_DEAD_TIME_US
#else
  #define GEIGER_DEAD_TIME_DEFAULT 0
#endif

#include "CounterMaths.h"
#include "../Corium/Util/StringUtil.h"

#define GEIGER_CPM_COUNT 60

class Counter : public CounterMaths {
    public:
      Counter();
      void loop();
      float get_cps();
      int get_last_cps() const { return (int)geigerTicks.last(); }
      int get_cpm();
      float get_cpmf();
      float get_cps_stable()  const { return _cached_cps; }
      float get_cpmf_stable() const { return _cached_cps * 60.0f; }
      int   get_cpm_stable()  const { return (int)roundf(_cached_cps * 60.0f); }
      float get_usv_stable()  const { return _cached_cps * 60.0f * _ratio_inv; }
      int get_cpm5();
      float get_cpm5f();
      int get_cpm15();
      float get_cpm15f();
      float get_usv();
      float get_usv5();
      bool  get_tube_alive();
      // soft-float, gate inline
      bool  counts_missing();
      // keep out of line
      int input_pin();
      bool  get_saturated();
      float    get_cps_live();      // (N-1)/T, 0 if N<2
      uint16_t get_cps_n();
      uint32_t get_cps_win_us();
      uint32_t get_min_pulse_us();
      static void IRAM_ATTR on_pulse(uint32_t now_us);
      static void on_pulse_batch(uint16_t count, uint32_t end_us, uint32_t span_us);
      // 0=auto 1=live 2=60s 3=bucket 4=fast
      void    set_cpm_mode(uint8_t m);
      uint8_t get_cpm_mode() const { return _cpm_mode; }
      bool blip_led();
      void begin();
      void secondticker(uint32_t uptime_s);
      void set_cpm_window(uint8_t n) {
        if (n < 1) n = 1;
        if (n > GEIGER_CPM_COUNT) n = GEIGER_CPM_COUNT;
        _cpm_window = n;
        _cpm_window_inv = 1.0f / (float)n;
        _warm_cached = false;
      }
      uint8_t get_cpm_window() const { return _cpm_window; }
      bool is_warm() const;
      void set_rx_pin(int pin) {
        geigerinput->set_rx_pin(pin);
      };
      void set_tx_pin(int pin) {
        geigerinput->set_tx_pin(pin);
      };
      int get_rx_pin() {
        return geigerinput->get_rx_pin();
      };
      int get_tx_pin() {
        return geigerinput->get_tx_pin();
      };
      void set_pcnt_filter(int val) {
        geigerinput->set_pcnt_filter(val);
      };
      void set_debounce(int val) {
        geigerinput->set_debounce(val);
      };
      void apply_pcnt_filter() {
        geigerinput->apply_pcnt_filter();
      };
      void set_pin_pull(int mode) {
        geigerinput->set_pin_pull(mode);
      };
      bool has_pcnt() {
        return geigerinput->has_pcnt();
      };
      void set_warning(int val);
      void set_alert(int val);
      void set_blip_led(bool on) { _blip_led = on; }
      void set_blip_brightness(uint8_t level);
      void set_quiet_hours(const char* from, const char* to);
      bool is_quiet_now();
      bool is_warning() const { return _bool_cpm_warning; }
      bool is_alert() const { return _bool_cpm_alert; }
      bool is_healthy() const { return geigerinput && geigerinput->isHealthy(); }
      GeigerInput* input() { return geigerinput; }
      void stop_for_ota() {
        if (_lifetime_enabled) save_lifetime();
        if (geigerinput) geigerinput->stopForOTA();
      }
      void restart_after_ota() {
        if (geigerinput) geigerinput->restartAfterOTA();
      }
      unsigned long last_blip() {
        return geigerinput->last_blip();
      }
      void reset_alarm();
#if GEIGER_IS_TEST(GEIGER_TYPE)
      void set_target_cpm(float val);
#endif
#if GEIGER_TYPE == GEIGER_TYPE_UDPRX
      GeigerUdpRx* udp_rx();
#endif
      void queueBlip() { _last_blip = micros(); }

      // inline gate
      inline bool needs_loop() {
#if GEIGER_IS_TEST(GEIGER_TYPE) && !GEIGER_IS_SERIAL(GEIGER_TYPE) && !defined(GEIGER_COUNTS_IN_ISR)
        return true;   // drains every pass anyway
#else
        if (_pending_work) return true;
  #ifdef GEIGER_COUNTS_IN_ISR
        return _last_blip_seen != _last_blip;
  #else
        if (!_running) return false;
        if (_last_blip_seen != _last_blip) return true;
    #ifdef COUNTER_DRAINS_PCNT
        return (uint32_t)(fast_millis() - _pcnt_ms) >= PCNT_DRAIN_MS;
    #else
        return geigerinput->poll_due();
    #endif
  #endif
#endif
      }
      // direct, polling merges blips
      void dispatchReceiverBlip();
      unsigned long clicks_hour = 0;
      unsigned long total_clicks_rollover = 0;
      unsigned long total_clicks = 0;
      unsigned long clicks_today = 0;
      unsigned long clicks_yesterday = 0;
      unsigned long total_clicks_lifetime = 0;
      unsigned long total_clicks_lifetime_rollover = 0;
      void set_lifetime_enabled(bool on) { _lifetime_enabled = on; }
      bool get_lifetime_enabled() const { return _lifetime_enabled; }
      void set_lifetime_clicks(unsigned long v) { total_clicks_lifetime = v; }
      void set_lifetime_rollover(unsigned long v) { total_clicks_lifetime_rollover = v; }
      uint64_t get_lifetime_clicks_total() const {
        return ((uint64_t)total_clicks_lifetime_rollover << 32) + total_clicks_lifetime;
      }
      void set_first_boot_ts(uint32_t v) { _first_boot_ts = v; }
      uint32_t get_first_boot_ts() const { return _first_boot_ts; }
      void set_lifetime_seconds(unsigned long v) { _lifetime_seconds_saved = v; }
      unsigned long get_lifetime_seconds() const;
      void save_lifetime();
      void reset_lifetime();
      void clock_stepped(int32_t delta_s);
      const uint32_t* pulse_histogram() const { return _pulse_hist; }
      static constexpr uint8_t pulse_histogram_buckets() { return HIST_BUCKETS; }
      CircularBuffer<int,45> cpm_history;
      CircularBuffer<int,24> day_hourly_history;
    private:
      struct RingSnapshot {
        uint32_t newest_us;
        uint32_t oldest_us;
        uint16_t count;
      };
      bool snapshot_ring(RingSnapshot& s) const;
      float cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const;
      void note_counts_missing(uint32_t uptime_s);
      void hour_rollover(time_t now);
      void lifetime_tick(int counts, time_t now);
      void update_alarms(int cpm);
      uint16_t _boundary_secs = 3600;
      uint8_t _cpm_mode = 3;     // bucket
      unsigned long _last_blip_seen = 0;
      uint32_t _last_count_up_s = 0;   // 0 = none since boot
      static uint32_t _pulse_hist[HIST_BUCKETS];
      uint32_t _prev_blip_us = 0;
      bool     _hist_primed  = false;
      int _cpm_warning = 50;
      int _cpm_alert = 100;
      bool _bool_cpm_warning = false;
      bool _bool_cpm_alert = false;
      bool _alarm_snoozed = false;
      bool _blip_led = true;
      bool _lifetime_enabled = true;
      bool _running = false;
      static constexpr uint8_t PENDING_RTC  = 1 << 0;
      static constexpr uint8_t PENDING_SAVE = 1 << 1;
      uint8_t  _pending_work = 0;
#ifdef COUNTER_DRAINS_PCNT
      static constexpr uint32_t PCNT_DRAIN_MS = 20;   // 50 Hz
      uint32_t _pcnt_ms = 0;
#endif
      uint32_t _rtc_pending_now = 0;
      uint32_t _first_boot_ts = 0;
      uint32_t _rtc_unsaved_clicks = 0;
      unsigned long _lifetime_seconds_saved = 0;
      uint32_t      _last_save_time_t = 0;
      uint8_t _cpm_window = GEIGER_CPM_COUNT;
      float   _cpm_window_inv = 1.0f / (float)GEIGER_CPM_COUNT;
      mutable bool _warm_cached = false;
      QuietWindow _quiet;
      float _cached_cps = 0.0f;
      float _cached_cpm_active = 0.0f;
      EGRingAvg<float, GEIGER_CPM_COUNT> geigerTicks;
#ifdef GEIGER_SMOOTH_AVG
      EGRingAvg<float, GEIGER_CPM5_COUNT>  geigerTicks5;
      EGRingAvg<float, GEIGER_CPM15_COUNT> geigerTicks15;
#else
      EGEma<float> geigerTicks5;
      EGEma<float> geigerTicks15;
#endif
      // static_casts rely on GEIGER_TYPE
      GeigerInput* geigerinput;
};

extern Counter gcounter;
#endif
