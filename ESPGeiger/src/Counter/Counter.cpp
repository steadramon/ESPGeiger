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

#include "Counter.h"
#include <new>
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
#include "App/AppKeys.h"
#include "Corium/Output/LedSignal.h"
#include "BlipLed/BlipLed.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/System/RtcMap.h"
#ifdef ESP32
#include <esp_attr.h>
#endif
#include "Corium/System/Lifecycle.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/Clock.h"
#include "TickClock.h"
#include "GRNG/GRNG.h"

#ifndef ESPG_WARMUP_S
#define ESPG_WARMUP_S 10
#endif

Counter gcounter;

namespace {
constexpr uint32_t RTC_STASH_S     = 30;
constexpr uint32_t LIFETIME_SAVE_S = 6 * 3600;
constexpr uint16_t PULSE_RING_SIZE = 256;
constexpr uint16_t PULSE_RING_MASK = PULSE_RING_SIZE - 1;
constexpr uint32_t PULSE_AGE_MAX_US = 70UL * 60UL * 1000000UL;   // under the 71.6 min wrap
static_assert((PULSE_RING_SIZE & PULSE_RING_MASK) == 0, "must be power of 2");
volatile uint32_t* s_pulse_ring   = nullptr;
volatile uint16_t  s_pulse_head   = 0;
volatile uint16_t  s_pulse_count  = 0;
volatile uint32_t  s_min_pulse_us = UINT32_MAX;
volatile uint32_t  s_prev_pulse_us = 0;
// isr owns the histogram once set
volatile bool      s_hist_from_isr = false;
IsrLock::Mux s_pulse_mux;

void ensure_pulse_ring() {
  if (s_pulse_ring) return;
  s_pulse_ring = (volatile uint32_t*)calloc(PULSE_RING_SIZE, sizeof(uint32_t));
  if (!s_pulse_ring) Log::console(PSTR("Counter: pulse ring alloc failed"));
}

// past the 32 bit wrap
inline uint64_t all_clicks(unsigned long rollover, unsigned long clicks) {
  return ((uint64_t)rollover << 32) + clicks;
}
}  // namespace

void Counter::set_cpm_mode(uint8_t m) {
  _cpm_mode = (m > 4) ? 3 : m;
  _cps_in.reset();
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
  IsrLock::enter(s_pulse_mux);
  for (uint16_t i = 0; i < count; i++) {
    s_pulse_ring[s_pulse_head] = t;
    s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
    if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
    t += step;
  }
  IsrLock::leave(s_pulse_mux);
}

uint32_t Counter::_pulse_hist[Counter::HIST_BUCKETS] = {0};

void EG_ISR_ATTR Counter::on_pulse(uint32_t now_us) {
  if (s_prev_pulse_us) {
    uint32_t interval = now_us - s_prev_pulse_us;
    if (interval && interval < s_min_pulse_us) s_min_pulse_us = interval;
    _pulse_hist[interval_bucket(interval)]++;
  }
  s_prev_pulse_us = now_us;
  s_hist_from_isr = true;

  if (!s_pulse_ring) return;
  IsrLock::enter_isr(s_pulse_mux);
  s_pulse_ring[s_pulse_head] = now_us;
  s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
  if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
  IsrLock::leave_isr(s_pulse_mux);
}

// under the lock; looked at each second, an age never reaches the us wrap
static uint16_t drop_old(uint16_t head, uint32_t now_us) {
  uint16_t count = s_pulse_count;
  while (count && now_us - s_pulse_ring[(head - count) & PULSE_RING_MASK] > PULSE_AGE_MAX_US) count--;
  s_pulse_count = count;
  return count;
}

bool Counter::snapshot_ring(RingSnapshot& s) const {
  if (!s_pulse_ring) { s.count = 0; return false; }
  IsrLock::enter(s_pulse_mux);
  uint32_t now = micros();
  uint16_t head  = s_pulse_head;
  uint16_t count = drop_old(head, now);
  if (count >= 2) {
    s.newest_us = s_pulse_ring[(head - 1)     & PULSE_RING_MASK];
    s.oldest_us = s_pulse_ring[(head - count) & PULSE_RING_MASK];
    // quiet longer than the ring spans, no rate now
    if (now - s.newest_us > s.newest_us - s.oldest_us) count = 0;
  }
  IsrLock::leave(s_pulse_mux);
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
bool rtc_life_recover(uint32_t* out_clicks, uint32_t* out_seconds) {
  LifeRtc r{};
  *out_clicks = 0; *out_seconds = 0;
  if (!life_read(r)) return false;
  if (r.magic != LIFE_RTC_MAGIC) return false;
  *out_clicks  = r.delta_clicks;
  *out_seconds = r.delta_seconds;
  return true;
}
}

