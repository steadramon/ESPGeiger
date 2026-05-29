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
#include <EGHttpServer.h>
#include "Counter.h"
#include "../Logger/Logger.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/StringUtil.h"
#include "../Util/MathUtil.h"
#include "../Util/PinSafety.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/TickProfile.h"
#include "../NTP/NTP.h"
#include "../GRNG/GRNG.h"
#include "../WebPortal/WebPortal.h"
#include "../UdpBlip/UdpBlip.h"
#include "../EnvSensor/EnvSensor.h"
#ifdef AUDIO_TICK
#include "../AudioTick/AudioTick.h"
#endif
#include "HIST_JS.gz.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

extern long start;  // boot epoch ref from main

// RTC stash for clicks + tracked-seconds since last flash save. Word 48 =
// OTA-safe region, clear of CrashDump (0..41). Folded into flash on boot.
#ifdef ESP8266
namespace {
constexpr uint32_t LIFE_RTC_OFFSET_WORDS = 48;
constexpr uint32_t LIFE_RTC_MAGIC = 0xCAFEC11D;  // bumped: layout grew delta_seconds
struct LifeRtc { uint32_t magic; uint32_t delta_clicks; uint32_t delta_seconds; };

void rtc_life_stash(uint32_t delta_clicks, uint32_t delta_seconds) {
  LifeRtc r{LIFE_RTC_MAGIC, delta_clicks, delta_seconds};
  ESP.rtcUserMemoryWrite(LIFE_RTC_OFFSET_WORDS, (uint32_t*)&r, sizeof(r));
}
bool rtc_life_recover_and_clear(uint32_t* out_clicks, uint32_t* out_seconds) {
  LifeRtc r{};
  *out_clicks = 0; *out_seconds = 0;
  if (!ESP.rtcUserMemoryRead(LIFE_RTC_OFFSET_WORDS, (uint32_t*)&r, sizeof(r))) return false;
  if (r.magic != LIFE_RTC_MAGIC) return false;
  uint32_t zero[3] = {0, 0, 0};
  ESP.rtcUserMemoryWrite(LIFE_RTC_OFFSET_WORDS, zero, sizeof(zero));
  *out_clicks  = r.delta_clicks;
  *out_seconds = r.delta_seconds;
  return true;
}
}
#else
static inline void rtc_life_stash(uint32_t, uint32_t) {}
static inline bool rtc_life_recover_and_clear(uint32_t* c, uint32_t* s) { *c=0; *s=0; return false; }
#endif

Counter::Counter() {
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
  geigerinput = new GeigerPulse();
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
  geigerinput = new GeigerSerial();
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
  geigerinput = new GeigerUdpRx();
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
  if (ntpclient.synced) {
    // Fire on each new hour (minute in test builds). gmtime() only on transition.
    // Cache next-fire time so we don't divide every tick - one-time alignment
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
      // First call after sync - align to wall-clock boundary; fire once for tz refresh.
      nextBoundary = currentTime - (currentTime % kBoundarySecs) + kBoundarySecs;
      fire = true;
    } else if (currentTime >= nextBoundary) {
      fire = true;
      // Advance past any missed boundaries (e.g. NTP step forward).
      while (currentTime >= nextBoundary) nextBoundary += kBoundarySecs;
    }
    if (fire) {
      // Track day index (epoch / 86400) so day rollover fires even when a
      // catch-up jump (NTP step) skips past midnight without landing on it.
      static uint32_t lastDay = 0;
      uint32_t curDay = (uint32_t)(currentTime / 86400);
      if (boundaryArmed) {
        day_hourly_history.push(clicks_hour);
      }
      clicks_hour = 0;
      if (curDay != lastDay) {
        if (boundaryArmed) clicks_yesterday = clicks_today;
        clicks_today = 0;
      }
      lastDay = curDay;
      boundaryArmed = true;
    }
  }

  clicks_today += eventCounter;
  clicks_hour += eventCounter;

  if (_lifetime_enabled) {
    unsigned long prev_lifetime = total_clicks_lifetime;
    total_clicks_lifetime += eventCounter;
    if (prev_lifetime > total_clicks_lifetime) {
      total_clicks_lifetime = eventCounter;
      total_clicks_lifetime_rollover++;
    }
    if (eventCounter > 0) {
      _rtc_unsaved_clicks += (uint32_t)eventCounter;
    }
    if (_last_save_time_t == 0 && ntpclient.synced) {
      _last_save_time_t = (uint32_t)currentTime;
    }
    // Tick schedules, loop() does the writes. Keeps the ~100us RTC stash
    // and the ~ms save_lifetime() flash write out of tick_max.
    static uint32_t s_rtc_ctr = 0;
    if (++s_rtc_ctr >= 30) {
      s_rtc_ctr = 0;
      _rtc_pending_now = (uint32_t)currentTime;  // free here, computed at top
      _pending_work |= PENDING_RTC;
    }
    if (_first_boot_ts == 0 && ntpclient.synced) {
      _first_boot_ts = (uint32_t)currentTime;
      _pending_work |= PENDING_SAVE;
    }
    static uint32_t s_save_ctr = 0;
    if (++s_save_ctr >= 21600) {   // 6 h
      s_save_ctr = 0;
      _pending_work |= PENDING_SAVE;
    }
  }

  _bool_cpm_warning = (_cpm_warning < ccpm);
  _bool_cpm_alert   = (_cpm_alert   < ccpm);
}

