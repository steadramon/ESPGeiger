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
#include "../Util/Globals.h"
#include "../NTP/NTP.h"
#include "../Util/EGSmoothed.h"

#include "../GeigerInput/GeigerInput.h"

#if GEIGER_TYPE == GEIGER_TYPE_PULSE
#include "../GeigerInput/Type/Pulse.h"
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
#include "../GeigerInput/Type/Serial.h"
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
#include "../GeigerInput/Type/UdpRx.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
#include "../GeigerInput/Type/Test.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
#include "../GeigerInput/Type/TestPulse.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
#include "../GeigerInput/Type/TestSerial.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
#include "../GeigerInput/Type/TestPulseInt.h"
#endif

#ifndef GEIGER_RATIO
  #define GEIGER_RATIO 151.0
#endif

#ifndef GEIGER_DEAD_TIME_US
  #define GEIGER_DEAD_TIME_US 100
#endif

#if GEIGER_IS_PULSE(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  #define GEIGER_DEAD_TIME_DEFAULT GEIGER_DEAD_TIME_US
#else
  #define GEIGER_DEAD_TIME_DEFAULT 0
#endif


#include "../Util/StringUtil.h"

extern NTP_Client ntpclient;

#define GEIGER_CPM_COUNT 60

#ifndef GEIGER_SMOOTH_AVG
#ifndef GEIGER_EMA_FACTOR
#define GEIGER_EMA_FACTOR 5
#endif
#else
#define GEIGER_CPM5_COUNT 60
#define GEIGER_CPM15_COUNT 60
#endif

