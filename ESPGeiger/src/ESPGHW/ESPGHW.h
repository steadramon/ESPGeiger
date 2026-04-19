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

#ifndef ESPGHW_H
#define ESPGHW_H
#include <Arduino.h>
#include "../Util/Globals.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/EGSmoothed.h"

#ifndef GEIGER_PWMPIN
#define GEIGER_PWMPIN 12
#endif

#ifndef GEIGER_VFEEDBACKPIN
#define GEIGER_VFEEDBACKPIN A0
#endif

#ifndef GEIGERHW_FREQ
#define GEIGERHW_FREQ 6000
#endif

#ifndef GEIGERHW_DUTY
#define GEIGERHW_DUTY 70
#endif

#ifndef GEIGERHW_MAX_FREQ
#define GEIGERHW_MAX_FREQ 9000
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

class ESPGeigerHW : public EGModule {
    public:
      ESPGeigerHW();
      const char* name() override { return "espghw"; }
      uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
      uint8_t display_order() override { return 0; }  // managed via /hv page
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
        if (duty > 255) {
          duty = 255;
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
        _scale = 0.0009765625032f * (float)ratio;  // precompute for hot path
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
      Smoothed<float> hvReading;
    private:
      int _hw_freq = GEIGERHW_FREQ;
      int _hw_duty = GEIGERHW_DUTY;
      int _cur_duty = 0;
      int _hw_vd_ratio = GEIGERHW_ADC_RATIO;
      int _hw_vd_offset = GEIGERHW_ADC_OFFSET;
      float _scale = 0.0009765625032f * (float)GEIGERHW_ADC_RATIO;  // 1/1024 * ratio
};

extern ESPGeigerHW hardware;

#endif