void Counter::save_lifetime() {
  // Advance tracked seconds by the span since the previous save: keeps the
  // CPM denominator symmetric with the click numerator across reboots.
  time_t currentTime = time(nullptr);
  if (_last_save_time_t > 0 && ntpclient.synced &&
      (uint32_t)currentTime >= _last_save_time_t) {
    _lifetime_seconds_saved += (uint32_t)currentTime - _last_save_time_t;
  }
  if (ntpclient.synced) _last_save_time_t = (uint32_t)currentTime;

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", total_clicks_lifetime);
  EGPrefs::put("life", "clk", buf);
  snprintf(buf, sizeof(buf), "%lu", total_clicks_lifetime_rollover);
  EGPrefs::put("life", "clkr", buf);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)_first_boot_ts);
  EGPrefs::put("life", "fbt", buf);
  snprintf(buf, sizeof(buf), "%lu", _lifetime_seconds_saved);
  EGPrefs::put("life", "secs", buf);
  EGPrefs::commit();
  _rtc_unsaved_clicks = 0;
  rtc_life_stash(0, 0);
}

unsigned long Counter::get_lifetime_seconds() const {
  unsigned long live = _lifetime_seconds_saved;
  if (_last_save_time_t > 0 && ntpclient.synced) {
    time_t now = time(nullptr);
    if ((uint32_t)now >= _last_save_time_t) live += (uint32_t)now - _last_save_time_t;
  }
  return live;
}

void Counter::reset_lifetime() {
  total_clicks_lifetime = 0;
  total_clicks_lifetime_rollover = 0;
  _first_boot_ts = 0;
  _lifetime_seconds_saved = 0;
  _last_save_time_t = 0;
  save_lifetime();
  Log::console(PSTR("Lifetime: reset"));
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
  unsigned long long uptime = ntpclient.getUptime ();
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
  geigerTicks.begin(_cpm_window);
#ifndef GEIGER_SMOOTH_AVG
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:EMA 15:EMA"), _cpm_window);
  geigerTicks5.begin(GEIGER_EMA_FACTOR);
  geigerTicks15.begin(GEIGER_EMA_FACTOR);
#else
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), _cpm_window, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  geigerTicks5.begin(GEIGER_CPM5_COUNT);
  geigerTicks15.begin(GEIGER_CPM15_COUNT);
