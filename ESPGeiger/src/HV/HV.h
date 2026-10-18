/*
  HV.h - HV generator and feedback class

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

#ifndef HV_H
#define HV_H
#include <Arduino.h>
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Util/EGSmoothed.h"
#include "HVTrim.h"
#ifdef ESP32
#include <EGLed.h>
#endif

#ifndef GEIGER_PWMPIN
#ifdef ESPGEIGER_HW
#define GEIGER_PWMPIN 12
#else
#define GEIGER_PWMPIN -1  // HV off until set
#endif
#endif

#ifndef GEIGER_VFEEDBACKPIN
#ifdef ESPGEIGER_HW
#define GEIGER_VFEEDBACKPIN A0
#elif defined(ESP32)
#define GEIGER_VFEEDBACKPIN 36  // ADC1, no wifi conflict
#else
#define GEIGER_VFEEDBACKPIN A0
#endif
#endif

#ifndef GEIGERHW_FREQ
#define GEIGERHW_FREQ 6000
#endif

#ifndef GEIGERHW_DUTY
#define GEIGERHW_DUTY 280
#endif

#ifndef GEIGERHW_MAX_FREQ
#ifdef ESPGEIGER_HW
#define GEIGERHW_MAX_FREQ 9000   // bistable above ~10 kHz
#elif defined(ESP32)
#define GEIGERHW_MAX_FREQ 80000
#else
#define GEIGERHW_MAX_FREQ 40000  // analogWriteFreq max
#endif
#endif

#ifndef GEIGERHW_MIN_FREQ
#define GEIGERHW_MIN_FREQ 100
#endif

#ifndef GEIGERHW_ADC_RATIO
#define GEIGERHW_ADC_RATIO 1035
#endif

#ifndef GEIGERHW_ADC_OFFSET
#define GEIGERHW_ADC_OFFSET 0
#endif

#ifndef GEIGERHW_MAX_V
#define GEIGERHW_MAX_V 700
#endif

// PWM full scale, saved duties are out of this
#define HV_PWM_BITS 10
#define HV_PWM_MAX  1023
static_assert(HV_PWM_MAX == (1 << HV_PWM_BITS) - 1, "HV_PWM_MAX is the top of HV_PWM_BITS");

// coil saturates past this
#ifndef GEIGERHW_MAX_DUTY
#ifdef ESPGEIGER_HW
#define GEIGERHW_MAX_DUTY 600
#else
#define GEIGERHW_MAX_DUTY HV_PWM_MAX
#endif
#endif

// volts, 0 = off
#ifndef GEIGERHW_OV_V
#define GEIGERHW_OV_V 0
#endif

// set espghw.<key> over serial
#ifndef GEIGERHW_TRIM_MAX
#define GEIGERHW_TRIM_MAX 5       // steps either side of saved duty
#endif
#ifndef GEIGERHW_TRIM_S
#define GEIGERHW_TRIM_S 10
#endif
#ifndef GEIGERHW_SETTLE_S
#define GEIGERHW_SETTLE_S 30      // no trim after a change
#endif
#ifndef GEIGERHW_BOOT_S
#define GEIGERHW_BOOT_S 300       // or after boot
#endif
#ifndef GEIGERHW_OV_S
#define GEIGERHW_OV_S 3           // over ov_v before cut-off
#endif

class HV : public EGModule {
    public:
      HV();
      const char* name() override { return PSTR("hv"); }
      uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
#ifdef ESPG_HV_ADC
      uint8_t display_order() override { return 0; }
#else
      uint8_t display_order() override { return 16; }
#endif
      uint16_t warmup_seconds() override { return 0; }
      bool has_loop() override { return true; }
#ifdef ESP8266
      uint16_t loop_interval_ms() override { return 1000 / LOOP_SLOW; }
#else
      uint16_t loop_interval_ms() override { return 1000; }
#endif
      void loop(unsigned long now) override;
      void begin() override;
      const EGPrefGroup* prefs_group() override;
      void on_prefs_loaded() override;
      void registerRoutes(EGHttpServer& http) override;
      const EGMenuEntry* menuEntries() override;
#ifdef ESPG_HV_ADC
      EGMeasureRows measurements() override;
#endif
      EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
      const char* legacy_file() override { return "/espgeigerhw.json"; }  // NOLINT(dram) LEGACY IMPORT
      void set_freq( int freq) {
        if (freq > GEIGERHW_MAX_FREQ) {
          freq = GEIGERHW_MAX_FREQ;
        }
        if (freq < GEIGERHW_MIN_FREQ) {
          freq = GEIGERHW_MIN_FREQ;
        }
      #ifdef ESP8266
        _cur_duty = -1;  // loop() posts freq and duty together
      #else
        ledcChangeFrequency(EGLED_HV_CHANNEL, freq, HV_PWM_BITS);
      #endif
        _hw_freq = freq;
      };
      int get_freq() {
        return _hw_freq;
      }
      // keep out of line
      void set_duty(int duty);
      int get_duty() {
        return _hw_duty;
      }
      void set_vd_ratio(int ratio) {
        // 0 = ADC off
        if (ratio < 0)     ratio = 0;
        if (ratio > 50000) ratio = 50000;
        _hw_vd_ratio = ratio;
      }
      void set_vd_offset(int offset) {
        if (offset < -100) offset = -100;
        if (offset >  100) offset =  100;
        _hw_vd_offset = offset;
      }
      int get_vd_ratio() {
        return _hw_vd_ratio;
      }
      int get_vd_offset() {
        return _hw_vd_offset;
      }
      void saveconfig();
      // Pin low first, never old duty at new freq
      void apply_freq_duty_safe(int new_freq, int new_duty);
      void set_pwm_pin(int pin);   // reboot to apply
      int get_pwm_pin() const { return _pwm_pin; }
      void set_hv_target(uint16_t v) { _hv_target = v; }
      uint16_t get_hv_target() const { return _hv_target; }
      int8_t get_duty_trim() const { return _duty_trim; }
      bool ov_tripped() const { return _ov_trip; }
      void clear_ov();
      void write_duty();
      // release_hold() if the OTA fails
      void hold(bool restarting);
      void release_hold();
      void reset_trim() { _duty_trim = 0; }
      uint16_t get_volts() const { return _hv_prev; }
      EGRingAvg<uint16_t, HV_TRIM_N> hvReading;
    private:
      void check_handler();
      int read_hv();
      void check_overvoltage(int volts);
      void trim_step();
      int _pwm_pin = GEIGER_PWMPIN;
      int _hw_freq = GEIGERHW_FREQ;
      int _hw_duty = GEIGERHW_DUTY;
      int _cur_duty = 0;
      int _hw_vd_ratio = GEIGERHW_ADC_RATIO;
      int _hw_vd_offset = GEIGERHW_ADC_OFFSET;
      uint16_t _hv_target = 0;
      uint16_t _hv_prev = 0;    // 0 = none
      uint16_t _adc[HV_ADC_MAX];
      uint8_t  _adc_n = 0;
      int8_t   _duty_trim = 0;
      unsigned long _trim_settle_until = 0;  // fast_millis()
      bool    _ov_trip = false;
      bool    _held = false;
      uint16_t _ov_v = GEIGERHW_OV_V;
      uint8_t _ov_secs = 0;
      uint8_t _ov_trip_s = GEIGERHW_OV_S;
      uint8_t _trim_max = GEIGERHW_TRIM_MAX;
      uint8_t _trim_s = GEIGERHW_TRIM_S;
      uint16_t _settle_s = GEIGERHW_SETTLE_S;
      uint16_t _boot_s = GEIGERHW_BOOT_S;
#ifdef ESP8266
      static constexpr uint8_t LOOP_SLOW = 10;
      static constexpr uint8_t PWM_RESTARTS = 3;   // per boot, then pin stays low
#else
      static constexpr uint8_t LOOP_SLOW = 1;
#endif
};

extern HV hv;

#endif
