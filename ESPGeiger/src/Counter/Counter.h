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

#ifdef ESPGEIGER_HW
#ifndef GEIGER_BLIPLED
#define GEIGER_BLIPLED 15
#endif
#endif

#include "../GeigerInput/GeigerInput.h"

#if GEIGER_TYPE == GEIGER_TYPE_PULSE
#include "../GeigerInput/Type/Pulse.h"
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
#include "../GeigerInput/Type/Serial.h"
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
      int get_cpm();
      float get_cpmf();
      int get_cpm5();
      float get_cpm5f();
      int get_cpm15();
      float get_cpm15f();
      float get_totalusv();
      float get_usv();
      float get_usv5();
      float get_usv15();
      float get_millirem();
      void set_ratio(float ratio);
      float get_ratio() {
        return _ratio;
      };
      void blip();
      void begin();
      void secondticker(unsigned long stick_now);
      void set_cpm_window(uint8_t n) {
        if (n < 1) n = 1;
        if (n > GEIGER_CPM_COUNT) n = GEIGER_CPM_COUNT;
        _cpm_window = n;
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
      void set_blip_brightness(uint8_t level) { led.MaxBrightness(level); }
      void set_quiet_hours(const char* from, const char* to);
      bool is_quiet_now();
      bool is_warning();
      bool is_alert();
      bool is_healthy() const { return geigerinput && geigerinput->isHealthy(); }
      void stop_for_ota() { if (geigerinput) geigerinput->stopForOTA(); }
      unsigned long last_blip() {
        return geigerinput->last_blip();
      }
      void reset_alarm() {
        // Todo
        //_bool_cpm_alert = false;
      }
#if GEIGER_IS_TEST(GEIGER_TYPE)
      void set_target_cpm(float val) {
        geigerinput->setTargetCPM(val, true);
      }
#endif
      unsigned long clicks_hour = 0;
      unsigned long total_clicks_rollover = 0;
      unsigned long total_clicks = 0;
      unsigned long clicks_today = 0;
      unsigned long clicks_yesterday = 0;
      CircularBuffer<int,45> cpm_history;
      CircularBuffer<int,24> day_hourly_history;
#ifdef GEIGER_BLIPLED
      JLed blip_led = JLed(GEIGER_BLIPLED).Stop();
#endif
    private:
      unsigned long _last_blip_seen = 0;
      int _cpm_warning = 50;
      int _cpm_alert = 100;
      bool _bool_cpm_warning = false;
      bool _bool_cpm_alert = false;
      bool _blip_led = true;
      uint8_t _cpm_window = GEIGER_CPM_COUNT;
      int16_t _quiet_from_min = -1;  // minutes since midnight; -1 = disabled
      int16_t _quiet_to_min   = -1;
      float _ratio = GEIGER_RATIO;
      float _ratio_inv = 1.0f / GEIGER_RATIO;   // reciprocal, kept in sync in set_ratio
      // Cached once per tick so accessors are O(1).
      float _cached_cps  = 0.0f;
      float _cached_cpmf = 0.0f;
      float _cached_usv  = 0.0f;
      Smoothed <float> geigerTicks;
      Smoothed <float> geigerTicks5;
      Smoothed <float> geigerTicks15;
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
      GeigerPulse* geigerinput;
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
      GeigerSerial* geigerinput;
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