// final, so the tick calls it direct
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
using InputType = GeigerPulse;
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
using InputType = GeigerSerial;
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
using InputType = GeigerUdpRx;
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
using InputType = GeigerTest;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
using InputType = GeigerTestPulse;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
using InputType = GeigerTestSerial;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
using InputType = GeigerTestPulseInt;
#endif

Counter::Counter() {
  geigerinput = new InputType();
}

namespace {
#if !GEIGER_IS_TEST(GEIGER_TYPE)
bool     s_missing_logged = false;
#endif
time_t   s_next_boundary  = 0;
TickClock s_clock;
bool     s_boundary_armed = false;
uint32_t s_last_day       = 0;
uint8_t  s_life_left      = RTC_STASH_S;
uint32_t s_save_ctr       = 0;
}  // namespace

// once a second from flash misses the cache
#ifdef ESP32
#define TICK_ATTR EG_ISR_ATTR
#define TICK_COLD inline
#else
#define TICK_ATTR
// the tick's work, from flash only when it has some
#define TICK_COLD __attribute__((noinline))
#endif

#if !GEIGER_IS_TEST(GEIGER_TYPE)
TICK_COLD void Counter::note_counts_missing(uint32_t uptime_s) {
  bool cm = CounterMaths::counts_missing(uptime_s, _last_count_up_s,
                                         all_clicks(total_clicks_rollover, total_clicks));
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  cm = cm && !udp_rx()->heard();
#endif
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

TICK_COLD void Counter::hour_rollover(time_t now) {
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

// time(), the hour and the first sync stamps, on read ticks
TICK_COLD uint32_t Counter::clock_read() {
  uint32_t now = (uint32_t)time(nullptr);
  if (!s_next_boundary || now >= s_next_boundary) hour_rollover(now);
  if (_lifetime_enabled) {
    if (_last_save_time_t == 0) _last_save_time_t = now;
    if (_first_boot_ts == 0) {
      _first_boot_ts = now;
      _pending_work |= PENDING_SAVE;
    }
  }
  s_clock.read(now, (uint32_t)s_next_boundary);
  return now;
}

inline void Counter::lifetime_tick(int counts, uint32_t now) {
  unsigned long prev_lifetime = total_clicks_lifetime;
  total_clicks_lifetime += counts;
  if (prev_lifetime > total_clicks_lifetime) {
    total_clicks_lifetime_rollover++;
  }
  _rtc_unsaved_clicks += (uint32_t)counts;
  if (!--s_life_left) life_work(now);
}

// the rtc stash every 30 s, the save every 6 h
TICK_COLD void Counter::life_work(uint32_t now) {
  s_life_left = RTC_STASH_S;
  _rtc_pending_now = now;
  _pending_work |= PENDING_RTC;
  s_save_ctr += RTC_STASH_S;
  if (s_save_ctr >= LIFETIME_SAVE_S) {
    s_save_ctr = 0;
    _pending_work |= PENDING_SAVE;
  }
}

// the counts moved or the mode reads live
TICK_COLD void Counter::cpm_work(bool fresh) {
  if (fresh) _cached_cps = apply_dead_time(window_mean(_cps_in.sum, _cps_in.n, _cpm_window, _cpm_window_inv));
  _cached_cpm_active = (_cpm_mode == 3 ? _cached_cps : get_cps()) * 60.0f;
  _cached_ccpm = (int)(_cached_cpm_active + 0.5f);   // capped by apply_dead_time
}

TICK_COLD void Counter::ema_work(bool add15) {
  geigerTicks5.add(_cached_cps);
  if (add15) geigerTicks15.add(geigerTicks5.get());
}

inline void Counter::update_alarms(int cpm) {
  bool warm = geigerTicks.warm();
  bool warn = alarm_over(cpm, _cpm_warning, warm);
  bool alrt = alarm_over(cpm, _cpm_alert, warm);
  if (!warn && !alrt) _alarm_snoozed = false;
  _bool_cpm_warning = warn && !_alarm_snoozed;
  _bool_cpm_alert   = alrt && !_alarm_snoozed;
  uint8_t bits = _bool_cpm_warning | (_bool_cpm_alert << 1) | (warm << 2);
  if (bits != _alarm_bits) {
    _alarm_bits = bits;
    _pending_work |= PENDING_ALARM;
  }
}

void TICK_ATTR Counter::secondticker(uint32_t uptime_s) {
  if (!_running) return;
  InputType* in = static_cast<InputType*>(geigerinput);
  in->secondTicker();

  int eventCounter = clamp_counts(in->collect());
  if (eventCounter > 0) _last_count_up_s = uptime_s;
#if !GEIGER_IS_TEST(GEIGER_TYPE)
  if (uptime_s - _last_count_up_s >= MISSING_MIN_S || s_missing_logged) note_counts_missing(uptime_s);
#endif
  static uint8_t tick_cnt = 0;
  geigerTicks.add(eventCounter);
  unsigned long previous_total_clicks = total_clicks;
  total_clicks += eventCounter;
  if (previous_total_clicks > total_clicks) {   // wrapped
    total_clicks_rollover++;
  }

  // same counts, same cps
  bool fresh = !_cps_in.same(geigerTicks.sum(), geigerTicks.count(), _dead_time_us);
  if (fresh || _cpm_mode != 3) cpm_work(fresh);
  int ccpm = _cached_ccpm;

  cpm_history.push(ccpm);

  bool add5 = tick_cnt == 0 || tick_cnt == 5 || tick_cnt == 10;
  if (add5) ema_work(tick_cnt == 0);
  if (++tick_cnt >= 15) tick_cnt = 0;
  if (cpm5_history) push_cpm5(add5);

  uint32_t currentTime = 0;
  if (Clock::synced) currentTime = s_clock.count() ? s_clock.now : clock_read();

  clicks_today += eventCounter;
  clicks_hour += eventCounter;

  if (_lifetime_enabled) lifetime_tick(eventCounter, currentTime);
  update_alarms(ccpm);
  _pending_work |= PENDING_COUNTS;
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
  _rtc_carry_s = 0;
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
  s_clock.read_next();
}

void Counter::reset_lifetime() {
  total_clicks_lifetime = 0;
  total_clicks_lifetime_rollover = 0;
  _first_boot_ts = 0;
  _lifetime_seconds_saved = 0;
  _last_save_time_t = 0;
  save_lifetime();
  s_clock.read_next();   // stamps the first boot again
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
      uint32_t up = DeviceInfo::uptime();
      uint32_t w  = _cpm_window;
      // warm, no ring read
      if (up >= 2u * w) return _cached_cps;
      float live = get_cps_live();
      if (live <= 0.0f) return _cached_cps;
      if (up <  w)      return apply_dead_time(live);
      float liveC = apply_dead_time(live);
      float a = (float)(up - w) * _cpm_window_inv;
      return liveC * (1.0f - a) + _cached_cps * a;
    }
    default: return _cached_cps;
  }
}