class Counter {
    public:
      Counter();
      void loop();
      float get_cps();
      // Raw count from the most recent second. MightyOhm-style integer protocols.
      int get_last_cps() const { return (int)geigerTicks.last(); }
      int get_cpm();
      float get_cpmf();
      // Bucket-derived, mode-independent. Public fleet posters use these.
      float get_cps_stable()  const { return _cached_cps; }
      float get_cpmf_stable() const { return _cached_cps * 60.0f; }
      int   get_cpm_stable()  const { return (int)roundf(_cached_cps * 60.0f); }
      float get_usv_stable()  const { return _cached_cps * 60.0f * _ratio_inv; }
      // Auto-expiring pause on external posters. 0 ms = resume now.
      static void     pause_external(uint32_t timeout_ms);
      static bool     external_paused();
      static uint32_t pause_remaining_ms();
      int get_cpm5();
      float get_cpm5f();
      int get_cpm15();
      float get_cpm15f();
      float get_totalusv();
      float get_usv();
      float get_usv5();
      float get_usv15();
      float get_millirem();
      // False = no pulse within timeout; tube/HV/wiring fault.
      bool  get_tube_alive();
      // True near dead-time 10x cap (very high rate).
      bool  get_saturated();
      // Ring-derived diagnostics. Zero on counter types that don't feed it.
      float    get_cps_live();      // (N-1)/T from ring, 0 if N<2
      uint16_t get_cps_n();
      uint32_t get_cps_win_us();
      uint32_t get_min_pulse_us();
      // Per-pulse ISR hook (interrupt-driven types).
      static void IRAM_ATTR on_pulse(uint32_t now_us);
      // Batch hook for paths without real timestamps; skips min_pulse_us (synthetic).
      static void on_pulse_batch(uint16_t count, uint32_t end_us, uint32_t span_us);
      // 0=auto blend, 1=live, 2=fixed 60s, 3=bucket (default), 4=adaptive fast.
      void    set_cpm_mode(uint8_t m);
      uint8_t get_cpm_mode() const { return _cpm_mode; }
      void set_ratio(float ratio);
      float get_ratio() {
        return _ratio;
      };
      void blip();
      void begin();
      void secondticker();
      void set_cpm_window(uint8_t n) {
        if (n < 1) n = 1;
        if (n > GEIGER_CPM_COUNT) n = GEIGER_CPM_COUNT;
        _cpm_window = n;
        _cpm_window_inv = 1.0f / (float)n;     // mode 0 blend hot-path
        _warm_cached = false;
      }
      uint8_t get_cpm_window() const { return _cpm_window; }
      // True once warmup + CPM window have elapsed (full sample bucket).
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
      void set_dead_time_us(uint16_t us) { _dead_time_us = us; _dead_time_sec = us * 1e-6f; }
      uint16_t get_dead_time_us() const { return _dead_time_us; }
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
      bool is_warning();
      bool is_alert();
      bool is_healthy() const { return geigerinput && geigerinput->isHealthy(); }
      // Active input as base type for virtual dispatch (/json, /clicks).
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
      // Button snooze; auto-clears when level drops below warning.
      void reset_alarm();
#if GEIGER_IS_TEST(GEIGER_TYPE)
      void set_target_cpm(float val) {
        geigerinput->setTargetCPM(val, true);
      }
#endif
#if GEIGER_TYPE == GEIGER_TYPE_UDPRX
      GeigerUdpRx* udp_rx() { return geigerinput; }
#endif
      // Mark one click event; Counter::loop fans out LED/UdpBlip/Audio/histogram.
      // CPM/total go through on_pulse_batch so multi-pulse batches still bump once.
      void queueBlip() { _last_blip = micros(); }
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
      unsigned long get_lifetime_seconds() const;  // live: saved + uncommitted delta
      void save_lifetime();
      void reset_lifetime();
      const uint32_t* pulse_histogram() const { return _pulse_hist; }
      static constexpr uint8_t pulse_histogram_buckets() { return HIST_BUCKETS; }
      CircularBuffer<int,45> cpm_history;
      CircularBuffer<int,24> day_hourly_history;
    private:
      // Ring snapshot for cooperative-context readers.
      struct RingSnapshot {
        uint32_t newest_us;
        uint32_t oldest_us;
        uint16_t count;
      };
      bool snapshot_ring(RingSnapshot& s) const;
      // Walks ring backwards, returns (N-1)/T over the smaller of: max_age_us
      // wall-clock window, max_pulses entries. Returns 0 if N<2. Backs the
      // mode 2 (fixed_60s) and mode 4 (adaptive_fast) dispatch paths.
      float cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const;
      // Shared dead-time correction (non-paralyzable model, capped near 10x).
      // Applied at the get_cps() dispatcher so all modes match bucket semantics.
      float apply_dead_time(float cps) const;
      uint8_t _cpm_mode = 3;     // 3 = bucket; matches legacy behaviour
      unsigned long _last_blip_seen = 0;
      // Inter-pulse-interval log2 histogram: bucket b covers [64us<<(b-1), 64us<<b),
      // bucket 0 = <64us, bucket 24 saturates. Updated in Counter::loop on each click.
      static constexpr uint8_t HIST_BUCKETS = 25;
      uint32_t _pulse_hist[HIST_BUCKETS] = {0};
      uint32_t _prev_blip_us = 0;
      bool     _hist_primed  = false;
      int _cpm_warning = 50;
      int _cpm_alert = 100;
      bool _bool_cpm_warning = false;
      bool _bool_cpm_alert = false;
      bool _alarm_snoozed = false;   // set by button, auto-clears below warning
      bool _blip_led = true;
      bool _lifetime_enabled = true;
      // Bit 0 = RTC stash due, bit 1 = save_lifetime() due. Single byte check
      // in loop() defers both writes out of tick_max.
      static constexpr uint8_t PENDING_RTC  = 1 << 0;
      static constexpr uint8_t PENDING_SAVE = 1 << 1;
      uint8_t  _pending_work = 0;
      uint32_t _rtc_pending_now = 0;     // currentTime snapshot for pending stash
      uint32_t _first_boot_ts = 0;
      uint32_t _rtc_unsaved_clicks = 0;
      unsigned long _lifetime_seconds_saved = 0;
      uint32_t      _last_save_time_t = 0;
      uint8_t _cpm_window = GEIGER_CPM_COUNT;
      float   _cpm_window_inv = 1.0f / (float)GEIGER_CPM_COUNT;   // for mode 0 blend
      mutable bool _warm_cached = false;
      int16_t _quiet_from_min = -1;  // minutes since midnight; -1 = disabled
      int16_t _quiet_to_min   = -1;
      float _ratio = GEIGER_RATIO;
      float _ratio_inv = 1.0f / GEIGER_RATIO;
      // Default to the 30 min floor; set_ratio() refines for high-sensitivity tubes.
      uint32_t _tube_timeout_us = 1800000000UL;
      uint16_t _dead_time_us = GEIGER_DEAD_TIME_DEFAULT;
      float    _dead_time_sec = GEIGER_DEAD_TIME_DEFAULT * 1e-6f;
      float _cached_cps = 0.0f;     // updated each tick, dead-time corrected
      EGRingAvg<float, GEIGER_CPM_COUNT> geigerTicks;
#ifdef GEIGER_SMOOTH_AVG
      EGRingAvg<float, GEIGER_CPM5_COUNT>  geigerTicks5;
      EGRingAvg<float, GEIGER_CPM15_COUNT> geigerTicks15;
#else
      EGEma<float> geigerTicks5;
      EGEma<float> geigerTicks15;
#endif
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
      GeigerPulse* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
      GeigerSerial* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
      GeigerUdpRx* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
      GeigerTest* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
      GeigerTestPulse* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
      GeigerTestSerial* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
      GeigerTestPulseInt* geigerinput;
#endif
};
#endif
