/*
  Counter.cpp - Geiger Counter class
  
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

#include <Arduino.h>
#include "Counter.h"
#include "../Logger/Logger.h"

Counter::Counter() {
  status.geiger_model = GEIGER_MODEL;
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
  geigerinput = new GeigerPulse();
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
  geigerinput = new GeigerSerial();
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
  geigerinput = new GeigerTest();
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  geigerinput = new GeigerTestPulse();
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  geigerinput = new GeigerTestSerial();
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
  geigerinput = new GeigerTestPulseInt();
#endif
}

void Counter::secondticker(unsigned long stick_now) {
  geigerinput->secondTicker();

  int eventCounter = geigerinput->collect();
  unsigned long int secidx = (stick_now / 1000) % 60;
  //Log::console(PSTR("Counter: Events - %d - %d"), eventCounter, secidx);
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {
    total_clicks = eventCounter;
    total_clicks_rollover++;
  }

  cpm_history.push(get_cpm());

  if (secidx % 5 == 0) {
    geigerTicks5.add(geigerTicks.get());
  }
  if (secidx % 15 == 0) {
    geigerTicks15.add(geigerTicks5.get());
  }

  time_t currentTime = time (NULL);
  if (currentTime > 0) {
    struct tm *timeinfo = gmtime (&currentTime);
//#ifdef GEIGERTESTMODE
//    if (timeinfo->tm_sec == 0) {
//#else
    if ((timeinfo->tm_min == 0) && (timeinfo->tm_sec == 0)) {
//#endif
      day_hourly_history.push(clicks_hour);
      clicks_hour = 0;
      if (timeinfo->tm_hour == 0) {
        clicks_yesterday = clicks_today;
        clicks_today = 0;
      }
    }
  }

  clicks_today += eventCounter;
  clicks_hour += eventCounter;

  if (_cpm_warning < get_cpm()) {
    _bool_cpm_warning = true;
  } else {
    _bool_cpm_warning = false;
  }
  if (_cpm_alert < get_cpm() ) {
    _bool_cpm_alert = true;
  } else {
    _bool_cpm_alert = false;
  }
}

float Counter::get_cps() {
  return geigerTicks.get();
}

int Counter::get_cpm() {
  return (int)roundf(get_cpmf());
}

float Counter::get_cpmf() {
  return geigerTicks.get()*60.0;
}

int Counter::get_cpm5() {
  return (int)roundf(get_cpm5f());
}

float Counter::get_cpm5f() {
  return geigerTicks5.get()*60.0;
}

int Counter::get_cpm15() {
  return (int)roundf(get_cpm15f());
}

float Counter::get_cpm15f() {
  return geigerTicks15.get()*60.0;
}

void Counter::set_ratio(float ratio) {
  _ratio = ratio;
}

void Counter::set_warning(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 9999)
  {
    val = 9999;
  }
  _cpm_warning = val;
}

void Counter::set_alert(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 9999)
  {
    val = 9999;
  }
  _cpm_alert = val;
}

bool Counter::is_warning() {
  return _bool_cpm_warning;
}

bool Counter::is_alert() {
  return _bool_cpm_alert;
}

float Counter::get_usv() {
  float avgCPM = geigerTicks.get()*60;
  return avgCPM/_ratio;
}

float Counter::get_usv5() {
  float avgCPM = geigerTicks5.get()*60;
  return avgCPM/_ratio;
}

float Counter::get_usv15() {
  float avgCPM = geigerTicks15.get()*60;
  return avgCPM/_ratio;
}

void Counter::begin() {
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), GEIGER_CPM_COUNT, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  geigerTicks.begin(SMOOTHED_AVERAGE, GEIGER_CPM_COUNT);
  geigerTicks5.begin(SMOOTHED_AVERAGE, GEIGER_CPM5_COUNT);
  geigerTicks15.begin(SMOOTHED_AVERAGE, GEIGER_CPM15_COUNT);

  geigerinput->begin();
}

void Counter::blip_led() {
#ifndef DISABLE_INTERNAL_BLIP
    status.led.Blink(20,20);
#endif
#ifdef GEIGER_BLIPLED
    if (status.blip_led.IsRunning() == false) {
      status.blip_led.Blink(2,1).Repeat(1);
    }
#endif

}

void Counter::loop(unsigned long stick_now) {
  geigerinput->loop();

  if (status.last_blip != geigerinput->last_blip()) {
    status.last_blip = geigerinput->last_blip();
#ifndef DISABLE_BLIP
    this->blip_led();
#endif
  }
}