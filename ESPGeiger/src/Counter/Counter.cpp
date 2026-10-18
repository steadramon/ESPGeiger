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
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
#include "GeigerInput/Type/Pulse.h"
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
#include "GeigerInput/Type/Serial.h"
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
#include "GeigerInput/Type/UdpRx.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
#include "GeigerInput/Type/Test.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
#include "GeigerInput/Type/TestPulse.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
#include "GeigerInput/Type/TestSerial.h"
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
#include "GeigerInput/Type/TestPulseInt.h"
#endif
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/System/RtcMap.h"
#ifdef ESP32
#include <esp_attr.h>
#endif
#include "Corium/System/Lifecycle.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/Clock.h"
#include "TickClock.h"
#include "App/ClickHooks.h"

Counter gcounter;

namespace {
constexpr uint32_t RTC_STASH_S     = 30;
constexpr uint32_t LIFETIME_SAVE_S = 6 * 3600;
constexpr uint16_t PULSE_RING_SIZE = 256;
constexpr uint16_t PULSE_RING_MASK = PULSE_RING_SIZE - 1;
static_assert((PULSE_RING_SIZE & PULSE_RING_MASK) == 0, "must be power of 2");
volatile uint32_t* s_pulse_ring   = nullptr;
volatile uint16_t  s_pulse_head   = 0;
volatile uint16_t  s_pulse_count  = 0;
volatile uint32_t  s_min_pulse_us = UINT32_MAX;
volatile uint32_t  s_prev_pulse_us = 0;
// isr owns the histogram once set
volatile bool      s_hist_from_isr = false;
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

#if GEIGER_IS_TEST(GEIGER_TYPE)
void Counter::set_target_cpm(float val) {
  static_cast<GeigerInputTest*>(geigerinput)->setTargetCPM(val, true);
}
#endif
#if GEIGER_TYPE == GEIGER_TYPE_UDPRX
GeigerUdpRx* Counter::udp_rx() { return static_cast<GeigerUdpRx*>(geigerinput); }
#endif

void Counter::on_pulse_batch(uint16_t count, uint32_t end_us, uint32_t span_us) {
  // synthetic, no min_pulse_us
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

uint32_t Counter::_pulse_hist[Counter::HIST_BUCKETS] = {0};

void IRAM_ATTR Counter::on_pulse(uint32_t now_us) {
  if (s_prev_pulse_us) {
    uint32_t interval = now_us - s_prev_pulse_us;
    if (interval && interval < s_min_pulse_us) s_min_pulse_us = interval;
    _pulse_hist[interval_bucket(interval)]++;
  }
  s_prev_pulse_us = now_us;
  s_hist_from_isr = true;

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

// RTC stash, survives a crash not a power cut
namespace {
constexpr uint32_t LIFE_RTC_MAGIC = 0xCAFEC11D;  // bump on layout change
struct LifeRtc { uint32_t magic; uint32_t delta_clicks; uint32_t delta_seconds; };

#ifdef ESP8266
typedef RtcSlot<LifeRtc, RtcMap::APP1_AT, RtcMap::APP1_WORDS> LifeSlot;
bool life_read(LifeRtc& r) { return LifeSlot::read(r); }
void life_write(const LifeRtc& r) { LifeSlot::write(r); }
void life_clear() { LifeSlot::clear(); }
#else
RTC_NOINIT_ATTR LifeRtc s_life;   // garbage after power-on
bool life_read(LifeRtc& r) { r = s_life; return true; }
void life_write(const LifeRtc& r) { s_life = r; }
void life_clear() { s_life.magic = 0; }
#endif

void rtc_life_stash(uint32_t delta_clicks, uint32_t delta_seconds) {
  life_write(LifeRtc{LIFE_RTC_MAGIC, delta_clicks, delta_seconds});
}
bool rtc_life_recover_and_clear(uint32_t* out_clicks, uint32_t* out_seconds) {
  LifeRtc r{};
  *out_clicks = 0; *out_seconds = 0;
  if (!life_read(r)) return false;
  if (r.magic != LIFE_RTC_MAGIC) return false;
  life_clear();
  *out_clicks  = r.delta_clicks;
  *out_seconds = r.delta_seconds;
  return true;
}
}

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

namespace {
#if !GEIGER_IS_TEST(GEIGER_TYPE)
bool     s_missing_logged = false;
#endif
time_t   s_next_boundary  = 0;
TickClock s_clock;
bool     s_boundary_armed = false;
uint32_t s_last_day       = 0;
uint32_t s_rtc_ctr        = 0;
uint32_t s_save_ctr       = 0;
}  // namespace

#if !GEIGER_IS_TEST(GEIGER_TYPE)
inline void Counter::note_counts_missing(uint32_t uptime_s) {
  bool cm = CounterMaths::counts_missing(uptime_s, _last_count_up_s, (uint32_t)total_clicks);
  if (cm && !s_missing_logged) {
#if GEIGER_IS_PULSE(GEIGER_TYPE)
    Log::console(PSTR("Counter: no counts detected - check tube and wiring (GPIO %d)"), input_pin());
#else
    Log::console(PSTR("Counter: no counts detected - check tube/input wiring"));
#endif
  }
  s_missing_logged = cm;
}
#endif

inline void Counter::hour_rollover(time_t now) {
  // not static, guard runs every tick
  const time_t kBoundarySecs = (time_t)_boundary_secs;
  bool fire = false;
  if (s_next_boundary == 0) {
    // first sync
    s_next_boundary = now - (now % kBoundarySecs) + kBoundarySecs;
    fire = true;
  } else if (now >= s_next_boundary) {
    fire = true;
    // ntp stepped forward
    while (now >= s_next_boundary) s_next_boundary += kBoundarySecs;
  }
  if (!fire) return;
  uint32_t curDay = (uint32_t)(now / 86400);
  if (s_boundary_armed) {
    day_hourly_history.push(clicks_hour);
  }
  clicks_hour = 0;
  if (curDay != s_last_day) {
    if (s_boundary_armed) clicks_yesterday = clicks_today;
    clicks_today = 0;
  }
  s_last_day = curDay;
  s_boundary_armed = true;
}

inline void Counter::lifetime_tick(int counts, time_t now) {
  unsigned long prev_lifetime = total_clicks_lifetime;
  total_clicks_lifetime += counts;
  if (prev_lifetime > total_clicks_lifetime) {
    total_clicks_lifetime_rollover++;
  }
  if (counts > 0) {
    _rtc_unsaved_clicks += (uint32_t)counts;
  }
  if (_last_save_time_t == 0 && Clock::synced) {
    _last_save_time_t = (uint32_t)now;
  }
  if (++s_rtc_ctr >= RTC_STASH_S) {
    s_rtc_ctr = 0;
    _rtc_pending_now = (uint32_t)now;
    _pending_work |= PENDING_RTC;
  }
  if (_first_boot_ts == 0 && Clock::synced) {
    _first_boot_ts = (uint32_t)now;
    _pending_work |= PENDING_SAVE;
  }
  if (++s_save_ctr >= LIFETIME_SAVE_S) {
    s_save_ctr = 0;
    _pending_work |= PENDING_SAVE;
  }
}

inline void Counter::update_alarms(int cpm) {
  bool warm = geigerTicks.warm();
  bool warn = alarm_over(cpm, _cpm_warning, warm);
  bool alrt = alarm_over(cpm, _cpm_alert, warm);
  if (!warn && !alrt) _alarm_snoozed = false;
  _bool_cpm_warning = warn && !_alarm_snoozed;
  _bool_cpm_alert   = alrt && !_alarm_snoozed;
}

// esp32: once a second from flash misses the cache
#ifdef ESP32
#define TICK_ATTR IRAM_ATTR
#else
#define TICK_ATTR
#endif

void TICK_ATTR Counter::secondticker(uint32_t uptime_s) {
  if (!_running) return;
  geigerinput->secondTicker();

  int eventCounter = geigerinput->collect();
  if (eventCounter > 0) _last_count_up_s = uptime_s;
#if !GEIGER_IS_TEST(GEIGER_TYPE)
  note_counts_missing(uptime_s);
#endif
  static uint8_t tick_cnt = 0;
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {   // wrapped
    total_clicks_rollover++;
  }

  _cached_cps = apply_dead_time(geigerTicks.get());
  _cached_cpm_active = get_cps() * 60.0f;
  // roundf without soft-float, cpm >= 0
  int ccpm = (int)(_cached_cpm_active + 0.5f);

  cpm_history.push(ccpm);

  if (tick_cnt == 0 || tick_cnt == 5 || tick_cnt == 10) {
    geigerTicks5.add(_cached_cps);
    if (tick_cnt == 0) {
      geigerTicks15.add(geigerTicks5.get());
    }
  }
  if (++tick_cnt >= 15) tick_cnt = 0;

  time_t currentTime = Clock::synced ? s_clock.step(s_next_boundary, time) : 0;
  if (Clock::synced) hour_rollover(currentTime);

  clicks_today += eventCounter;
  clicks_hour += eventCounter;

  if (_lifetime_enabled) lifetime_tick(eventCounter, currentTime);
  update_alarms(ccpm);
}

void Counter::reset_alarm() {
  if (!_bool_cpm_warning && !_bool_cpm_alert) return;
  _alarm_snoozed    = true;
  _bool_cpm_warning = false;
  _bool_cpm_alert   = false;
  Log::console(PSTR("Alarm: snoozed until level drops below warning"));
}

void Counter::save_lifetime() {
  uint32_t now = Clock::epoch();
  if (_last_save_time_t > 0 && Clock::synced && now >= _last_save_time_t) {
    _lifetime_seconds_saved += now - _last_save_time_t;
  }
  if (Clock::synced) _last_save_time_t = now;

  EGPrefs::putUInt("life", EG_K_CLK, total_clicks_lifetime);
  EGPrefs::putUInt("life", EG_K_CLKR, total_clicks_lifetime_rollover);
  EGPrefs::putUInt("life", EG_K_FBT, _first_boot_ts);
  EGPrefs::putUInt("life", EG_K_SECS, _lifetime_seconds_saved);
  EGPrefs::commit();
  _rtc_unsaved_clicks = 0;
  rtc_life_stash(0, 0);
}

unsigned long Counter::get_lifetime_seconds() const {
  unsigned long live = _lifetime_seconds_saved;
  if (_last_save_time_t > 0 && Clock::synced) {
    uint32_t now = Clock::epoch();
    if (now >= _last_save_time_t) live += now - _last_save_time_t;
  }
  return live;
}

void Counter::clock_stepped(int32_t delta_s) {
  if (_last_save_time_t) _last_save_time_t += delta_s;
  // stamped from the bad time this boot
  if (_first_boot_ts && _first_boot_ts >= (uint32_t)(Clock::boot_epoch - delta_s)) {
    _first_boot_ts += delta_s;
    _pending_work |= PENDING_SAVE;
  }
  s_next_boundary = 0;
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

bool Counter::get_tube_alive() {
  return tube_alive((uint32_t)DeviceInfo::uptime(), _last_count_up_s,
                    (uint32_t)total_clicks);
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
  return CounterMaths::counts_missing((uint32_t)DeviceInfo::uptime(),
                                      _last_count_up_s,
                                      (uint32_t)total_clicks);
#endif
}

bool Counter::get_saturated() {
  float cps = get_cps_live();
  if (cps <= 0.0f) cps = geigerTicks.get();
  return saturated_at(cps);
}

float Counter::get_cps_live() {
  RingSnapshot s;
  if (!snapshot_ring(s)) return 0.0f;
  uint32_t span_us = s.newest_us - s.oldest_us;
  if (span_us == 0) return 0.0f;
  return (float)(s.count - 1) * 1.0e6f / (float)span_us;
}

float Counter::cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const {
  // 0 = uncapped
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

uint16_t Counter::get_cps_n() {
  return s_pulse_count;     // atomic on xtensa
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

void Counter::set_quiet_hours(const char* from, const char* to) {
  _quiet.set(from, to);
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

float Counter::get_usv() {
  return get_cpmf() * _ratio_inv;
}

float Counter::get_usv5() {
  return get_cpm5f() * _ratio_inv;
}

static void counter_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::OTA_FAILED) gcounter.restart_after_ota();
  else gcounter.stop_for_ota();
}

static void counter_clock_stepped(int32_t delta_s) { gcounter.clock_stepped(delta_s); }

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
  _boundary_secs = (uint16_t)geigerinput->boundary_seconds();
  apply_pcnt_filter();
  Lifecycle::on(counter_lifecycle);
  Clock::on_step(counter_clock_stepped);

  uint32_t rtc_delta_clicks = 0, rtc_delta_seconds = 0;
  bool rtc_ok = rtc_life_recover_and_clear(&rtc_delta_clicks, &rtc_delta_seconds);
  if (rtc_ok && _lifetime_enabled) {
    if (rtc_delta_clicks > 0) {
      unsigned long prev = total_clicks_lifetime;
      total_clicks_lifetime += rtc_delta_clicks;
      if (prev > total_clicks_lifetime) {
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
  _running = true;
}

bool Counter::blip_led() {
#ifdef DISABLE_BLIP
  return false;
#else
  return _blip_led && !is_quiet_now();
#endif
}

void Counter::dispatchReceiverBlip() {
  _last_blip = micros();
  // else audio fires twice and udpblip echoes
  _last_blip_seen = _last_blip;
  ClickHooks::rx_click(blip_led());
}

bool Counter::is_quiet_now() {
  return _quiet.active();
}

// NOLINTNEXTLINE(complexity) hot path
void Counter::loop() {
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
#elif defined(COUNTER_DRAINS_PCNT)
  {
    uint32_t pcnt_ms = fast_millis();
    if ((uint32_t)(pcnt_ms - _pcnt_ms) >= PCNT_DRAIN_MS) {
      _pcnt_ms = pcnt_ms;
      static_cast<GeigerPulse*>(geigerinput)->drain_pcnt();
    }
  }
#elif GEIGER_IS_UDPRX(GEIGER_TYPE)
  geigerinput->loop();
#endif

  unsigned long lb = _last_blip;
  if (_last_blip_seen != lb) {
    _last_blip_seen = lb;

    if (!s_hist_from_isr) {
      if (_hist_primed) _pulse_hist[interval_bucket((uint32_t)(lb - _prev_blip_us))]++;
      else _hist_primed = true;
      _prev_blip_us = lb;
    }

    ClickHooks::click((uint32_t)lb, blip_led());
  }
}