#endif

  geigerinput->begin();
  apply_pcnt_filter();

  uint32_t rtc_delta_clicks = 0, rtc_delta_seconds = 0;
  bool rtc_ok = rtc_life_recover_and_clear(&rtc_delta_clicks, &rtc_delta_seconds);
  if (rtc_ok && _lifetime_enabled) {
    if (rtc_delta_clicks > 0) {
      unsigned long prev = total_clicks_lifetime;
      total_clicks_lifetime += rtc_delta_clicks;
      if (prev > total_clicks_lifetime) {
        total_clicks_lifetime = rtc_delta_clicks;
        total_clicks_lifetime_rollover++;
      }
    }
    _lifetime_seconds_saved += rtc_delta_seconds;
    if (rtc_delta_clicks > 0 || rtc_delta_seconds > 0) {
      Log::console(PSTR("Lifetime: recovered %lu clicks + %lu s from RTC"),
                   (unsigned long)rtc_delta_clicks,
                   (unsigned long)rtc_delta_seconds);
    }
  }
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
  if (const char* why = PinSafety::claim_output(pin, PSTR("LED"))) {
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

  struct tm t;
  if (!ntpclient.localTm(&t)) return false;
  int16_t m = t.tm_hour * 60 + t.tm_min;

  s_quiet_cached = (_quiet_from_min < _quiet_to_min)
    ? (m >= _quiet_from_min && m < _quiet_to_min)
    : (m >= _quiet_from_min || m < _quiet_to_min);
  s_quiet_recompute_ms = now_ms + (60UL - t.tm_sec) * 1000UL;
  return s_quiet_cached;
}

void Counter::loop() {
  // Single combined check for both deferred items (RTC stash, save_lifetime).
  // Bitmask: PENDING_RTC (bit 0), PENDING_SAVE (bit 1). Most iterations the
  // byte is 0 and we skip with one branch. Keeps RTC + flash writes out of
  // tick_max while paying only one predicted-false branch per loop iter.
  if (_pending_work) {
    uint8_t pw = _pending_work;
    _pending_work = 0;
    if (pw & PENDING_RTC) {
      uint32_t delta_s = 0;
      if (_last_save_time_t > 0 && _rtc_pending_now >= _last_save_time_t) {
        delta_s = _rtc_pending_now - _last_save_time_t;
      }
      if (_rtc_unsaved_clicks > 0 || delta_s > 0) {
        rtc_life_stash(_rtc_unsaved_clicks, delta_s);
      }
    }
    if (pw & PENDING_SAVE) {
      save_lifetime();
    }
  }

#if GEIGER_IS_SERIAL(GEIGER_TYPE) || GEIGER_IS_TEST(GEIGER_TYPE)
  geigerinput->loop();
#elif GEIGER_IS_UDPRX(GEIGER_TYPE)
  #ifndef UDPRX_POLL_SKIP
  #define UDPRX_POLL_SKIP 500
  #endif
  static uint16_t s_udp_skip = 0;
  if (++s_udp_skip >= UDPRX_POLL_SKIP) {
    s_udp_skip = 0;
    geigerinput->loop();
  }
#endif

  unsigned long lb = _last_blip;
  if (_last_blip_seen != lb) {
    _last_blip_seen = lb;

    if (_hist_primed) {
      uint32_t d = (uint32_t)(lb - _prev_blip_us);   // unsigned, wrap-safe
      uint8_t b;
      if (d < 64) {
        b = 0;
      } else {
        // d >= 64 here, so __builtin_clz(d) is well-defined.
        int lg = 31 - __builtin_clz(d);              // floor(log2(d)), 6..31
        b = (lg < 30) ? (uint8_t)(lg - 5) : 24;
      }
      _pulse_hist[b]++;
    } else {
      _hist_primed = true;
    }
    _prev_blip_us = lb;

#ifndef DISABLE_BLIP
    this->blip();
#endif
    unsigned long nowMs = millis();
    udpblip.notifyClick(nowMs);
#ifdef AUDIO_TICK
    audiotick.notifyClick(nowMs);
#endif
  }
}

// ---------- HTTP routes ----------

extern Counter gcounter;

namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }

// uint64 -> decimal ASCII (nano-libc has no %llu).
static void fmt_u64(char* out, size_t cap, uint64_t v) {
  char tmp[24];
  int n = 0;
  if (v == 0) { tmp[n++] = '0'; }
  else { while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + v % 10); v /= 10; } }
  size_t o = 0;
  while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
  out[o] = 0;
}

