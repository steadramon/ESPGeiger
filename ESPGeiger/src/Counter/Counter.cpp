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
#include <string.h>
#include "GeigerInput/InputType.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
#include "App/AppKeys.h"
#include "Corium/Output/LedSignal.h"
#include "BlipLed/BlipLed.h"
#include "Corium/System/RtcMap.h"
#include "Corium/Util/Attr.h"
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
// CPM_60S and CPM_FAST: max age or max pulses, first hit wins
constexpr uint32_t FIXED_US = 60UL * 1000000UL;
constexpr uint16_t FIXED_N  = 64;
constexpr uint32_t FAST_US  = 5UL * 1000000UL;
constexpr uint16_t FAST_N   = 19;
constexpr uint8_t  EMA5_S   = 5;
constexpr uint8_t  EMA15_S  = 3 * EMA5_S;
static_assert((PULSE_RING_SIZE & PULSE_RING_MASK) == 0, "must be power of 2");
volatile uint32_t* s_pulse_ring   = nullptr;
volatile uint16_t  s_pulse_head   = 0;
volatile uint16_t  s_pulse_count  = 0;
volatile uint32_t  s_min_pulse_us = UINT32_MAX;
volatile uint32_t  s_prev_pulse_us = 0;
// isr owns the histogram once set
volatile bool      s_hist_from_isr = false;
corium::IsrLock::Mux s_pulse_mux;

// zeros first, then the ISR sees it under the lock
void ensure_pulse_ring() {
  if (s_pulse_ring) return;
  volatile uint32_t* r = (volatile uint32_t*)calloc(PULSE_RING_SIZE, sizeof(uint32_t));
  if (!r) {
    corium::Log::console(PSTR("Counter: Pulse ring alloc failed"));
    return;
  }
  corium::IsrLock::enter(s_pulse_mux);
  s_pulse_head = 0;
  s_pulse_count = 0;
  s_pulse_ring = r;
  corium::IsrLock::leave(s_pulse_mux);
}

// as a boot in bucket mode; the ISR takes it under the lock
void drop_pulse_ring() {
  if (!s_pulse_ring) return;
  corium::IsrLock::enter(s_pulse_mux);
  volatile uint32_t* r = s_pulse_ring;
  s_pulse_ring = nullptr;
  s_pulse_head = 0;
  s_pulse_count = 0;
  corium::IsrLock::leave(s_pulse_mux);
  free((void*)r);
}

// past the 32 bit wrap
inline uint64_t all_clicks(unsigned long rollover, unsigned long clicks) {
  return ((uint64_t)rollover << 32) + clicks;
}

// lo += n, carry into hi
inline __attribute__((always_inline)) void add_wrapped(unsigned long& lo, unsigned long& hi, unsigned long n) {
  unsigned long prev = lo;
  lo += n;
  if (prev > lo) hi++;
}
}  // namespace

