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
#ifdef PULSE_OUT
#include "../PulseOut/PulseOut.h"
#endif
#include "HIST_JS.gz.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

extern long start;  // boot epoch ref from main

// Pulse-timestamp ring (lazy alloc).
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
  if (!s_pulse_ring) Log::console(PSTR("Counter: pulse ring alloc failed"));
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
  // Synthetic timestamps; min_pulse_us is real-only.
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
  if (eventCounter > 0) _last_count_up_s = (uint32_t)DeviceInfo::uptime();
#if !GEIGER_IS_TEST(GEIGER_TYPE)
  {
    static bool cm_logged = false;
    bool cm = counts_missing();
    if (cm && !cm_logged) {
#if GEIGER_IS_PULSE(GEIGER_TYPE)
      Log::console(PSTR("Counter: no counts detected - check tube and wiring (GPIO %d)"), input_pin());
#else
      Log::console(PSTR("Counter: no counts detected - check tube/input wiring"));
#endif
    }
    cm_logged = cm;
  }
#endif
  static uint8_t tick_cnt = 0;
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {
    total_clicks = eventCounter;
    total_clicks_rollover++;
  }

  _cached_cps = apply_dead_time(geigerTicks.get());
  // Cache dispatched CPM once per tick: hot reads skip the switch, warn
  // tracks the value the user sees (relevant in non-bucket modes).
  _cached_cpm_active = get_cps() * 60.0f;
  int ccpm = (int)roundf(_cached_cpm_active);

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
    // Build-time constant per input type, evaluated once.
    static const time_t kBoundarySecs = (time_t)geigerinput->boundary_seconds();
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
    // Schedule here; loop() does the RTC + flash work.
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
    case 0: {
      float live = get_cps_live();
      if (live <= 0.0f) return _cached_cps;
      uint32_t up = DeviceInfo::uptime();
      uint32_t w  = _cpm_window;
      if (up <  w)      return apply_dead_time(live);
      if (up >= 2u * w) return _cached_cps;
      float liveC = apply_dead_time(live);
      float a = (float)(up - w) * _cpm_window_inv;
      return liveC * (1.0f - a) + _cached_cps * a;
    }
    default: return _cached_cps;
  }
}

int Counter::get_cpm() {
  return (int)roundf(_cached_cpm_active);
}

float Counter::get_cpmf() {
  return _cached_cpm_active;
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
  uint32_t t = (uint32_t)(12000.0 / (double)ratio);
  _tube_timeout_s = (t < 1800) ? 1800 : t;
}

bool Counter::get_tube_alive() {
  if (_ratio <= 0.0f) return true;
  // Silence measured in uptime seconds from the last collected count, so a
  // tube that has never counted since boot also goes dead once past timeout.
  return ((uint32_t)DeviceInfo::uptime() - _last_count_up_s) < _tube_timeout_s;
}

int Counter::input_pin() {
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  return geigerinput->get_rx_pin();
#else
  return -1;
#endif
}

bool Counter::counts_missing() {
#if GEIGER_IS_TEST(GEIGER_TYPE)
  return false;
#else
  // 10 expected counts of silence (Poisson, P ~ 5e-5). Expected rate is the
  // assumed background (0.1 uSv/h through the ratio) until the first count,
  // then the observed lifetime rate, so unset ratios and quiet sites
  // self-calibrate. Cap at the dead-tube timeout: the advisory must never
  // be slower than the hard latch.
  if (_ratio <= 0.0f) return false;
  uint32_t up = (uint32_t)DeviceInfo::uptime();
  uint32_t silence = up - _last_count_up_s;
  // Below the 60 s floor no threshold can fire; skip the soft-float and
  // 64-bit divides on the healthy-station common path.
  if (silence < 60) return false;
  uint32_t thresh = (uint32_t)(6000.0f / _ratio);
  if (total_clicks > 0) {
    uint32_t obs = (uint32_t)((10ULL * up) / total_clicks);
    if (obs > thresh) thresh = obs;
  }
  if (thresh > _tube_timeout_s) thresh = _tube_timeout_s;
  return silence >= thresh;
#endif
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
  // total_clicks wraps at 2^32; weight each rollover by 2^32.
  float totalUsv  = 4294967296.0f * inv_uptime * (float)total_clicks_rollover;
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

void Counter::dispatchReceiverBlip() {
  _last_blip = micros();
  // Suppress Counter::loop's polling-based re-dispatch of this same blip,
  // otherwise each receiver blip fires audio twice and would also bounce
  // back out via udpblip.notifyClick on a UdpRx+UdpBlip combo device.
  // OLED/NeoPixel still see _last_blip change via their own poll counters.
  _last_blip_seen = _last_blip;
#ifndef DISABLE_BLIP
  this->blip();
#endif
  unsigned long nowMs = fast_millis();
#ifdef AUDIO_TICK
  audiotick.notifyClick(nowMs);
#endif
#ifdef PULSE_OUT
  pulseout.notifyClick(nowMs);
#endif
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
#elif GEIGER_IS_PULSE(GEIGER_TYPE) && defined(USE_PCNT)
  // 50 Hz PCNT drain so audio/LED/UDP fire on the click, not the 1 Hz tick.
  {
    static uint32_t s_last_pcnt_ms = 0;
    uint32_t pcnt_ms = fast_millis();
    if ((uint32_t)(pcnt_ms - s_last_pcnt_ms) >= 20) {
      s_last_pcnt_ms = pcnt_ms;
      geigerinput->drain_pcnt();
    }
  }
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

    GRNG::mix((uint32_t)lb ^ ESP.getCycleCount());

#ifndef DISABLE_BLIP
    this->blip();
#endif
    unsigned long nowMs = fast_millis();  // shared time base with UdpBlip::loop
    udpblip.notifyClick(nowMs);
#ifdef AUDIO_TICK
    audiotick.notifyClick(nowMs);
#endif
#ifdef PULSE_OUT
    pulseout.notifyClick(nowMs);
#endif
  }
}