static void hClicks(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // /clicks JSON can run ~250 B with full 24h history; chunked to be safe.
  res.beginChunked(200, "application/json");

  char line[160];
  int n;

  n = snprintf_P(line, sizeof(line),
    PSTR("{\"last_day\":[%d"), (int)gcounter.clicks_hour);
  if (n > 0) res.sendChunk(line, (size_t)n);

  const int histSize = gcounter.day_hourly_history.size();
  for (int i = histSize; i > 0; i--) {
    n = snprintf_P(line, sizeof(line), PSTR(",%d"),
                   (int)gcounter.day_hourly_history[i - 1]);
    if (n > 0) res.sendChunk(line, (size_t)n);
  }

  n = snprintf_P(line, sizeof(line),
    PSTR("],\"today\":%d,\"yesterday\":%d,\"ratio\":\"%s\""),
    (int)gcounter.clicks_today,
    (int)gcounter.clicks_yesterday,
    EGPrefs::getString("sys", "ratio"));
  if (n > 0) res.sendChunk(line, (size_t)n);

#if GEIGER_IS_TEST(GEIGER_TYPE)
  res.sendChunk(F(",\"roll\":60"));
#endif

  if (ntpclient.synced) {
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"start\":%lu"), (unsigned long)ntpclient.boot_epoch);
  } else {
    unsigned long uptime = ntpclient.getUptime() - start;
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"uptime\":%lu"), uptime);
  }
  if (n > 0) res.sendChunk(line, (size_t)n);

  if (gcounter.get_lifetime_enabled()) {
    char clkBuf[24];
    fmt_u64(clkBuf, sizeof(clkBuf), gcounter.get_lifetime_clicks_total());
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"life\":{\"clk\":%s,\"fbt\":%lu,\"secs\":%lu}"),
      clkBuf, (unsigned long)gcounter.get_first_boot_ts(),
      gcounter.get_lifetime_seconds());
    if (n > 0) res.sendChunk(line, (size_t)n);
  }

  // Inter-pulse-interval histogram (log2 buckets, 64us .. >=512s).
  {
    const uint32_t* hist = gcounter.pulse_histogram();
    const uint8_t   nb   = Counter::pulse_histogram_buckets();
    n = snprintf_P(line, sizeof(line),
                   PSTR(",\"hist\":[%lu"), (unsigned long)hist[0]);
    if (n > 0) res.sendChunk(line, (size_t)n);
    for (uint8_t b = 1; b < nb; b++) {
      n = snprintf_P(line, sizeof(line), PSTR(",%lu"),
                     (unsigned long)hist[b]);
      if (n > 0) res.sendChunk(line, (size_t)n);
    }
    res.sendChunk(F("]"));
  }

  res.sendChunk(F("}"));
  res.endChunked();
}

static void hLastData(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // GeigerLog WiFiServer 12-field order:
  //   CPM,CPS,CPM1st,CPS1st,CPM2nd,CPS2nd,CPM3rd,CPS3rd,Temp,Press,Humid,Xtra
  // Absent fields left empty (GeigerLog reads "" as missing).
  char cpm[16], cps[16], cpm5[16], cpm15[16];
  format_f(cpm,   sizeof(cpm),   gcounter.get_cpmf());
  format_f(cps,   sizeof(cps),   gcounter.get_cps());
  format_f(cpm5,  sizeof(cpm5),  gcounter.get_cpm5f());
  format_f(cpm15, sizeof(cpm15), gcounter.get_cpm15f());

  char t[16] = "", h[16] = "", p[16] = "";
  if (envsensor.present()) {
    float et = envsensor.tempC(), eh = envsensor.humidity(), ep = envsensor.pressure();
    if (!isnan(et)) format_f(t, sizeof(t), et);
    if (!isnan(eh)) format_f(h, sizeof(h), eh);
    if (!isnan(ep)) format_f(p, sizeof(p), ep);
  }

  char xtra[16];   // HV on HW builds, else dose rate
#ifdef ESPG_HV_ADC
  if (hv.get_pwm_pin() >= 0 && hv.get_vd_ratio() != 0)
    format_f(xtra, sizeof(xtra), hv.hvReading.get());
  else
#endif
    format_f(xtra, sizeof(xtra), gcounter.get_usv());

  char out[128];
  snprintf_P(out, sizeof(out),
    PSTR("%s, %s, %s, , %s, , , , %s, %s, %s, %s"),
    cpm, cps, cpm5, cpm15, t, p, h, xtra);
  res.send(200, "text/plain", out);
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
static void hSetCPM(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* v = req.arg("v");
  if (v) {
    int target = atoi(v);
    if (target > 0) gcounter.set_target_cpm(target);
  }
  res.send(200, "text/plain", "OK");
}
#endif

static void hJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Live status as JSON. Chunked since ~250-320 B exceeds the inline-send cap.
  bool raw = req.arg("raw") != nullptr;  // ?raw=1: un-smoothed heap + frag/lfb
  res.beginChunked(200, "application/json");
  char buf[200];
  char c[16], s[16], c5[16], c15[16], cs[16];
  format_f(c,   sizeof(c),   gcounter.get_cpmf());
  format_f(s,   sizeof(s),   gcounter.get_usv());
  format_f(c5,  sizeof(c5),  gcounter.get_cpm5f());
  format_f(c15, sizeof(c15), gcounter.get_cpm15f());
  format_f(cs,  sizeof(cs),  gcounter.get_cps());
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ut\":%lu,\"c\":%s,\"s\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,\"r\":%s,\"tc\":%u,\"mem\":%u,\"rssi\":%d"),
    DeviceInfo::uptime(),
    c, s, c5, c15, cs,
    EGPrefs::getString("sys", "ratio"),
    gcounter.total_clicks,
    raw ? ESP.getFreeHeap() : DeviceInfo::freeHeap(),
    (int)Wifi::rssi);
  if (n > 0) res.sendChunk(buf, (size_t)n);
