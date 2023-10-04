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
#define GEIGERHW_DUTY 170
#endif

#ifndef GEIGERHW_MAX_FREQ
#define GEIGERHW_MAX_FREQ 9000
#endif

extern Status status;

class ESPGeigerHW {
    public:
      ESPGeigerHW();
      void loop();
      void fiveloop();
      void begin();
      void set_freq( int freq) {
        if (_hw_freq > GEIGERHW_MAX_FREQ) {
          _hw_freq = GEIGERHW_MAX_FREQ;
        }
#ifdef ESP8266
        analogWriteFreq(_hw_freq);
#else
        ledcChangeFrequency(0, _hw_freq, 8);
#endif
        _hw_freq = freq;
      };
      int get_freq() {
        return _hw_freq;
      }
      void set_duty( int duty) {
        if (_hw_duty > 255) {
          _hw_duty = 255;
        }
        _hw_duty = duty;
      };
      int get_duty() {
        return _hw_duty;
      }
      void saveconfig();
      void loadconfig();
    private:
      int _hw_freq = GEIGERHW_FREQ;
      int _hw_duty = GEIGERHW_DUTY;
};

#endif