int Counter::get_cpm() {
  return to_int(_cached_cpm_active);
}

float Counter::get_cpmf() {
  return _cached_cpm_active;
}

int Counter::get_cpm5() {
  return to_int(get_cpm5f());
}

float Counter::get_cpm5f() {
  return geigerTicks5.get() * 60.0f;
}

// out of the tick
void __attribute__((noinline)) Counter::push_cpm5(bool fresh) {
  if (fresh) _cpm5_x10 = cps_x600(geigerTicks5.get());
  cpm5_history->push(_cpm5_x10);
}

void Counter::want_cpm5_history() {
  if (cpm5_history) return;
  _cpm5_x10 = cps_x600(geigerTicks5.get());
  cpm5_history = new (std::nothrow) CircularBuffer<uint16_t,45>();
}

int Counter::get_cpm15() {
  return to_int(get_cpm15f());
}

float Counter::get_cpm15f() {
  return geigerTicks15.get() * 60.0f;
}

bool Counter::get_tube_alive() {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  if (udp_rx()->heard()) return true;
#endif
  return tube_alive((uint32_t)DeviceInfo::uptime(), _last_count_up_s,
                    all_clicks(total_clicks_rollover, total_clicks));
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
  bool cm = CounterMaths::counts_missing((uint32_t)DeviceInfo::uptime(),
                                         _last_count_up_s,
                                         all_clicks(total_clicks_rollover, total_clicks));
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  cm = cm && !udp_rx()->heard();
#endif
  return cm;
#endif
}