#ifdef ESPG_HV_ADC
  char hvBuf[16];
  format_f(hvBuf, sizeof(hvBuf), hv.hvReading.get());
  n = snprintf_P(buf, sizeof(buf), PSTR(",\"hv\":%s"), hvBuf);
  if (n > 0) res.sendChunk(buf, (size_t)n);
#endif
  // /json is the live-display endpoint used by /status JS. Temperature
  // ships in the user's preferred unit so the JS doesn't have to convert.
  // External APIs (MQTT/WebAPI/webhook/UDP) still send canonical units.
  if (envsensor.present()) {
    float et = envsensor.tempUser(), eh = envsensor.humidity(), ep = envsensor.pressure();
    char fbuf[12];
    if (!isnan(et)) {
      format_f(fbuf, sizeof(fbuf), et);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"t\":%s,\"tu\":%u"), fbuf, envsensor.unit());
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
    if (!isnan(eh)) {
      format_f(fbuf, sizeof(fbuf), eh);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"h\":%s"), fbuf);
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
    if (!isnan(ep)) {
      format_f(fbuf, sizeof(fbuf), ep);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"p\":%s"), fbuf);
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
  }
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  // UDP feed loss %; only once a producer is heard, so a fresh rx isn't 0.
  if (gcounter.udp_rx() && gcounter.udp_rx()->packets_accepted() > 0) {
    uint16_t lx10 = gcounter.udp_rx()->loss_pct_x10();
    n = snprintf_P(buf, sizeof(buf), PSTR(",\"loss\":%u.%u"), lx10 / 10, lx10 % 10);
    if (n > 0) res.sendChunk(buf, (size_t)n);
  }
#endif
  // tick/t_max/lps + heap diagnostics are not used by the status page; only
  // emit under ?raw=1 to keep the polled /json lean.
  if (raw) {
    n = snprintf_P(buf, sizeof(buf),
      PSTR(",\"tick\":%u,\"t_max\":%u,\"lps\":%u,\"frag\":%u,\"lfb\":%u,\"lfblow\":%u"),
      TickProfile::tick_us, TickProfile::tick_max_us, TickProfile::lps_raw,
      (unsigned)DeviceInfo::heapFrag(),
      (unsigned)DeviceInfo::largestFreeBlock(),
      (unsigned)DeviceInfo::largestFreeBlockLow());
    if (n > 0) res.sendChunk(buf, (size_t)n);
  }
  res.sendChunk("}", 1);
  res.endChunked();
}

