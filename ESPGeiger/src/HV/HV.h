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
#include "../Util/Globals.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/EGSmoothed.h"

#ifndef GEIGER_PWMPIN
#ifdef ESPGEIGER_HW
#define GEIGER_PWMPIN 12
#else
#define GEIGER_PWMPIN -1  // Safety: HV disabled until pin explicitly set
#endif
#endif

#ifndef GEIGER_VFEEDBACKPIN
#ifdef ESPGEIGER_HW
#define GEIGER_VFEEDBACKPIN A0
#elif defined(ESP32)
#define GEIGER_VFEEDBACKPIN 36  // ADC1_CH0 (SVP/A0): input-only, ADC1 = no WiFi conflict
#else
#define GEIGER_VFEEDBACKPIN A0  // ESP8266's only ADC input
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
#define GEIGERHW_MAX_FREQ 9000   // espgeigerhw boost circuit goes bistable above ~10 kHz
#elif defined(ESP32)
#define GEIGERHW_MAX_FREQ 80000  // ESP32 LEDC handles much higher with hardware PWM
#else
#define GEIGERHW_MAX_FREQ 40000  // ESP8266 analogWriteFreq documented max
#endif
#endif

#ifndef GEIGERHW_MIN_FREQ
#define GEIGERHW_MIN_FREQ 100   // ESP8266 / ESP32 documented minimum
#endif

#ifndef GEIGERHW_ADC_RATIO
#define GEIGERHW_ADC_RATIO 1035
#endif

#ifndef GEIGERHW_ADC_OFFSET
#define GEIGERHW_ADC_OFFSET 0
#endif

class HV : public EGModule {
    public:
      HV();
      const char* name() override { return "hv"; }
      uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
      // Managed via /hv when feedback exists; falls back to /param otherwise.
#ifdef ESPG_HV_ADC
      uint8_t display_order() override { return 0; }
#else
      uint8_t display_order() override { return 15; }
#endif
      uint16_t warmup_seconds() override { return 0; }
      bool has_loop() override { return true; }
      uint16_t loop_interval_ms() override { return 1000; }
      void loop(unsigned long now) override;
      void fiveloop();
      void begin() override;
      const EGPrefGroup* prefs_group() override;
      void on_prefs_loaded() override;
      const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
      const char* legacy_file() override { return "/espgeigerhw.json"; }  // LEGACY IMPORT
      void set_freq( int freq) {
        if (freq > GEIGERHW_MAX_FREQ) {
          freq = GEIGERHW_MAX_FREQ;
        }
        if (freq < GEIGERHW_MIN_FREQ) {
          freq = GEIGERHW_MIN_FREQ;
        }
      #ifdef ESP8266
        analogWriteFreq(freq);
      #else
        ledcChangeFrequency(0, freq, 8);
      #endif
        _hw_freq = freq;
      };
      int get_freq() {
        return _hw_freq;
      }
      void set_duty( int duty) {
        if (duty > 1023) {
          duty = 1023;
        }
        if (duty < 1) {
          duty = 1;
        }
        _hw_duty = duty;
      };
      int get_duty() {
        return _hw_duty;
      }
      void set_vd_ratio( int ratio) {
        _hw_vd_ratio = ratio;
      };
      void set_vd_offset (int offset) {
        _hw_vd_offset = offset;
      }
      int get_vd_ratio() {
        return _hw_vd_ratio;
      }
      int get_vd_offset() {
        return _hw_vd_offset;
      }
      void saveconfig();  // redirects to EGPrefs::commit
      // Apply freq + duty changes safely: drop PWM low first, then re-arm,
      // so freq transitions don't run with old duty at new freq (spike risk).
      void apply_freq_duty_safe(int new_freq, int new_duty);
      // PWM pin is configured at boot from prefs; takes effect after reboot.
      void set_pwm_pin(int pin) { _pwm_pin = pin; }
      int get_pwm_pin() const { return _pwm_pin; }
      void set_hv_target(uint16_t v) { _hv_target = v; }
      uint16_t get_hv_target() const { return _hv_target; }
      int8_t get_duty_trim() const { return _duty_trim; }
      void reset_trim() { _duty_trim = 0; }
      Smoothed<float> hvReading;
    private:
      int _pwm_pin = GEIGER_PWMPIN;
      int _hw_freq = GEIGERHW_FREQ;
      int _hw_duty = GEIGERHW_DUTY;
      int _cur_duty = 0;
      int _hw_vd_ratio = GEIGERHW_ADC_RATIO;
      int _hw_vd_offset = GEIGERHW_ADC_OFFSET;
      // Closed-loop trim: 0 target = disabled (open-loop). Bounded to ±5 LSB
      // (≈±8 V on this divider) so feedback faults can't run away.
      uint16_t _hv_target = 0;
      int8_t   _duty_trim = 0;
      unsigned long _trim_settle_until = 0;  // suppress trim until this millis()
      static constexpr int8_t TRIM_MAX = 8;
      static constexpr int    TRIM_HYST_V = 4;
      static constexpr uint8_t TRIM_PERIOD_S = 10;
      static constexpr unsigned long TRIM_SETTLE_MS = 30000;
};

extern HV hv;

#endif
