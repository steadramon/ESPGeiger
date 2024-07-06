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
#include "../Status.h"

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

#ifndef GEIGERHW_ADC_RATIO
#define GEIGERHW_ADC_RATIO 1035
#endif

#ifndef GEIGERHW_ADC_OFFSET
#define GEIGERHW_ADC_OFFSET 0
#endif

extern Status status;

class ESPGeigerHW {
    public:
      ESPGeigerHW();
      void loop();
      void fiveloop();
      void begin();
      void set_freq( int freq) {
        if (freq > GEIGERHW_MAX_FREQ) {
          freq = GEIGERHW_MAX_FREQ;
        }
        if (freq < 100) {
          freq = 100;
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
      void saveconfig();
      void loadconfig();
    private:
      int _hw_freq = GEIGERHW_FREQ;
      int _hw_duty = GEIGERHW_DUTY;
      int _cur_duty = 0;
      int _hw_vd_ratio = GEIGERHW_ADC_RATIO;
      int _hw_vd_offset = GEIGERHW_ADC_OFFSET;
};

#endif