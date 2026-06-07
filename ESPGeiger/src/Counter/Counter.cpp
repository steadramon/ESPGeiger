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
#include "../Util/LedSignal.h"
#include "../Util/StringUtil.h"
#include "../Util/MathUtil.h"
#include "../Util/PinSafety.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/TickProfile.h"
#include "../Util/FastMillis.h"
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

// Pulse-timestamp ring. Lazy-allocated; null-guarded on access.
namespace {
constexpr uint16_t PULSE_RING_SIZE = 256;
constexpr uint16_t PULSE_RING_MASK = PULSE_RING_SIZE - 1;
static_assert((PULSE_RING_SIZE & PULSE_RING_MASK) == 0, "must be power of 2");
volatile uint32_t* s_pulse_ring   = nullptr;
volatile uint16_t  s_pulse_head   = 0;
volatile uint16_t  s_pulse_count  = 0;
volatile uint32_t  s_min_pulse_us = UINT32_MAX;
// Tracked without the ring so /history min_pulse_us works for default mode 3.
volatile uint32_t  s_prev_pulse_us = 0;
#ifdef ESP32
portMUX_TYPE s_pulse_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

void ensure_pulse_ring() {
  if (s_pulse_ring) return;
  s_pulse_ring = (volatile uint32_t*)calloc(PULSE_RING_SIZE, sizeof(uint32_t));
}
}  // namespace

void Counter::set_cpm_mode(uint8_t m) {
  _cpm_mode = (m > 4) ? 3 : m;
  if (_cpm_mode != 3) ensure_pulse_ring();
}

static volatile uint32_t s_pause_until_ms = 0;

void Counter::pause_external(uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    s_pause_until_ms = 0;
    Log::console(PSTR("External posts: resumed"));
    return;
  }
  s_pause_until_ms = fast_millis() + timeout_ms;
  Log::console(PSTR("External posts: paused for %u s"), (unsigned)(timeout_ms / 1000U));
}

bool Counter::external_paused() {
  uint32_t until = s_pause_until_ms;
  if (until == 0) return false;
  if ((int32_t)(fast_millis() - until) >= 0) {
    s_pause_until_ms = 0;
    Log::console(PSTR("External posts: resumed (timeout)"));
    return false;
  }
  return true;
}

uint32_t Counter::pause_remaining_ms() {
  uint32_t until = s_pause_until_ms;
  if (until == 0) return 0;
  int32_t d = (int32_t)(until - fast_millis());
  return d > 0 ? (uint32_t)d : 0;
}

void Counter::on_pulse_batch(uint16_t count, uint32_t end_us, uint32_t span_us) {
  // Synthetic timestamps; skips s_min_pulse_us by design.
  if (!s_pulse_ring || count == 0) return;
  if (count > PULSE_RING_SIZE - 1) count = PULSE_RING_SIZE - 1;
  uint32_t step = (count > 1) ? (span_us / count) : 0;
  uint32_t t = end_us - (uint32_t)(count - 1) * step;
#ifdef ESP32
  portENTER_CRITICAL_ISR(&s_pulse_mux);
#else
  noInterrupts();
#endif
  for (uint16_t i = 0; i < count; i++) {
    s_pulse_ring[s_pulse_head] = t;
    s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
    if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
    t += step;
  }
#ifdef ESP32
  portEXIT_CRITICAL_ISR(&s_pulse_mux);
#else
  interrupts();
#endif
}

void IRAM_ATTR Counter::on_pulse(uint32_t now_us) {
  // Min interval runs without the ring.
  if (s_prev_pulse_us) {
    uint32_t interval = now_us - s_prev_pulse_us;
    if (interval && interval < s_min_pulse_us) s_min_pulse_us = interval;
  }
  s_prev_pulse_us = now_us;

  if (!s_pulse_ring) return;
#ifdef ESP32
  portENTER_CRITICAL_ISR(&s_pulse_mux);
#endif
  s_pulse_ring[s_pulse_head] = now_us;
  s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
  if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
#ifdef ESP32
  portEXIT_CRITICAL_ISR(&s_pulse_mux);
#endif
}

