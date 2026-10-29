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
#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "App/Topics.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/EGSmoothed.h"
#include "Corium/Util/MathUtil.h"
#include "HVTrim.h"
#ifdef ESP32
#include <EGLed.h>
#endif

// hw32: the board's hard limits (firmware brief), not prefs
#ifdef ESPGEIGER_HW32
#define GEIGER_PWMPIN 7
#define GEIGER_VFEEDBACKPIN 4
#define GEIGERHW_MAX_FREQ 6000
#define GEIGERHW_DUTY 150           // ~24 us at 6 kHz
#define GEIGERHW_MAX_ON_US 46       // brief's ceiling, bring-up may lower it
#define GEIGERHW_MAX_DUTY 282       // MAX_ON_US at MAX_FREQ
#define GEIGERHW_ADC_RATIO 336      // 328.3 in mV
#define GEIGERHW_OV_V 730
#define GEIGERHW_TRIM_BAND_MIN 3    // volts, the mV ratio gives 0
#define GEIGERHW_FB_LOW_MV 150      // ~50 V while driving: divider open
#define GEIGERHW_FB_HIGH_MV 2400    // ADC top: low leg open
#define GEIGERHW_FB_BAD_S 5
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

#ifdef GEIGERHW_MAX_ON_US
static_assert(GEIGERHW_MAX_DUTY <= GEIGERHW_MAX_ON_US * GEIGERHW_MAX_FREQ * 1024LL / 1000000,
              "GEIGERHW_MAX_DUTY over the on-time cap");
#endif

// volts, 0 = off
#ifndef GEIGERHW_OV_V
#define GEIGERHW_OV_V 0
#endif

#ifndef GEIGERHW_TRIM_BAND_MIN
#define GEIGERHW_TRIM_BAND_MIN 0
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
      const char* legacy_file() override { return "/espgeigerhw.json"; }  // LEGACY IMPORT
      void set_freq(int freq);
      int get_freq() {
        return _hw_freq;
      }
      // keep out of line
      void set_duty(int duty);
      int get_duty() {
        return _hw_duty;
      }
      void set_vd_ratio(int ratio) {
        _hw_vd_ratio = clamp(ratio, 0, 50000);   // 0 = ADC off
      }
      void set_vd_offset(int offset) {
        _hw_vd_offset = clamp(offset, -100, 100);
      }
      int get_vd_ratio() {
        return _hw_vd_ratio;
      }
      int get_vd_offset() {
        return _hw_vd_offset;
      }
      bool saveconfig();
      // Pin low first, never old duty at new freq
      void apply_freq_duty_safe(int new_freq, int new_duty);
      void set_pwm_pin(int pin);   // reboot to apply
      int get_pwm_pin() const { return _pwm_pin; }
      void set_hv_target(uint16_t v) { _hv_target = v; }
      uint16_t get_hv_target() const { return _hv_target; }
      int8_t get_duty_trim() const { return _duty_trim; }
      bool ov_tripped() const { return _ov_trip; }
      // feedback latch, from loop()
      static constexpr uint8_t TOPIC_FAULT = Topic::HV_FAULT;
#ifdef ESPGEIGER_HW32
      bool fb_fault() const { return _fb_fault; }
#else
      bool fb_fault() const { return false; }
#endif
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
      void sample_adc();
      int read_hv();
      void check_overvoltage(int volts);
      void trim_step();
      int max_duty() const;
#ifdef ESPGEIGER_HW32
      void check_feedback();
      bool    _fb_fault = false;   // till restart
      uint8_t _fb_bad_s = 0;
#endif
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
      Deadline _settle;   // fast_millis(), cleared once reached
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
      static constexpr uint16_t LOOP_MS = 1000 / LOOP_SLOW;
};

extern HV hv;

#endif
