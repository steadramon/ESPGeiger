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
#include <CircularBuffer.h>
#include "../Status.h"
#include "../NTP/NTP.h"
#include <Smoothed.h>

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

extern Status status;

#define GEIGER_CPM_COUNT 60
#define GEIGER_CPM5_COUNT 60
#define GEIGER_CPM15_COUNT 60

class Counter {
    public:
      Counter();
      void loop(unsigned long stick_now);
      float get_cps();
      int get_cpm();
      float get_cpmf();
      int get_cpm5();
      float get_cpm5f();
      int get_cpm15();
      float get_cpm15f();
      float get_usv();
      float get_usv5();
      float get_usv15();
      float get_millirem();
      void set_ratio(float ratio);
      float get_ratio() {
        return _ratio;
      };
      void blip_led();
      void begin();
      const char* geiger_model() { return GEIGER_MODEL; };
      void secondticker(unsigned long stick_now);
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
      void set_warning(int val);
      void set_alert(int val);
      bool is_warning();
      bool is_alert();
      unsigned long last_blip() {
        return geigerinput->last_blip();
      }
      void reset_alarm() {
        _bool_cpm_alert = false;
      }
#ifdef GEIGERTESTMODE
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
    private:
      int _cpm_warning = 50;
      int _cpm_alert = 100;
      bool _bool_cpm_warning = false;
      bool _bool_cpm_alert = false;
      float _ratio = GEIGER_RATIO;
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