bool Counter::snapshot_ring(RingSnapshot& s) const {
  if (!s_pulse_ring) { s.count = 0; return false; }
#ifdef ESP32
  portENTER_CRITICAL(&s_pulse_mux);
#else
  noInterrupts();
#endif
  uint16_t head  = s_pulse_head;
  uint16_t count = s_pulse_count;
  if (count >= 2) {
    s.newest_us = s_pulse_ring[(head - 1)     & PULSE_RING_MASK];
    s.oldest_us = s_pulse_ring[(head - count) & PULSE_RING_MASK];
  }
#ifdef ESP32
  portEXIT_CRITICAL(&s_pulse_mux);
#else
  interrupts();
#endif
  s.count = count;
  return count >= 2;
}

// RTC stash for clicks + seconds since last flash save. Word 48: OTA-safe,
// clear of CrashDump (0..41). Folded into flash on boot.
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

void Counter::secondticker() {
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

  _cached_cps = apply_dead_time(geigerTicks.get());
  int ccpm = (int)roundf(_cached_cps * 60.0f);

  cpm_history.push(ccpm);

  if (tick_cnt == 0 || tick_cnt == 5 || tick_cnt == 10) {
    geigerTicks5.add(_cached_cps);
    if (tick_cnt == 0) {
      geigerTicks15.add(geigerTicks5.get());
    }
  }
  if (++tick_cnt >= 15) tick_cnt = 0;

  time_t currentTime = time (NULL);
  if (ntpclient.synced) {
    // Fires each hour (minute in test). Cached next-fire avoids divide-per-tick.
    time_t kBoundarySecs = (time_t)geigerinput->boundary_seconds();
    static time_t nextBoundary = 0;
    static bool boundaryArmed = false;
    bool fire = false;
    if (nextBoundary == 0) {
      // First sync: align and fire once for tz.
      nextBoundary = currentTime - (currentTime % kBoundarySecs) + kBoundarySecs;
      fire = true;
    } else if (currentTime >= nextBoundary) {
      fire = true;
      // Catch up after NTP step forward.
      while (currentTime >= nextBoundary) nextBoundary += kBoundarySecs;
    }
    if (fire) {
      // Day index so rollover survives NTP step past midnight.
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
    // Tick schedules, loop() writes; keeps RTC + flash out of tick_max.
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

  bool warn = (_cpm_warning < ccpm);
  bool alrt = (_cpm_alert   < ccpm);
  if (!warn && !alrt) _alarm_snoozed = false;     // auto re-arm when calm
  _bool_cpm_warning = warn && !_alarm_snoozed;
  _bool_cpm_alert   = alrt && !_alarm_snoozed;
}

void Counter::reset_alarm() {
  if (!_bool_cpm_warning && !_bool_cpm_alert) return;
  _alarm_snoozed    = true;
  _bool_cpm_warning = false;
  _bool_cpm_alert   = false;
  Log::console(PSTR("Alarm: snoozed until level drops below warning"));
}

void Counter::save_lifetime() {
  // Advance tracked seconds since last save so lifetime CPM stays right across reboots.
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
  // Ring modes fall back to _cached_cps when ring is empty.
  if (_cpm_mode == 3) return _cached_cps;
  auto ringOrBucket = [&](float v) -> float {
    return v > 0.0f ? apply_dead_time(v) : _cached_cps;
  };
  switch (_cpm_mode) {
    case 1: return ringOrBucket(get_cps_live());
    case 2: return ringOrBucket(cps_windowed(60UL * 1000000UL, 64));
    case 4: return ringOrBucket(cps_windowed(5UL  * 1000000UL, 19));
    case 0: {                                  // auto: ring early, bucket warm
      float live = get_cps_live();
      if (live <= 0.0f) return _cached_cps;
      uint32_t up = DeviceInfo::uptime();
      uint32_t w  = _cpm_window;
      if (up <  w)      return apply_dead_time(live);
      if (up >= 2u * w) return _cached_cps;
      // Linear blend over [w, 2w]: bucket weight 0 -> 1.
      float liveC = apply_dead_time(live);
      float a = (float)(up - w) * _cpm_window_inv;
      return liveC * (1.0f - a) + _cached_cps * a;
    }
    default: return _cached_cps;
  }
}

int Counter::get_cpm() {
  return (int)roundf(get_cps() * 60.0f);
}

float Counter::get_cpmf() {
  return get_cps() * 60.0f;
}

int Counter::get_cpm5() {
  return (int)roundf(get_cpm5f());
}

float Counter::get_cpm5f() {
  return geigerTicks5.get() * 60.0f;
}

int Counter::get_cpm15() {
  return (int)roundf(get_cpm15f());
}

float Counter::get_cpm15f() {
  return geigerTicks15.get() * 60.0f;
}

void Counter::set_ratio(float ratio) {
  if (ratio <= 0) return;
  _ratio = ratio;
  _ratio_inv = 1.0f / ratio;
  // 30 min floor: shielded / low-CPM setups would false-alarm at the bare formula.
  double t = 12000000000.0 / (double)ratio;
  if (t < 1800000000.0) t = 1800000000.0;
  _tube_timeout_us = (t >= 4.29e9) ? UINT32_MAX : (uint32_t)t;
}

bool Counter::get_tube_alive() {
  if (_ratio <= 0.0f) return true;
  unsigned long lb_us = geigerinput->last_blip();
  if (lb_us == 0) return true;
  return (micros() - lb_us) < _tube_timeout_us;
}

bool Counter::get_saturated() {
  // Raw cps only; corrected values feedback-loop near the cap.
  if (_dead_time_us == 0) return false;
  float cps = get_cps_live();
  if (cps <= 0.0f) cps = geigerTicks.get();
  return cps * _dead_time_sec > 0.875f;
}

float Counter::get_cps_live() {
  RingSnapshot s;
  if (!snapshot_ring(s)) return 0.0f;
  uint32_t span_us = s.newest_us - s.oldest_us;
  if (span_us == 0) return 0.0f;
  return (float)(s.count - 1) * 1.0e6f / (float)span_us;
}

float Counter::cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const {
  // 0 = uncapped on that axis.
  if (!s_pulse_ring) return 0.0f;
#ifdef ESP32
  portENTER_CRITICAL(&s_pulse_mux);
#else
  noInterrupts();
#endif
  uint16_t head  = s_pulse_head;
  uint16_t count = s_pulse_count;
  uint32_t newest = 0, oldest = 0;
  uint16_t take = 0;
  if (count >= 2) {
    newest = s_pulse_ring[(head - 1) & PULSE_RING_MASK];
    oldest = newest;
    take = 1;
    uint16_t cap = (max_pulses && max_pulses < count) ? max_pulses : count;
    for (uint16_t i = 2; i <= cap; i++) {
      uint32_t t = s_pulse_ring[(head - i) & PULSE_RING_MASK];
      if (max_age_us && (newest - t) > max_age_us) break;
      oldest = t;
      take = i;
    }
  }
#ifdef ESP32
  portEXIT_CRITICAL(&s_pulse_mux);
#else
  interrupts();
#endif
  if (take < 2) return 0.0f;
  uint32_t span = newest - oldest;
  if (span == 0) return 0.0f;
  return (float)(take - 1) * 1.0e6f / (float)span;
}

float Counter::apply_dead_time(float cps) const {
  // Non-paralyzable model, capped at 10x. Skip below 50 cps (factor < 1.005).
  if (_dead_time_us == 0 || cps <= 50.0f) return cps;
  float x = cps * _dead_time_sec;
  if (x > 0.9f) x = 0.9f;
  return cps / (1.0f - x);
}

uint16_t Counter::get_cps_n() {
  return s_pulse_count;     // 16-bit aligned volatile read is atomic on Xtensa
}

uint32_t Counter::get_cps_win_us() {
  RingSnapshot s;
  if (!snapshot_ring(s)) return 0;
  return s.newest_us - s.oldest_us;
}

uint32_t Counter::get_min_pulse_us() {
  uint32_t v = s_min_pulse_us;
  return (v == UINT32_MAX) ? 0 : v;
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
  return get_cpmf() * _ratio_inv;
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
  return get_cpm5f() * _ratio_inv;
}

float Counter::get_usv15() {
  return get_cpm15f() * _ratio_inv;
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
    LedSignal::click();
}

void Counter::set_blip_brightness(uint8_t level) {
  LedSignal::setBrightness(level);
}

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
  // Both ends must parse (blank = off) and NTP synced; uptime is not wall-clock.
  if (_quiet_from_min < 0 || _quiet_to_min < 0)      return false;
  if (_quiet_from_min == _quiet_to_min)              return false;
  if (!ntpclient.synced)                             return false;

  unsigned long now_ms = fast_millis();
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
  // Single byte check defers RTC stash + flash save out of tick_max.
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
    unsigned long nowMs = fast_millis();  // shared time base with UdpBlip::loop
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

  gcounter.input()->appendClicksExtra(res);

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
    // Smallest inter-pulse interval since boot, paired with the histogram.
    uint32_t mp = gcounter.get_min_pulse_us();
    if (mp > 0) {
      n = snprintf_P(line, sizeof(line),
                     PSTR(",\"min_pulse_us\":%lu"), (unsigned long)mp);
      if (n > 0) res.sendChunk(line, (size_t)n);
    }
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
  n = snprintf_P(buf, sizeof(buf),
    PSTR(",\"tube\":%u,\"sat\":%u"),
    (unsigned)(gcounter.get_tube_alive() ? 1 : 0),
    (unsigned)(gcounter.get_saturated() ? 1 : 0));
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
  gcounter.input()->appendJsonExtra(res);
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
    if (gcounter.get_cps_n() >= 2) {
      char csl[16];
      format_f(csl, sizeof(csl), gcounter.get_cps_live());
      n = snprintf_P(buf, sizeof(buf),
        PSTR(",\"cs_live\":%s,\"cs_n\":%u,\"cs_win_us\":%lu"),
        csl,
        (unsigned)gcounter.get_cps_n(),
        (unsigned long)gcounter.get_cps_win_us());
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
  }
  res.sendChunk("}", 1);
  res.endChunked();
}

// /s - lean status endpoint for /status JS. /json stays general-purpose.
static void hStatusJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char c[16], c5[16], c15[16], cs[16];
  format_f(c,   sizeof(c),   gcounter.get_cpmf());
  format_f(c5,  sizeof(c5),  gcounter.get_cpm5f());
  format_f(c15, sizeof(c15), gcounter.get_cpm15f());
  format_f(cs,  sizeof(cs),  gcounter.get_cps());

  res.beginChunked(200, "application/json");
  char buf[200];
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ut\":%lu,\"c\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,"
         "\"r\":%s,\"tc\":%u,\"rssi\":%d"),
    DeviceInfo::uptime(), c, c5, c15, cs,
    EGPrefs::getString("sys", "ratio"),
    gcounter.total_clicks,
    (int)Wifi::rssi);
  if (n > 0) res.sendChunk(buf, (size_t)n);

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
<div class=muted style="font-size:.85em;margin-top:.4em">log<sub>2</sub> buckets 64&micro;s to &ge;512s &middot; cumulative since boot<span id=mpW style=display:none> &middot; observed min <b id=mp>&mdash;</b></span></div>
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
  if(o.min_pulse_us){
    var u=o.min_pulse_us,
        s=u<1000?u+'µs':u<1e6?(u/1000).toFixed(u<10000?2:1)+'ms':(u/1e6).toFixed(2)+'s';
    $('mp').textContent=s;
    $('mpW').style.display='';
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

// /pause[?s=N] - pause external posts. ?s=0 resumes. Capped at 24 h.
static void hPause(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* sec = req.arg("s");
  if (sec) {
    int n = atoi(sec);
    if (n < 0) n = 0;
    if (n > 86400) n = 86400;
    Counter::pause_external((uint32_t)n * 1000U);
  }
  char body[40];
  uint32_t rem = Counter::pause_remaining_ms();
  snprintf_P(body, sizeof(body),
             rem ? PSTR("PAUSED %u s") : PSTR("RESUMED"), (unsigned)(rem / 1000U));
  res.send(200, "text/plain", body);
}

void CounterRoutes::registerRoutes(EGHttpServer& http) {
  http.on("/clicks",   EGHttpRequest::GET,  hClicks);
  http.on("/lastdata", EGHttpRequest::GET,  hLastData);
  http.on("/json",     EGHttpRequest::GET,  hJson);
  http.on("/s",        EGHttpRequest::GET,  hStatusJson);
  http.on("/hist",     EGHttpRequest::GET,  hHistory);
  http.on("/histjs",   EGHttpRequest::GET,  hHistJs);
  http.on("/life/reset", EGHttpRequest::POST, hLifeReset);
  http.on("/pause",      EGHttpRequest::GET,  hPause);
#if GEIGER_IS_TEST(GEIGER_TYPE)
  http.on("/cpm",      EGHttpRequest::GET,  hSetCPM);
#endif
}