// /hist body + /histjs. All values come from /clicks.
static const char HISTORY_BODY[] PROGMEM = R"HTML(
<div id=lf style=display:none><h2>Lifetime</h2><div class=card><div id=g2>
<div><span class=muted>Clicks </span><b id=lfc>&mdash;</b></div>
<div><span class=muted>Dose </span><b id=lfs class=dose>&mdash;</b></div>
<div><span class=muted>Tracked </span><b id=lfd>&mdash;</b></div>
<div><span class=muted>Install age </span><b id=lfi>&mdash;</b></div>
<div><span class=muted>Avg CPM </span><b id=lfa>&mdash;</b></div>
</div><p style="margin:.8em 0 0"><button class=danger onclick="if(confirm('Reset lifetime counters?'))fetch('/life/reset',{method:'POST'}).then(()=>location.reload())">Reset lifetime</button></p></div></div>
<h2>Inter-pulse intervals</h2>
<div class=card style="margin:.4em 0">
<div id=ph class=bar-row></div>
<div id=phL class=bar-lbls></div>
<div class=muted style="font-size:.85em;margin-top:.4em">log<sub>2</sub> buckets 64&micro;s to &ge;512s &middot; cumulative since boot</div>
</div>
<h2>Last 24 hours</h2>
<div class=card style="margin:.4em 0"><div id=g2>
<div><span class=muted>Today </span><b id=hsT>&mdash;</b></div>
<div><span class=muted>Yesterday </span><b id=hsY>&mdash;</b></div>
<div><span class=muted id=hsAL>24h avg CPM </span><b id=hsA>&mdash;</b></div>
<div><span class=muted>24h peak CPM </span><b id=hsP>&mdash;</b></div>
<div><span class=muted>Day rate vs yesterday </span><b id=hsD>&mdash;</b></div>
</div></div>
<div id=bc class="bar-row bot"></div>
<table>
<thead><tr><th>Date</th><th>Clicks</th><th>Avg CPM</th><th><span class=usvL>&micro;Sv</span></th></tr></thead>
<tbody id=tb></tbody>
</table>
<script src=/histjs)HTML" EG_CACHE_BUST R"HTML(></script>
)HTML";

