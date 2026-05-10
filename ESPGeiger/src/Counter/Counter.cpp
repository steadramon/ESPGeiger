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
#include "../Util/MathUtil.h"
#include "../Util/PinSafety.h"
#include "../Util/DeviceInfo.h"
#include "../NTP/NTP.h"
#include "../GRNG/GRNG.h"

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
  if (eventCounter > 0) {
    GRNG::mix((uint32_t)geigerinput->last_blip() ^ ESP.getCycleCount());
  }
  static uint8_t tick_cnt = 0;
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {
    total_clicks = eventCounter;
    total_clicks_rollover++;
  }

  _cached_cps  = geigerTicks.get();
  if (_dead_time_us > 0 && _cached_cps > 50.0f) {
    float x = _cached_cps * _dead_time_sec;
    _cached_cps *= 1.0f + x * (1.0f + x);
  }
  _cached_cpmf = _cached_cps * 60.0f;
  _cached_usv  = _cached_cpmf * _ratio_inv;
  int ccpm = (int)roundf(_cached_cpmf);

  cpm_history.push(ccpm);

  if (tick_cnt == 0 || tick_cnt == 5 || tick_cnt == 10) {
    geigerTicks5.add(_cached_cps);
    float cps5 = geigerTicks5.get();
    _cached_cpm5f = cps5 * 60.0f;
    if (tick_cnt == 0) {
      geigerTicks15.add(cps5);
      _cached_cpm15f = geigerTicks15.get() * 60.0f;
    }
  }
  if (++tick_cnt >= 15) tick_cnt = 0;

  time_t currentTime = time (NULL);
  if (currentTime > 0) {
    // Fire on each new hour (minute in test builds). gmtime() only on transition.
    // Cache next-fire time so we don't divide every tick — one-time alignment
    // divide on first sync, then pure compare-and-add.
#if GEIGER_IS_TEST(GEIGER_TYPE)
    constexpr time_t kBoundarySecs = 60;
#else
    constexpr time_t kBoundarySecs = 3600;
#endif
    static time_t nextBoundary = 0;
    static bool boundaryArmed = false;
    bool fire = false;
    if (nextBoundary == 0) {
      // First call after sync — align to wall-clock boundary; fire once for tz refresh.
      nextBoundary = currentTime - (currentTime % kBoundarySecs) + kBoundarySecs;
      fire = true;
    } else if (currentTime >= nextBoundary) {
      fire = true;
      // Advance past any missed boundaries (e.g. NTP step forward).
      while (currentTime >= nextBoundary) nextBoundary += kBoundarySecs;
    }
    if (fire) {
      // Refresh cached UTC offset (also sets initial value on first sync).
      // mktime with tm_isdst=-1 handles DST. Capture utc_hour before mktime,
      // which may normalise gmt_tm in place.
      struct tm gmt_tm = *gmtime(&currentTime);
      int utc_hour = gmt_tm.tm_hour;
      gmt_tm.tm_isdst = -1;
      ntpclient.tz_offset_min = (int16_t)((currentTime - mktime(&gmt_tm)) / 60);
      if (boundaryArmed) {
        day_hourly_history.push(clicks_hour);
        clicks_hour = 0;
        if (utc_hour == 0) {
          clicks_yesterday = clicks_today;
          clicks_today = 0;
        }
      }
      boundaryArmed = true;
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
  return (int)roundf(_cached_cpm5f);
}

float Counter::get_cpm5f() {
  return _cached_cpm5f;
}

int Counter::get_cpm15() {
  return (int)roundf(_cached_cpm15f);
}

float Counter::get_cpm15f() {
  return _cached_cpm15f;
}

void Counter::set_ratio(float ratio) {
  if (ratio > 0) {
    _ratio = ratio;
    _ratio_inv = 1.0f / ratio;                 // keep reciprocal in sync for hot-path multiplies
    _cached_usv = _cached_cpmf * _ratio_inv;   // refresh cache so readers see new ratio immediately
  }
}

void Counter::set_warning(int val) {
  _cpm_warning = clamp(val, 0, 9999);
}

void Counter::set_alert(int val) {
  _cpm_alert = clamp(val, 0, 9999);
}

bool Counter::is_warm() const {
  if (_warm_cached) return true;
  if (DeviceInfo::uptime() >= (uint32_t)(ESPG_WARMUP_S + _cpm_window)) {
    _warm_cached = true;
    return true;
  }
  return false;
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
  unsigned long long uptime = NTP.getUptime ();
  if (uptime < 1) {
    return 0;
  }
  const float inv_uptime = 1.0f / uptime;
  float totalUsv  = (float)__LONG_MAX__ * inv_uptime * (float)total_clicks_rollover;
  totalUsv       += total_clicks * inv_uptime;
  return totalUsv * _ratio_inv;
}

float Counter::get_usv5() {
  return _cached_cpm5f * _ratio_inv;
}

float Counter::get_usv15() {
  return _cached_cpm15f * _ratio_inv;
}

void Counter::begin() {
  geigerTicks.begin(SMOOTHED_AVERAGE, _cpm_window);
#ifndef GEIGER_SMOOTH_AVG
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:EMA 15:EMA"), _cpm_window);
  geigerTicks5.begin(SMOOTHED_EXPONENTIAL, GEIGER_EMA_FACTOR);
  geigerTicks15.begin(SMOOTHED_EXPONENTIAL, GEIGER_EMA_FACTOR);
#else
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), _cpm_window, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  geigerTicks5.begin(SMOOTHED_AVERAGE, GEIGER_CPM5_COUNT);
  geigerTicks15.begin(SMOOTHED_AVERAGE, GEIGER_CPM15_COUNT);
#endif

  geigerinput->begin();
  apply_pcnt_filter();
}