bool Counter::get_saturated() {
  float cps = get_cps_live();
  if (cps <= 0.0f) cps = geigerTicks.mean();
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
  IsrLock::enter(s_pulse_mux);
  uint32_t now = micros();
  uint16_t head  = s_pulse_head;
  uint16_t count = drop_old(head, now);
  uint32_t newest = s_pulse_ring[(head - 1) & PULSE_RING_MASK];
  uint32_t oldest = newest;
  uint16_t take = 0;
  // quiet past the window, none in it
  if (max_age_us && now - newest > max_age_us) count = 0;
  if (count >= 2) {
    take = 1;
    uint16_t cap = (max_pulses && max_pulses < count) ? max_pulses : count;
    for (uint16_t i = 2; i <= cap; i++) {
      uint32_t t = s_pulse_ring[(head - i) & PULSE_RING_MASK];
      if (max_age_us && (newest - t) > max_age_us) break;
      oldest = t;
      take = i;
    }
  }
  IsrLock::leave(s_pulse_mux);
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
#ifdef LED_FLASH_ENGINE
  BlipLed::setBrightness(level);
#endif
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

bool ClickFns::add(ClickFn f) {
  if (!f || n >= MAX) return false;
  fn[n++] = f;
  return true;
}

void ClickFns::fire(uint32_t us) const {
  for (uint8_t i = 0; i < n; i++) fn[i](us);
}

static void grng_click(uint32_t us) { GRNG::mix(us ^ DeviceInfo::cycles()); }
static void led_click(uint32_t) {
  if (!gcounter.blip_led()) return;
#ifdef LED_FLASH_ENGINE
  BlipLed::flash();
#else
  LedSignal::flash();
#endif
}

void Counter::begin() {
  _clicks.add(grng_click);
#ifndef DISABLE_BLIP
  _clicks.add(led_click);
  _rx_clicks.add(led_click);
#endif
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
  bool rtc_ok = rtc_life_recover(&rtc_delta_clicks, &rtc_delta_seconds);
  if (rtc_ok && !_lifetime_enabled) {
    life_clear();
  } else if (rtc_ok) {
    // till a flash save
    _rtc_unsaved_clicks = rtc_delta_clicks;
    _rtc_carry_s = rtc_delta_seconds;
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
  _rx_clicks.fire((uint32_t)_last_blip_seen);
}

bool Counter::is_quiet_now() {
  return _quiet.active();
}

// hot path
void Counter::loop() {
  if (_pending_work) {
    uint8_t pw = _pending_work;
    _pending_work = 0;
    if (pw & PENDING_RTC) {
      uint32_t delta_s = 0;
      if (_last_save_time_t > 0 && _rtc_pending_now >= _last_save_time_t) {
        delta_s = _rtc_pending_now - _last_save_time_t;
      }
      delta_s += _rtc_carry_s;
      if (_rtc_unsaved_clicks > 0 || delta_s > 0) {
        rtc_life_stash(_rtc_unsaved_clicks, delta_s);
      }
    }
    if (pw & PENDING_SAVE) {
      save_lifetime();
    }
    if (pw & PENDING_ALARM) EGModuleRegistry::emit(TOPIC_ALARM);
    if (pw & PENDING_COUNTS) EGModuleRegistry::emit(TOPIC_COUNTS);
  }

  static_cast<InputType*>(geigerinput)->loop();

  unsigned long lb = _last_blip;
  if (_last_blip_seen != lb) {
    _last_blip_seen = lb;

    if (!s_hist_from_isr) {
      if (_hist_primed) _pulse_hist[interval_bucket((uint32_t)(lb - _prev_blip_us))]++;
      else _hist_primed = true;
      _prev_blip_us = lb;
    }

    _clicks.fire((uint32_t)lb);
  }
}
