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
#include "App/AppConfig.h"
#include "Corium/Util/EGSmoothed.h"
#include "Corium/Output/QuietWindow.h"
#include "Corium/Module/Registry.h"
#include "App/Topics.h"

#include "GeigerInput/GeigerInput.h"
#include "Corium/System/FastMillis.h"

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

#define GEIGER_CPM_COUNT 60

typedef void (*ClickFn)(uint32_t us);

// filled at begin, called from loop
struct ClickFns {
  static constexpr uint8_t MAX = 8;
  ClickFn fn[MAX];
  uint8_t n = 0;
  bool add(ClickFn f);   // false = full
  void fire(uint32_t us) const;
};

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
      int   get_cpm_stable()  const { return to_int(_cached_cps * 60.0f); }
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
      static void CORIUM_ISR_ATTR on_pulse(uint32_t now_us);
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
      // warn, alert or warm changed, from loop()
      static constexpr uint8_t TOPIC_ALARM = Topic::ALARM;
      // a second counted, from loop()
      static constexpr uint8_t TOPIC_COUNTS = Topic::COUNTS;
      bool is_healthy() const { return geigerinput && geigerinput->isHealthy(); }
      GeigerInput* input() { return geigerinput; }
      // web ota keeps the second ticking, no zero seconds
      void stop_for_ota() {
        _running = false;
        if (_lifetime_enabled) save_lifetime();
        if (geigerinput) geigerinput->stopForOTA();
      }
      void restart_after_ota() {
        if (geigerinput) geigerinput->restartAfterOTA();
        _running = true;
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

      void ota_hold(bool on) { _ota = on; }
      // inline gate, every pass: only members, a global costs an l32r on esp32
      inline bool needs_loop(uint32_t now) {
        return !_ota && (_pending_work || _last_blip_seen != _last_blip || geigerinput->due(now));
      }
      // direct, polling merges blips
      void dispatchReceiverBlip();
      bool on_click(ClickFn f) { return _clicks.add(f); }
      // udp receiver, no echo
      bool on_rx_click(ClickFn f) { return _rx_clicks.add(f); }
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
      void pulse_histogram(uint32_t* out) const;   // HIST_BUCKETS, a copy under the lock
      static constexpr uint8_t pulse_histogram_buckets() { return HIST_BUCKETS; }
      CircularBuffer<int,45> cpm_history;
      CircularBuffer<uint16_t,45>* cpm5_history = nullptr;   // tenths
      void want_cpm5_history();
      CircularBuffer<int,24> day_hourly_history;
    private:
      uint16_t _cpm5_x10 = 0;
      void push_cpm5(bool fresh);
      struct RingSnapshot {
        uint32_t newest_us;
        uint32_t oldest_us;
        uint16_t count;
      };
      bool snapshot_ring(RingSnapshot& s) const;
      float cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const;
      void note_counts_missing(uint32_t uptime_s);
      void hour_rollover(time_t now);
      uint32_t clock_read();
      void lifetime_tick(int counts, uint32_t now);
      void life_work(uint32_t now);
      void cpm_work(bool fresh);
      void ema_work(bool add15);
      bool ema_tick();
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
      bool _ota = false;   // ota_in_progress, kept here for the pass
      static constexpr uint8_t PENDING_RTC   = 1 << 0;
      static constexpr uint8_t PENDING_SAVE  = 1 << 1;
      static constexpr uint8_t PENDING_ALARM = 1 << 2;
      static constexpr uint8_t PENDING_COUNTS = 1 << 3;
      uint8_t  _pending_work = 0;
      uint8_t  _alarm_bits = 0;   // warn, alert, warm as last emitted
      uint32_t _rtc_pending_now = 0;
      uint32_t _first_boot_ts = 0;
      uint32_t _rtc_unsaved_clicks = 0;
      uint32_t _rtc_carry_s = 0;
      unsigned long _lifetime_seconds_saved = 0;
      uint32_t      _last_save_time_t = 0;
      uint8_t _cpm_window = GEIGER_CPM_COUNT;
      float   _cpm_window_inv = 1.0f / (float)GEIGER_CPM_COUNT;
      mutable bool _warm_cached = false;
      QuietWindow _quiet;
      float _cached_cps = 0.0f;
      float _cached_cpm_active = 0.0f;
      int   _cached_ccpm = 0;
      CpsInputs _cps_in;
      EGRingAvg<uint32_t, GEIGER_CPM_COUNT> geigerTicks;   // counts, exact sum
#ifdef GEIGER_SMOOTH_AVG
      EGRingAvg<float, GEIGER_CPM5_COUNT>  geigerTicks5;
      EGRingAvg<float, GEIGER_CPM15_COUNT> geigerTicks15;
#else
      EGEma<float> geigerTicks5;
      EGEma<float> geigerTicks15;
#endif
      // static_casts rely on GEIGER_TYPE
      GeigerInput* geigerinput;
      ClickFns _clicks;
      ClickFns _rx_clicks;
};

extern Counter gcounter;
#endif