void Counter::blip() {
    if (!_blip_led) return;
    if (is_quiet_now()) return;
#ifndef DISABLE_INTERNAL_BLIP
  #if defined(HAS_EXT_BLIP) && !defined(GEIGER_BLIPLED)
    if (!ext_blip_led)
  #endif
    led.Blink(20, 20);
#endif
#ifdef GEIGER_BLIPLED
    if (!blip_led.IsRunning()) blip_led.Blink(2, 1).Repeat(1);
#elif defined(HAS_EXT_BLIP)
    if (ext_blip_led && !ext_blip_led->IsRunning()) ext_blip_led->Blink(ext_blip_pulse_ms, 1).Repeat(1);
#endif
}

void Counter::set_blip_brightness(uint8_t level) {
  led.MaxBrightness(level);
#if !defined(GEIGER_BLIPLED) && defined(HAS_EXT_BLIP)
  if (ext_blip_led) ext_blip_led->MaxBrightness(level);
#endif
}

#if !defined(GEIGER_BLIPLED) && defined(HAS_EXT_BLIP)
void Counter::set_ext_blip_pin(int pin) {
  if (const char* why = PinSafety::unsafe_output(pin)) {
    Log::console(PSTR("LED: blip_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  if (ext_blip_led) {
    ext_blip_led->Off().Update();
    delete ext_blip_led;
    ext_blip_led = nullptr;
  }
  if (pin >= 0) {
    ext_blip_led = new JLed(pin);
    ext_blip_led->Stop();
  }
}
#endif

static unsigned long s_quiet_recompute_ms = 0;
static bool s_quiet_cached = false;

void Counter::set_quiet_hours(const char* from, const char* to) {
  ParsedTime f = parseTime(from);
  ParsedTime t = parseTime(to);
  _quiet_from_min = f.isValid ? (f.hour * 60 + f.minute) : -1;
  _quiet_to_min   = t.isValid ? (t.hour * 60 + t.minute) : -1;
  s_quiet_recompute_ms = 0;
}

bool Counter::is_quiet_now() {
  // Both ends must parse; either blank = feature off. Also require NTP so we
  // don't silence the LED on unsynced boots (uptime-as-time would be wrong).
  if (_quiet_from_min < 0 || _quiet_to_min < 0)      return false;
  if (_quiet_from_min == _quiet_to_min)              return false;
  if (!ntpclient.synced)                             return false;

  unsigned long now_ms = millis();
  if ((long)(now_ms - s_quiet_recompute_ms) < 0) return s_quiet_cached;

  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  if (!t) return false;
  int16_t m = t->tm_hour * 60 + t->tm_min;

  s_quiet_cached = (_quiet_from_min < _quiet_to_min)
    ? (m >= _quiet_from_min && m < _quiet_to_min)
    : (m >= _quiet_from_min || m < _quiet_to_min);
  s_quiet_recompute_ms = now_ms + (60UL - t->tm_sec) * 1000UL;
  return s_quiet_cached;
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