void Counter::set_cpm_mode(uint8_t m) {
  _cpm_mode = m;
  _cps_in.reset();
  if (_cpm_mode != CPM_BUCKET) ensure_pulse_ring();
  else drop_pulse_ring();
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
void Counter::set_target_cpm(float val) {
  static_cast<GeigerInputTest*>(_input)->setTargetCPM(val, true);
}
#endif
#if GEIGER_TYPE == GEIGER_TYPE_UDPRX
GeigerUdpRx* Counter::udp_rx() { return static_cast<GeigerUdpRx*>(_input); }
#endif

uint16_t Counter::on_pulse_batch(uint16_t count, uint32_t end_us, uint32_t span_us) {
  // synthetic, no min_pulse_us; evenly spaced, last one at end_us
  if (!s_pulse_ring || count == 0) return 0;
  if (count > PULSE_RING_SIZE - 1) count = PULSE_RING_SIZE - 1;
  uint32_t step = (count > 1) ? (span_us / count) : 0;
  uint32_t t = end_us - (uint32_t)(count - 1) * step;
  corium::IsrLock::enter(s_pulse_mux);
  for (uint16_t i = 0; i < count; i++) {
    s_pulse_ring[s_pulse_head] = t;
    s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
    if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
    t += step;
  }
  corium::IsrLock::leave(s_pulse_mux);
  return count;
}

void Counter::drop_pulses(uint32_t n) {
  if (!s_pulse_ring) return;
  corium::IsrLock::enter(s_pulse_mux);
  if (n > s_pulse_count) n = s_pulse_count;
  s_pulse_count -= n;
  s_pulse_head = (s_pulse_head - n) & PULSE_RING_MASK;
  corium::IsrLock::leave(s_pulse_mux);
}

uint32_t Counter::_pulse_hist[Counter::HIST_BUCKETS] = {0};

// hist and min under the ring's lock, the pages copy them under it
void CORIUM_ISR_ATTR Counter::on_pulse(uint32_t now_us) {
  corium::IsrLock::enterIsr(s_pulse_mux);
  if (s_prev_pulse_us) {
    uint32_t interval = now_us - s_prev_pulse_us;
    if (interval && interval < s_min_pulse_us) s_min_pulse_us = interval;
    _pulse_hist[interval_bucket(interval)]++;
  }
  s_prev_pulse_us = now_us;
  s_hist_from_isr = true;
  if (s_pulse_ring) {
    s_pulse_ring[s_pulse_head] = now_us;
    s_pulse_head = (s_pulse_head + 1) & PULSE_RING_MASK;
    if (s_pulse_count < PULSE_RING_SIZE) s_pulse_count++;
  }
  corium::IsrLock::leaveIsr(s_pulse_mux);
}

void Counter::pulse_histogram(uint32_t* out) const {
  corium::IsrLock::enter(s_pulse_mux);
  memcpy(out, _pulse_hist, sizeof(_pulse_hist));
  corium::IsrLock::leave(s_pulse_mux);
}

// under the lock; looked at each second, an age never reaches the us wrap
static uint16_t drop_old(uint16_t head, uint32_t now_us) {
  uint16_t count = s_pulse_count;
  while (count && now_us - s_pulse_ring[(head - count) & PULSE_RING_MASK] > PULSE_AGE_MAX_US) count--;
  s_pulse_count = count;
  return count;
}

// the ISR still fills it when no mode reads it each second
static void prune_ring() {
  if (!s_pulse_ring) return;
  corium::IsrLock::enter(s_pulse_mux);
  drop_old(s_pulse_head, micros());
  corium::IsrLock::leave(s_pulse_mux);
}

bool Counter::snapshot_ring(RingSnapshot& s) const {
  if (!s_pulse_ring) { s.count = 0; return false; }
  corium::IsrLock::enter(s_pulse_mux);
  uint32_t now = micros();
  uint16_t head  = s_pulse_head;
  uint16_t count = drop_old(head, now);
  if (count >= 2) {
    s.newest_us = s_pulse_ring[(head - 1)     & PULSE_RING_MASK];
    s.oldest_us = s_pulse_ring[(head - count) & PULSE_RING_MASK];
    // quiet longer than the ring spans, no rate now
    if (now - s.newest_us > s.newest_us - s.oldest_us) count = 0;
  }
  corium::IsrLock::leave(s_pulse_mux);
  s.now_us = now;
  s.count = count;
  return count >= 2;
}

// RTC stash, survives a crash not a power cut; the totals, a boot takes the bigger
namespace {
constexpr uint32_t LIFE_RTC_MAGIC = 0xCAFEC11F;  // bump on layout change
// magic ^ the rest, a flipped bit anywhere fails it
struct LifeRtc { uint32_t magic; uint32_t clicks; uint32_t rollover; uint32_t secs; };

CORIUM_RTC_SLOT(LifeSlot, LifeRtc, corium::RtcMap::APP1_AT, corium::RtcMap::APP1_WORDS);
bool life_read(LifeRtc& r) { return LifeSlot::read(r); }
void life_write(const LifeRtc& r) { LifeSlot::write(r); }
// all zero never checks, magic 0 alone can
void life_clear() { life_write(LifeRtc{}); }

void rtc_life_stash(uint32_t clicks, uint32_t rollover, uint32_t secs) {
  life_write(LifeRtc{LIFE_RTC_MAGIC ^ clicks ^ rollover ^ secs, clicks, rollover, secs});
}
bool rtc_life_recover(LifeRtc& r) {
  return life_read(r) && r.magic == (LIFE_RTC_MAGIC ^ r.clicks ^ r.rollover ^ r.secs);
}
}

Counter::Counter() {
  _input = new InputType();
}

namespace {
#if !GEIGER_IS_TEST(GEIGER_TYPE)
bool     s_missing_logged = false;
#endif
uint32_t s_next_boundary  = 0;   // epoch, past 2106 too
TickClock s_clock;
bool     s_boundary_armed = false;
uint32_t s_last_day       = 0;
uint8_t  s_life_left      = RTC_STASH_S;
uint32_t s_save_s         = 0;
}  // namespace

#if !GEIGER_IS_TEST(GEIGER_TYPE)
CORIUM_TICK_COLD void Counter::note_counts_missing(uint32_t uptime_s) {
  bool missing = CounterMaths::counts_missing(uptime_s, _last_count_up_s,
                                              all_clicks(total_clicks_rollover, total_clicks));
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  missing = missing && !udp_rx()->heard();
#endif
  if (missing && !s_missing_logged) {
#if GEIGER_IS_PULSE(GEIGER_TYPE)
    corium::Log::console(PSTR("Counter: No counts detected - check tube and wiring (GPIO %d)"), input_pin());
#else
    corium::Log::console(PSTR("Counter: No counts detected - check tube/input wiring"));
#endif
  }
  s_missing_logged = missing;
}
#endif

static uint32_t next_boundary(time_t t, uint32_t secs) {
  return (uint32_t)(t - t % secs + secs);
}

// t for the hour and day, 2^32 s isn't whole ones
CORIUM_TICK_COLD void Counter::hour_rollover(time_t t) {
  // not static, guard runs every tick
  const uint32_t step = _boundary_s;
  uint32_t now = (uint32_t)t;
  bool fire = false;
  if (s_next_boundary == 0) {
    // first sync
    s_next_boundary = next_boundary(t, step);
    fire = true;
  } else if (corium::reached(now, s_next_boundary)) {
    fire = true;
    // ntp stepped forward
    while (corium::reached(now, s_next_boundary)) s_next_boundary += step;
  }
  if (!fire) return;
  uint32_t day = (uint32_t)(t / 86400);
  if (s_boundary_armed) {
    day_hourly_history.push(clicks_hour);
  }
  clicks_hour = 0;
  if (day != s_last_day) {
    if (s_boundary_armed) clicks_yesterday = clicks_today;
    clicks_today = 0;
  }
  s_last_day = day;
  s_boundary_armed = true;
}

// time(), the hour and the first sync stamps, on read ticks
CORIUM_TICK_COLD uint32_t Counter::clock_read() {
  time_t t = corium::Clock::now();
  uint32_t now = (uint32_t)t;
  if (!s_next_boundary || corium::reached(now, s_next_boundary)) hour_rollover(t);
  // none yet, or ntp says it's ahead of now; a browser or rtc never moves one
  bool ahead = corium::Clock::source == corium::Clock::NTP && !corium::reached(now, _first_boot_ts);
  if (_lifetime_enabled && (!_first_boot_ts || ahead)) {
    _first_boot_ts = now;
    _pending_work |= PENDING_SAVE;
  }
  s_clock.read(now, s_next_boundary);
  return now;
}

inline void Counter::lifetime_tick(int counts) {
  add_wrapped(total_clicks_lifetime, total_clicks_lifetime_rollover, counts);
  if (!--s_life_left) life_work();
}

// counted time, no clock: the rtc stash every 30 s, the save every 6 h
CORIUM_TICK_COLD void Counter::life_work() {
  s_life_left = RTC_STASH_S;
  _life_saved_s += RTC_STASH_S;
  _pending_work |= PENDING_RTC;
  s_save_s += RTC_STASH_S;
  if (s_save_s >= LIFETIME_SAVE_S) {
    s_save_s = 0;
    _pending_work |= PENDING_SAVE;
  }
}

// the counts moved or the mode reads live
CORIUM_TICK_COLD void Counter::cpm_work(bool fresh) {
  if (fresh) _cached_cps = apply_dead_time(window_mean(_cps_in.sum, _cps_in.n, _cpm_window, _cpm_window_inv));
  _cached_cpm_active = (_cpm_mode == CPM_BUCKET ? _cached_cps : get_cps()) * 60.0f;
  _cached_ccpm = (int)(_cached_cpm_active + 0.5f);   // capped by apply_dead_time
}

CORIUM_TICK_COLD void Counter::ema_work(bool add15) {
  _ticks5.add(_cached_cps);
  if (add15) _ticks15.add(_ticks5.get());
}

// true on a 5 s step
inline bool Counter::ema_tick() {
  static uint8_t s_tick = 0;
  bool add5 = s_tick == 0 || s_tick == EMA5_S || s_tick == 2 * EMA5_S;
  if (add5) ema_work(s_tick == 0);
  if (++s_tick >= EMA15_S) s_tick = 0;
  return add5;
}

inline void Counter::update_alarms(int cpm) {
  bool warm = _ticks.warm();
  bool warn = alarm_over(cpm, _cpm_warning, warm);
  bool alrt = alarm_over(cpm, _cpm_alert, warm);
  if (!warn && !alrt) _alarm_snoozed = false;
  _warn_on  = warn;
  _alert_on = alrt;
  uint8_t bits = _warn_on | (_alert_on << 1) | (warm << 2);
  if (bits != _alarm_bits) {
    _alarm_bits = bits;
    _pending_work |= PENDING_ALARM;
  }
}

void CORIUM_TICK_ATTR Counter::secondticker(uint32_t uptime_s) {
  if (!_running) return;
  InputType* in = static_cast<InputType*>(_input);
  in->secondTicker();

  int counts = clamp_counts(in->collect());
  if (counts > 0) _last_count_up_s = uptime_s;
#if !GEIGER_IS_TEST(GEIGER_TYPE)
  if (uptime_s - _last_count_up_s >= MISSING_MIN_S || s_missing_logged) note_counts_missing(uptime_s);
#endif
  _ticks.add(counts);
  add_wrapped(total_clicks, total_clicks_rollover, counts);

  // same counts, same cps
  bool fresh = !_cps_in.same(_ticks.sum(), _ticks.count(), _dead_time_us);
  if (fresh || _cpm_mode != CPM_BUCKET) cpm_work(fresh);
  int ccpm = _cached_ccpm;

  cpm_history.push(ccpm);

  bool add5 = ema_tick();
  if (cpm5_history) push_cpm5(add5);

  // the hour, the first boot stamp
  if (corium::Clock::synced && !s_clock.count()) (void)clock_read();

  clicks_today += counts;
  clicks_hour += counts;

  if (_lifetime_enabled) lifetime_tick(counts);
  update_alarms(ccpm);
  _pending_work |= PENDING_COUNTS;
}

// the sounders and the relay sleep, the state stays
void Counter::reset_alarm() {
  if (_alarm_snoozed || (!_warn_on && !_alert_on)) return;
  _alarm_snoozed = true;
  _pending_work |= PENDING_ALARM;
  corium::Log::console(PSTR("Alarm: Snoozed until level drops below warning"));
}

// true = on flash; the stash has the totals either way
bool Counter::save_lifetime() {
  bool put = corium::Prefs::putUInt("life", EG_K_CLK, total_clicks_lifetime) &&
             corium::Prefs::putUInt("life", EG_K_CLKR, total_clicks_lifetime_rollover) &&
             corium::Prefs::putUInt("life", EG_K_FBT, _first_boot_ts) &&
             corium::Prefs::putUInt("life", EG_K_SECS, _life_saved_s);
  (void)corium::Prefs::commit();   // false for any group, landed() says for life
  if (put && corium::Prefs::landed("life")) return true;
  corium::Log::console(PSTR("Lifetime: Not saved"));
  return false;
}

unsigned long Counter::get_lifetime_seconds() const {
  return _life_saved_s;
}

void Counter::clock_stepped(int32_t delta_s) {
  // stamped from the bad time this boot
  if (_first_boot_ts && corium::reached(_first_boot_ts, (uint32_t)(corium::Clock::bootEpoch - delta_s))) {
    _first_boot_ts += delta_s;
    _pending_work |= PENDING_SAVE;
  }
  // same hour, no early bar
  if (s_next_boundary != next_boundary(corium::Clock::now(), _boundary_s)) s_next_boundary = 0;
  s_clock.read_next();
}

bool Counter::reset_lifetime() {
  total_clicks_lifetime = 0;
  total_clicks_lifetime_rollover = 0;
  _life_saved_s = 0;   // the first boot stays
  life_clear();   // else a crash brings the old totals back
  bool ok = save_lifetime();
  if (ok) corium::Log::console(PSTR("Lifetime: Reset"));
  return ok;
}

float Counter::get_cps() {
  if (_cpm_mode == CPM_BUCKET) return _cached_cps;
  auto or_bucket = [&](float v) -> float {
    return v > 0.0f ? apply_dead_time(v) : _cached_cps;
  };
  switch (_cpm_mode) {
    case CPM_LIVE: return or_bucket(get_cps_live());
    case CPM_60S:  return or_bucket(cps_windowed(FIXED_US, FIXED_N));
    case CPM_FAST: return or_bucket(cps_windowed(FAST_US, FAST_N));
    case CPM_FAST_START: {
      uint32_t up = corium::DeviceInfo::uptime();
      uint32_t w  = _cpm_window;
      // live till w, then blend into the bucket by 2w; warm, the ring only pruned
      if (up >= 2u * w) {
        prune_ring();
        return _cached_cps;
      }
      float live = get_cps_live(FAST_N);   // a few close pulses overshoot, the bucket till then
      if (live <= 0.0f) return _cached_cps;
      if (up <  w)      return apply_dead_time(live);
      float adj = apply_dead_time(live);
      float a = (float)(up - w) * _cpm_window_inv;
      return adj * (1.0f - a) + _cached_cps * a;
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
  return _ticks5.get() * 60.0f;
}

// out of the tick
void __attribute__((noinline)) Counter::push_cpm5(bool fresh) {
  if (fresh) _cpm5_x10 = cps_x600(_ticks5.get());
  cpm5_history->push(_cpm5_x10);
}

void Counter::want_cpm5_history() {
  if (cpm5_history) return;
  _cpm5_x10 = cps_x600(_ticks5.get());
  cpm5_history = new (std::nothrow) CircularBuffer<uint16_t,45>();
}

int Counter::get_cpm15() {
  return to_int(get_cpm15f());
}

float Counter::get_cpm15f() {
  return _ticks15.get() * 60.0f;
}

bool Counter::get_tube_alive() {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  if (udp_rx()->heard()) return true;
#endif
  return tube_alive((uint32_t)corium::DeviceInfo::uptime(), _last_count_up_s,
                    all_clicks(total_clicks_rollover, total_clicks));
}

int Counter::input_pin() {
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  return _input->get_rx_pin();
#else
  return -1;
#endif
}

bool Counter::counts_missing() {
#if GEIGER_IS_TEST(GEIGER_TYPE)
  return false;
#else
  bool missing = CounterMaths::counts_missing((uint32_t)corium::DeviceInfo::uptime(),
                                              _last_count_up_s,
                                              all_clicks(total_clicks_rollover, total_clicks));
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  missing = missing && !udp_rx()->heard();
#endif
  return missing;
#endif
}

bool Counter::get_saturated() {
  float cps = get_cps_live();
  if (cps <= 0.0f) cps = _ticks.mean();
  return saturated_at(cps);
}

float Counter::get_cps_live(uint16_t min_n) {
  RingSnapshot s;
  if (!snapshot_ring(s) || s.count < min_n) return 0.0f;
  uint32_t span_us = s.now_us - s.oldest_us;   // to now, falls once they stop
  if (span_us == 0) return 0.0f;
  return (float)(s.count - 1) * 1.0e6f / (float)span_us;
}

float Counter::cps_windowed(uint32_t max_age_us, uint16_t max_pulses) const {
  // 0 = uncapped
  if (!s_pulse_ring) return 0.0f;
  corium::IsrLock::enter(s_pulse_mux);
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
  corium::IsrLock::leave(s_pulse_mux);
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
  corium::IsrLock::enter(s_pulse_mux);
  uint32_t v = s_min_pulse_us;
  corium::IsrLock::leave(s_pulse_mux);
  return (v == UINT32_MAX) ? 0 : v;
}

void Counter::set_blip_brightness(uint8_t level) {
  corium::LedSignal::setBrightness(level);
#ifdef LED_FLASH_ENGINE
  BlipLed::setBrightness(level);
#endif
}

void Counter::set_quiet_hours(const char* from, const char* to) {
  _quiet.set(from, to);
}

// warmup plus one full window
bool Counter::is_warm() const {
  if (_warm_cached) return true;
  if (corium::DeviceInfo::uptime() >= (uint32_t)(ESPG_WARMUP_S + _cpm_window)) {
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

static void counter_lifecycle(corium::Lifecycle::Event e) {
  gcounter.ota_hold(e == corium::Lifecycle::OTA_START);
  if (e == corium::Lifecycle::OTA_FAILED) gcounter.restart_after_ota();
  else gcounter.stop_for_ota();
}

static void counter_clock_stepped(int32_t delta_s) { gcounter.clock_stepped(delta_s); }

// past MAX the build fails, this logs one that slips by
bool ClickFns::add(ClickFn f) {
  if (!f) return false;
  for (uint8_t i = 0; i < n; i++) {
    if (fn[i] == f) return true;   // a second begin()
  }
  if (n >= MAX) {
    corium::Log::console(PSTR("Counter: Click hook dropped, %u max"), (unsigned)MAX);
    return false;
  }
  fn[n++] = f;
  return true;
}

void ClickFns::fire(uint32_t us) const {
  for (uint8_t i = 0; i < n; i++) fn[i](us);
}

static void grng_click(uint32_t us) { GRNG::mix(us ^ corium::DeviceInfo::cycles()); }
static void led_click(uint32_t) {
  if (!gcounter.blip_led()) return;
#ifdef LED_FLASH_ENGINE
  BlipLed::flash();
#else
  corium::LedSignal::flash();
#endif
}

void Counter::begin() {
  _clicks.add(grng_click);
#ifndef DISABLE_BLIP
  _clicks.add(led_click);
  _rx_clicks.add(led_click);
#endif
  _ticks.begin(_cpm_window);
#ifndef GEIGER_SMOOTH_AVG
  corium::Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:EMA 15:EMA"), _cpm_window);
  _ticks5.begin(GEIGER_EMA_FACTOR);
  _ticks15.begin(GEIGER_EMA_FACTOR);
#else
  corium::Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), _cpm_window, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  _ticks5.begin(GEIGER_CPM5_COUNT);
  _ticks15.begin(GEIGER_CPM15_COUNT);
#endif

  _input->begin();
  _boundary_s = (uint16_t)_input->boundary_seconds();
  apply_pcnt_filter();
  corium::Lifecycle::on(counter_lifecycle);
  corium::Clock::onStep(counter_clock_stepped);

  LifeRtc r{};
  // an erase keeps the rtc, not the life file: no file, its totals go
  bool rtc_ok = corium::Prefs::stored("life") && rtc_life_recover(r);
  if (!rtc_ok || !_lifetime_enabled) {
    life_clear();
  } else {
    // newer than the file after a crash, older after a save
    uint64_t was = all_clicks(total_clicks_lifetime_rollover, total_clicks_lifetime);
    uint64_t got = all_clicks(r.rollover, r.clicks);
    uint64_t clicks = got > was ? got - was : 0;
    uint32_t secs = r.secs > _life_saved_s ? r.secs - _life_saved_s : 0;
    if (clicks > 0) {
      total_clicks_lifetime = r.clicks;
      total_clicks_lifetime_rollover = r.rollover;
    }
    _life_saved_s += secs;
    if (clicks > 0 || secs > 0) {
      corium::Log::console(PSTR("Lifetime: Recovered %lu clicks + %lu s from RTC"),
                   (unsigned long)clicks,
                   (unsigned long)secs);
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

// rare, out of the pass
void __attribute__((noinline)) Counter::run_pending() {
  uint8_t pw = _pending_work;
  _pending_work = 0;
  if (pw & PENDING_RTC) rtc_life_stash(total_clicks_lifetime, total_clicks_lifetime_rollover, _life_saved_s);
  if (pw & PENDING_SAVE) {
    (void)save_lifetime();   // the stash has it
  }
  if (pw & PENDING_ALARM) corium::Registry::emit(TOPIC_ALARM);
  if (pw & PENDING_COUNTS) corium::Registry::emit(TOPIC_COUNTS);
}

void Counter::loop() {
  if (_pending_work) run_pending();

  static_cast<InputType*>(_input)->loop();

  unsigned long lb = _last_blip;
  if (_last_blip_seen != lb) {
    _last_blip_seen = lb;

    // inputs without on_pulse fill the histogram here
    if (!s_hist_from_isr) {
      if (_hist_primed) _pulse_hist[interval_bucket((uint32_t)(lb - _prev_blip_us))]++;
      else _hist_primed = true;
      _prev_blip_us = lb;
    }

    _clicks.fire((uint32_t)lb);
  }
}
