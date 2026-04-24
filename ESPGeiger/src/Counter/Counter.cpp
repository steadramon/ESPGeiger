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
#include "../Util/StringUtil.h"
#include "../NTP/NTP.h"

Counter::Counter() {
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
  static uint8_t tick_cnt = 0;
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {
    total_clicks = eventCounter;
    total_clicks_rollover++;
  }

  // Compute once per tick so external accessors are O(1) loads.
  _cached_cps  = geigerTicks.get();
  _cached_cpmf = _cached_cps * 60.0f;
  _cached_usv  = _cached_cpmf * _ratio_inv;
  int ccpm = (int)roundf(_cached_cpmf);

  cpm_history.push(ccpm);

  if (tick_cnt == 0 || tick_cnt == 5 || tick_cnt == 10) {
    geigerTicks5.add(_cached_cps);
  }
  if (tick_cnt == 0) {
    geigerTicks15.add(geigerTicks5.get());
  }
  if (++tick_cnt >= 15) tick_cnt = 0;

  time_t currentTime = time (NULL);
  if (currentTime > 0) {
    // Fire on each new hour (minute in test builds). gmtime() only on transition.
#if GEIGER_IS_TEST(GEIGER_TYPE)
    static time_t lastBoundary = 0;
    time_t thisBoundary = currentTime / 60;
#else
    static time_t lastBoundary = 0;
    time_t thisBoundary = currentTime / 3600;
#endif
    if (thisBoundary != lastBoundary) {
      // Refresh cached UTC offset once per hour (also sets initial value on
      // first tick after NTP sync). mktime with tm_isdst=-1 handles DST.
      // Capture utc_hour before mktime, which may normalise gmt_tm in place.
      struct tm gmt_tm = *gmtime(&currentTime);
      int utc_hour = gmt_tm.tm_hour;
      gmt_tm.tm_isdst = -1;
      ntpclient.tz_offset_min = (int16_t)((currentTime - mktime(&gmt_tm)) / 60);
      if (lastBoundary != 0) {
        day_hourly_history.push(clicks_hour);
        clicks_hour = 0;
        if (utc_hour == 0) {
          clicks_yesterday = clicks_today;
          clicks_today = 0;
        }
      }
      lastBoundary = thisBoundary;
    }
  }

  clicks_today += eventCounter;
  clicks_hour += eventCounter;

  _bool_cpm_warning = (_cpm_warning < ccpm);
  _bool_cpm_alert   = (_cpm_alert   < ccpm);
}

float Counter::get_cps() {
  return _cached_cps;
}

int Counter::get_cpm() {
  return (int)roundf(_cached_cpmf);
}

float Counter::get_cpmf() {
  return _cached_cpmf;
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
  if (ratio > 0) {
    _ratio = ratio;
    _ratio_inv = 1.0f / ratio;                 // keep reciprocal in sync for hot-path multiplies
    _cached_usv = _cached_cpmf * _ratio_inv;   // refresh cache so readers see new ratio immediately
  }
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
  return _cached_usv;
}

float Counter::get_totalusv() {
  float totalUsv = 0;
  unsigned long long uptime = NTP.getUptime ();
  if (uptime < 1) {
    return 0;
  }
  const float inv_factor = 60.0f / uptime;
  totalUsv  = (float)__LONG_MAX__ * inv_factor * (float)total_clicks_rollover;
  totalUsv += total_clicks * inv_factor;
  static constexpr float INV_60 = 1.0f / 60.0f;
  return totalUsv * INV_60 * _ratio_inv;
}

float Counter::get_usv5() {
  return geigerTicks5.get() * 60.0f * _ratio_inv;
}

float Counter::get_usv15() {
  return geigerTicks15.get() * 60.0f * _ratio_inv;
}

void Counter::begin() {
  geigerTicks.begin(SMOOTHED_AVERAGE, GEIGER_CPM_COUNT);
#ifndef GEIGER_SMOOTH_AVG
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:EMA 15:EMA"), GEIGER_CPM_COUNT);
  geigerTicks5.begin(SMOOTHED_EXPONENTIAL, GEIGER_EMA_FACTOR);
  geigerTicks15.begin(SMOOTHED_EXPONENTIAL, GEIGER_EMA_FACTOR);
#else
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), GEIGER_CPM_COUNT, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  geigerTicks5.begin(SMOOTHED_AVERAGE, GEIGER_CPM5_COUNT);
  geigerTicks15.begin(SMOOTHED_AVERAGE, GEIGER_CPM15_COUNT);
#endif

  geigerinput->begin();
  apply_pcnt_filter();
}

void Counter::blip() {
    const bool quiet = is_quiet_now();
#ifndef DISABLE_INTERNAL_BLIP
    if (_blip_led && !quiet) led.Blink(20,20);
#endif
#ifdef GEIGER_BLIPLED
    if (_blip_led && !quiet && blip_led.IsRunning() == false) {
      blip_led.Blink(2,1).Repeat(1);
    }
#endif

}

void Counter::set_quiet_hours(const char* from, const char* to) {
  ParsedTime f = parseTime(from);
  ParsedTime t = parseTime(to);
  _quiet_from_min = f.isValid ? (f.hour * 60 + f.minute) : -1;
  _quiet_to_min   = t.isValid ? (t.hour * 60 + t.minute) : -1;
}

bool Counter::is_quiet_now() {
  // Both ends must parse; either blank = feature off. Also require NTP so we
  // don't silence the LED on unsynced boots (uptime-as-time would be wrong).
  if (_quiet_from_min < 0 || _quiet_to_min < 0)      return false;
  if (_quiet_from_min == _quiet_to_min)              return false;
  if (!ntpclient.synced)                             return false;

  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  int16_t m = t->tm_hour * 60 + t->tm_min;

  // Range crosses midnight when from > to (e.g. 22:00 -> 07:00).
  return (_quiet_from_min < _quiet_to_min)
    ? (m >= _quiet_from_min && m < _quiet_to_min)
    : (m >= _quiet_from_min || m < _quiet_to_min);
}

void Counter::loop() {
  // Only Serial/Test builds need per-iteration UART drain here.
#if GEIGER_IS_SERIAL(GEIGER_TYPE) || GEIGER_IS_TEST(GEIGER_TYPE)
  geigerinput->loop();
#endif

  unsigned long lb = _last_blip;
  if (_last_blip_seen != lb) {
    _last_blip_seen = lb;
#ifndef DISABLE_BLIP
    this->blip();
#endif
  }
}