static const char HIST_JS[] PROGMEM = R"JS(
fetch('/clicks').then(r=>r.json()).then(o=>{
  var $=byID,N=Date.now()/1000|0,T=o.today||0,Y=o.yesterday||0,
      tb=$('tb'),
      start='start' in o?new Date(o.start*1000):new Date(Date.now()-o.uptime*1000),
      rlv='roll' in o?o.roll*1000:3600000,
      rows='',mx=0,sum=0,
      F=s=>s>=86400?(s/86400).toFixed(1)+'d':s>=3600?(s/3600).toFixed(1)+'h':s>=60?(s/60|0)+'m':s+'s';
  if('life' in o){
    var L=o.life,ts=L.secs>0?L.secs:(L.fbt>0?N-L.fbt:0);
    $('lfc').textContent=L.clk.toLocaleString();
    setDose($('lfs'),L.clk/60/o.ratio);
    if(ts>0){$('lfd').textContent=F(ts);$('lfa').textContent=(L.clk*60/ts).toFixed(1)}
    if(L.fbt>0)$('lfi').textContent=F(N-L.fbt);
    $('lf').style.display='';
  }
  var nowMs=Date.now(),curB=Math.floor(nowMs/rlv)*rlv,pk=0,bs=o.start*1000;
  o.last_day.forEach((n,i)=>{
    sum+=n;if(n>mx)mx=n;
    var sd=curB-i*rlv,ed=i?sd+rlv:nowMs;
    if(i===o.last_day.length-1&&sd<bs)sd=bs;
    var ms=ed-sd;
    if(ms>0){var r=n*60000/ms;if(r>pk)pk=r}
  });
  $('hsT').textContent=T.toLocaleString();
  $('hsY').textContent=Y.toLocaleString();
  var es='start' in o?Math.max(0,N-o.start):o.uptime||0,
      h60=rlv>=3600000&&es>=3600,
      cpmA=h60?((o.last_day[1]||0)*(60-new Date().getMinutes())/60+(o.last_day[0]||0))/60
              :es>0?sum*60/Math.min(es,86400):0,
      M=N%86400,
      ok=Y>0&&M>=3600&&'start' in o&&o.start<=N-M-86400,
      Tp=M>0?T*86400/M:T,
      p=ok?(Tp-Y)*100/Y:0;
  $('hsA').textContent=Math.round(cpmA);
  $('hsAL').textContent=(h60?'1h ':'')+'avg CPM ';
  $('hsP').textContent=Math.round(pk);
  $('hsD').textContent=ok?(p>=0?'+':'')+p.toFixed(1)+'%':'—';
  if(mx<1)mx=1;
  var bars='';
  for(var i=23;i>=0;i--){
    var v=o.last_day[i]||0,
        h=v?(v/mx*100).toFixed(1)+'%':'3px',
        bg=v?'var(--accent)':'var(--border)';
    bars+='<div class=bar-cell style="background:'+bg+';height:'+h+';min-height:3px" title="'+v+'"></div>';
  }
  byID('bc').innerHTML=bars;
  o.last_day.forEach(function(n,idx){
    var off=idx*rlv,
        sd=new Date(Math.floor(Date.now()/rlv)*rlv-off),
        ed=new Date(sd.getTime()+rlv);
    if(idx==0)ed=new Date();
    if(idx==o.last_day.length-1&&sd<start)sd=start;
    var mins=(ed-sd)/60000,
        ok=mins>0&&o.ratio>0,
        uv=ok?n/mins/o.ratio:0;
    rows+='<tr><td>'+sd.toLocaleString()+'</td><td>'+n+'</td><td>'+(ok?Math.ceil(n/mins):'—')+'</td><td'+(ok?' class=usv data-uv="'+uv+'"':'')+'>'+(ok?uv.toFixed(4):'—')+'</td></tr>';
  });
  tb.innerHTML=rows;
  if(o.hist&&o.hist.length){
    var H=o.hist,mh=1,
        L=['<64us','<128us','<256us','<512us','<1ms','<2ms','<4ms','<8ms','<16ms','<32ms','<64ms','<128ms','<256ms','<512ms','<1s','<2s','<4s','<8s','<16s','<32s','<1m','<2m','<4m','<8m','>8m'],
        showL=[0,4,8,12,16,20,24];
    H.forEach(v=>{if(v>mh)mh=v});
    var hb='',lb='';
    for(var i=0;i<H.length;i++){
      var v=H[i],hp=v?(v/mh*100).toFixed(1)+'%':'2px',
          bg=v?'var(--accent)':'var(--border)',
          ta=i===0?'left':i===H.length-1?'right':'center';
      hb+='<div class=bar-cell style="background:'+bg+';height:'+hp+';min-height:2px" title="'+L[i]+': '+v+'"></div>';
      lb+='<div class=bar-cell style="text-align:'+ta+'">'+(showL.indexOf(i)>=0?L[i]:'')+'</div>';
    }
    $('ph').innerHTML=hb;
    $('phL').innerHTML=lb;
  }
  if(window.applyRad)applyRad();
});
)JS";

static void hHistory(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  const char* fn = DeviceInfo::friendlyName();
  const char* sub = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  WebPortal::sendPageHead(res, F("History"), sub);
  res.sendChunk(FPSTR(HISTORY_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hHistJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", "public, max-age=31536000, immutable");
#if EG_GZ_HIST_JS
  res.sendGzipP(200, "application/javascript", HIST_JS_GZ, HIST_JS_GZ_LEN);
#else
  res.send(200, "application/javascript", FPSTR(HIST_JS));
#endif
}

static void hLifeReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  gcounter.reset_lifetime();
  res.send(200, "text/plain", "OK");
}

void CounterRoutes::registerRoutes(EGHttpServer& http) {
  http.on("/clicks",   EGHttpRequest::GET,  hClicks);
  http.on("/lastdata", EGHttpRequest::GET,  hLastData);
  http.on("/json",     EGHttpRequest::GET,  hJson);
  http.on("/hist",     EGHttpRequest::GET,  hHistory);
  http.on("/histjs",   EGHttpRequest::GET,  hHistJs);
  http.on("/life/reset", EGHttpRequest::POST, hLifeReset);
#if GEIGER_IS_TEST(GEIGER_TYPE)
  http.on("/cpm",      EGHttpRequest::GET,  hSetCPM);
#endif
}
