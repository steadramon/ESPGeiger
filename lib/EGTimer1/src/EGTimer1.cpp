/*
  EGTimer1.cpp - the one Timer1 owner for ESP8266 builds

  Copyright (C) 2026 @steadramon

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
#include "EGTimer1.h"

#if defined(ESP8266) && defined(EG_TIMER1)

#include <Arduino.h>
#include "ets_sys.h"
#include "core_esp8266_waveform.h"
#include "EGTimer1Nmi.h"
#include "EGTimer1Pwm.h"
#include "EGTimer1Soft.h"

#ifdef EG_SERIAL_NMI
#include "EGTimer1Rx.h"
// cycles, not scaled at 160 MHz, set high
#ifndef EG_RX_GPIO_LAT
#define EG_RX_GPIO_LAT 480
#endif
// cycles early, the NMI is only ever late
#ifndef EG_RX_BIAS
#define EG_RX_BIAS 120
#endif
static constexpr uint32_t RX_EARLY_TICKS = 200;
// slower baud, the pin isr catches the next start bit
static constexpr uint32_t RX_SPIN_MAX_TICKS = 1000;
static constexpr uint32_t RX_RING = 256;
static_assert((RX_RING & (RX_RING - 1)) == 0, "ring size must be a power of two");
#endif

// EG_TIMER1_STATS delays the gpio isrs

// always_inline keeps it in IRAM
static inline __attribute__((always_inline)) uint32_t ccount() {
  uint32_t c;
  __asm__ __volatile__("rsr %0,ccount" : "=a"(c));
  return c;
}

// no memw between the spin and the pin
static inline __attribute__((always_inline)) void reg_write(volatile uint32_t& r, uint32_t v) {
  __asm__ __volatile__("s32i %0, %1, 0" : : "a"(v), "a"(&r) : "memory");
}

// GPIO16 has its own register
static inline __attribute__((always_inline)) void drive(uint32_t set, uint32_t clr) {
  if (set) reg_write(GPOS, set);
  if (clr) reg_write(GPOC, clr);
  if ((set | clr) & (1u << 16)) reg_write(GP16O, (clr & (1u << 16)) ? 0 : 1);
}

// 1 at 160 MHz, cycles are 2x ticks
static constexpr uint8_t X2 = F_CPU == 160000000L ? 1 : 0;

static constexpr uint32_t START_TICKS = 800;    // 10 us

// wake 4 us before a fall, spin the rest
#ifndef EG_TIMER1_EARLY
#define EG_TIMER1_EARLY 320
#endif

// +2 us for a soft pass
static constexpr uint32_t SPIN_TICKS = EGT1_LEAD_TICKS + EG_TIMER1_EARLY + 160;

static constexpr uint8_t PASSES = 8;

static constexpr uint32_t POST_TIMEOUT_US = 2000;

// shorter = 0 % or 100 %
static constexpr uint32_t MIN_PHASE_US = 2;

// hot bytes first, not volatile, main uses vload/vstore
struct Gen {
  uint8_t pwm_on;
  uint8_t idle = 1;       // timer not armed
  uint8_t x2;
  uint8_t reset = 1;
  uint32_t entries;
  uint32_t soft_mask;
  uint32_t pwm_bit;
  uint32_t cmd_seq;
  uint32_t done;
  uint32_t ok_bits;       // request n at bit n % 32
  EGT1Cmd cmd;
  EGT1Soft soft[EGT1_SOFT_MAX];
  EGT1PwmState pwm;
  EGT1PwmMailbox mb;
  uint8_t tmr_on;
  uint32_t tmr_next;
  uint32_t (*tmr_fn)();
#ifdef EG_SERIAL_NMI
  EGT1Rx rx;
  uint8_t rx_pin;
  uint8_t rx_spin;
  uint8_t rx_paused;
  uint32_t rx_bit;
  uint32_t rx_window;     // cycles
  uint32_t rx_head;
  uint32_t rx_ovf, rx_bytes;
#endif
#ifdef EG_TIMER1_STATS
  // cpu cycles
  uint32_t late_1us;
  uint32_t rise_cc;
  uint8_t have_rise;
  uint32_t on_min, on_max, per_min, per_max, late_max, isr_max;
  uint32_t on_sum, on_n, late_n;
  uint32_t isr_sum;       // wraps ~50 s at 80 MHz
  uint32_t spin_sum;
  uint32_t wake_cc;
  uint8_t wake_ok;
  uint32_t ent_min, ent_max, ent_sum, ent_n;
  uint32_t skips0;
#endif
};
static Gen g;

template <typename T> static inline T vload(const T& x) { return *(const volatile T*)&x; }
template <typename T> static inline void vstore(T& x, T v) { *(volatile T*)&x = v; }

static uint8_t s_pwm_pin = 0xFF;
static uint32_t s_seq;
static uint32_t s_post_limit;   // cpu cycles
static bool s_init;

#ifdef EG_SERIAL_NMI
static uint8_t s_rx_ring[RX_RING];
static uint32_t s_rx_tail;      // main side only
#endif

// O2, every callee always_inline, so no call leaves IRAM
#pragma GCC push_options
#pragma GCC optimize("O2")

static inline __attribute__((always_inline)) void on_edge(uint8_t action, uint32_t t,
                                                          uint32_t due) {
  if (action == EGT1_PWM_RISE) {
    reg_write(GPOS, g.pwm_bit);
#ifdef EG_TIMER1_STATS
    if (g.have_rise) {
      uint32_t per = t - g.rise_cc;
      if (per < g.per_min) g.per_min = per;
      if (per > g.per_max) g.per_max = per;
    }
    g.rise_cc = t;
    g.have_rise = 1;
#endif
  } else {
    reg_write(GPOC, g.pwm_bit);
#ifdef EG_TIMER1_STATS
    uint32_t on = t - g.rise_cc;
    if (on < g.on_min) g.on_min = on;
    if (on > g.on_max) g.on_max = on;
    g.on_sum += on;
    g.on_n++;
#endif
  }
#ifdef EG_TIMER1_STATS
  if (action == EGT1_PWM_RISE) return;   // the fall carries the timing
  uint32_t late = t - due;
  if (late > g.late_max) g.late_max = late;
  if (late > g.late_1us) g.late_n++;
#else
  (void)t;
  (void)due;
#endif
}

#ifdef EG_SERIAL_NMI
static inline __attribute__((always_inline)) void rx_push(uint8_t b) {
  uint32_t h = g.rx_head;
  if (h - vload(s_rx_tail) < RX_RING) {
    s_rx_ring[h & (RX_RING - 1)] = b;
    g.rx_head = h + 1;
    g.rx_bytes++;
  } else {
    g.rx_ovf++;
  }
}

// clear the stale edge first
static inline __attribute__((always_inline)) void rx_listen() {
  if (g.rx_paused) return;
  GPIEC = g.rx_bit;
  GPC(g.rx_pin) = (GPC(g.rx_pin) & ~(0xFu << GPCI)) | ((uint32_t)FALLING << GPCI);
}

static inline __attribute__((always_inline)) void rx_service() {
  uint32_t t = g.rx.next;
  if ((int32_t)(t - ccount()) > (int32_t)g.rx_window) return;
  uint32_t now;
  while ((int32_t)(t - (now = ccount())) > 0) {}
  bool v = (GPI >> g.rx_pin) & 1;
  uint8_t a = egrx_sample(g.rx, v, now - t);
  if (a == EGRX_NEXT) return;
  if (a != EGRX_BYTE) {
    rx_push(EGRX_MARK);
    rx_listen();
    return;
  }
  rx_push(g.rx.cur);
  bool high = true, waited = false;
  if (g.rx_spin) {
    uint32_t until = t + g.rx.cpb;
    for (;;) {
      now = ccount();
      high = (GPI >> g.rx_pin) & 1;
      if (!high || (int32_t)(until - now) <= 0) break;
      waited = true;
    }
  }
  if (high) {
    g.rx.on = 0;
    rx_listen();
  } else {
    egrx_resync(g.rx, t, now, waited, EG_RX_BIAS);
  }
}
#endif

static inline __attribute__((always_inline)) uint8_t pwm_pass(uint8_t x2) {
  uint32_t due;
  uint32_t window = (g.pwm.high ? SPIN_TICKS : EGT1_LEAD_TICKS) << x2;
  uint8_t a = egt1_pwm_step(g.pwm, g.mb, ccount(), x2, window, &due);
  if (a != EGT1_PWM_RISE && a != EGT1_PWM_FALL) return a;
  uint32_t t = ccount();
#ifdef EG_TIMER1_STATS
  int32_t slack = (int32_t)(due - t);
#endif
  while ((int32_t)(due - (t = ccount())) > 0) {}
  on_edge(a, t, due);
  if (a == EGT1_PWM_RISE) egt1_pwm_fall_from(g.pwm, t, x2);
  else egt1_pwm_late(g.pwm, t - due, x2);
#ifdef EG_TIMER1_STATS
  if (slack > 0) g.spin_sum += (uint32_t)slack;
#endif
  return a;
}

static inline __attribute__((always_inline)) uint32_t pwm_early() {
  return g.pwm.high ? EG_TIMER1_EARLY : 0;
}

static inline __attribute__((always_inline)) void tmr_pass(uint32_t now, uint8_t x2) {
  if ((int32_t)(g.tmr_next - now) > 0) return;
  uint32_t r = g.tmr_fn();
  if (!r) {
    g.tmr_on = 0;
    return;
  }
  if (r > (0x7FFFFFFFu >> x2)) r = 0x7FFFFFFFu >> x2;
  r <<= x2;
  g.tmr_next += r;
  if ((int32_t)(g.tmr_next - now) <= 0) g.tmr_next = now + r;
}

static inline __attribute__((always_inline)) void finish(uint32_t t0, uint32_t d, uint8_t x2) {
  g.entries++;
#ifdef EG_TIMER1_STATS
  // kick() entries not counted
  if (g.wake_ok) {
    int32_t e = (int32_t)(t0 - g.wake_cc);
    if (e >= 0) {
      if ((uint32_t)e < g.ent_min) g.ent_min = e;
      if ((uint32_t)e > g.ent_max) g.ent_max = e;
      g.ent_sum += e;
      g.ent_n++;
    }
    g.wake_ok = 0;
  }
  uint32_t isr = ccount() - t0;
  if (isr > g.isr_max) g.isr_max = isr;
  g.isr_sum += isr;
  if (d) {
    g.wake_cc = ccount() + (d << x2);
    g.wake_ok = 1;
  }
#else
  (void)t0;
  (void)x2;
#endif
  if (d) reg_write(T1L, d);
}

static inline __attribute__((always_inline)) bool edge_pass(uint32_t t0, uint8_t x2) {
  uint32_t due, t;
  uint32_t window = (g.pwm.high ? SPIN_TICKS : EGT1_LEAD_TICKS) << x2;
  uint8_t a = egt1_pwm_step(g.pwm, g.mb, ccount(), x2, window, &due);
  if (a == EGT1_PWM_FALL) {
    uint32_t d = egt1_arm(true, g.pwm.next, false, 0, due, x2);
#ifdef EG_TIMER1_STATS
    int32_t slack = (int32_t)(due - ccount());
    if (slack > 0) g.spin_sum += (uint32_t)slack;
#endif
    while ((int32_t)(due - (t = ccount())) > 0) {}
    on_edge(a, t, due);
    reg_write(T1L, d);
    egt1_pwm_late(g.pwm, t - due, x2);
    g.idle = 0;
    finish(t0, 0, x2);
    return true;
  }
  if (a != EGT1_PWM_RISE) return false;
  while ((int32_t)(due - (t = ccount())) > 0) {}
  on_edge(a, t, due);
  egt1_pwm_fall_from(g.pwm, t, x2);
  g.idle = 0;
  finish(t0, egt1_arm(true, g.pwm.next - (EG_TIMER1_EARLY << x2), false, 0, ccount(), x2), x2);
  return true;
}

#ifdef EG_TIMER1_STATS
static inline __attribute__((always_inline)) void stats_reset() {
  g.on_min = g.per_min = UINT32_MAX;
  g.on_max = g.per_max = g.late_max = g.isr_max = 0;
  g.on_sum = g.on_n = g.late_n = 0;
  g.isr_sum = g.spin_sum = 0;
  g.ent_min = UINT32_MAX;
  g.ent_max = g.ent_sum = g.ent_n = 0;
  g.skips0 = g.pwm.skips;
  g.reset = 0;
}
#endif

static inline __attribute__((always_inline)) void apply_cmd() {
  g.soft_mask = egt1_apply(g.soft, EGT1_SOFT_MAX, g.cmd, ccount());
  uint32_t b = 1u << (g.cmd_seq & 31);
  g.ok_bits = g.cmd.ok ? (g.ok_bits | b) : (g.ok_bits & ~b);
  g.done = g.cmd_seq;
}

struct Pass {
  uint32_t set = 0, clr = 0;
  uint32_t soft_at = 0;
  uint8_t soft = 0;
  bool stepped = false;
};

static inline __attribute__((always_inline)) void edges(Pass& p, uint8_t x2, bool pwm_on, bool pwm_only) {
  for (uint8_t i = 0; i < PASSES; i++) {
    if (pwm_on && pwm_pass(x2) != EGT1_PWM_WAIT) continue;
    if (pwm_only) return;
    p.soft = egt1_soft_step(g.soft, EGT1_SOFT_MAX, ccount(), &p.set, &p.clr, &p.soft_at);
    p.stepped = true;
    // soft edges may eat the PWM slack, look again
    if (!pwm_on || !(p.soft & EGT1_STEP_EDGE)) return;
    drive(p.set, p.clr);
    p.set = p.clr = 0;
  }
}

static inline __attribute__((always_inline)) uint32_t next_wake(Pass& p, uint8_t x2, bool pwm_on) {
  if (p.soft & EGT1_STEP_FREED) {
    uint32_t m = 0;
    for (uint8_t k = 0; k < EGT1_SOFT_MAX; k++) m |= g.soft[k].mask;
    g.soft_mask = m;
  }
  uint32_t now = ccount();
  if (!p.stepped) p.soft = egt1_soft_next(g.soft, EGT1_SOFT_MAX, now, &p.soft_at) ? EGT1_STEP_RUNS : 0;
  // timed callback armed like a soft edge
  if (g.tmr_on) {
    tmr_pass(now, x2);
    if (g.tmr_on && (!(p.soft & EGT1_STEP_RUNS) || (int32_t)(g.tmr_next - p.soft_at) < 0)) {
      p.soft_at = g.tmr_next;
      p.soft |= EGT1_STEP_RUNS;
    }
  }
  bool p_on = pwm_on;
  uint32_t p_at = g.pwm.next - (pwm_early() << x2);
#ifdef EG_SERIAL_NMI
  if (g.rx.on) {
    p_on = true;
    p_at = g.rx.next - (RX_EARLY_TICKS << x2);
  }
#endif
  uint32_t d = egt1_arm(p_on, p_at, p.soft & EGT1_STEP_RUNS, p.soft_at, now, x2);
  if (!d && g.cmd.op) d = EGT1_LEAD_TICKS;
  return d;
}

extern "C" void IRAM_ATTR egt1_nmi() {
#ifdef EG_TIMER1_STATS
  uint32_t t0 = ccount();
  if (g.reset) stats_reset();
#else
  const uint32_t t0 = 0;
#endif
  if (g.cmd.op) apply_cmd();
#ifdef EG_SERIAL_NMI
  if (g.rx.on) rx_service();
#endif
  constexpr uint8_t x2 = X2;
#ifdef EG_TIMER1_PWM
  bool pwm_on = g.pwm_on;
#else
  // keeps pwm out of IRAM
  constexpr bool pwm_on = false;
#endif
  bool pwm_only = pwm_on && !g.soft_mask && !g.tmr_on;
  if (pwm_only && edge_pass(t0, x2)) return;
  Pass p;
  edges(p, x2, pwm_on, pwm_only);
  if (pwm_only) {
    uint32_t pwm_at = g.pwm.next - (pwm_early() << x2);
    g.idle = 0;
    finish(t0, egt1_arm(true, pwm_at, false, 0, ccount(), x2), x2);
    return;
  }
  uint32_t d = next_wake(p, x2, pwm_on);
  g.idle = d ? 0 : 1;
  // register writes last
  drive(p.set, p.clr);
  finish(t0, d, x2);
}

#pragma GCC pop_options

// for EGTimer1Nmi.S
static_assert(EGT1_NMI_STACK % 16 == 0, "the frame starts 16-byte aligned");
extern "C" {
uint32_t egt1_nmi_stack[(EGT1_NMI_STACK + EGT1_NMI_FRAME) / 4] __attribute__((aligned(16)));
void egt1_nmi_entry();
}

// only if the bytes match, else the SDK path stays
static bool patch_nmi_vector() {
  volatile uint32_t* v = (volatile uint32_t*)0x40100020;
  uint32_t w0 = v[0], w1 = v[1];
  if ((w0 & 0x00FFFFFF) != 0x0013D300) return false;   // wsr.excsave3 a0
  if (((w0 >> 24) & 0x3F) != 0x05) return false;      // call0
  uint32_t target = (uint32_t)egt1_nmi_entry;
  int32_t off = ((int32_t)target - 0x40100024) >> 2;
  if ((target & 3) || off < -(1 << 17) || off >= (1 << 17)) return false;
  uint32_t insn = 0x05u | (((uint32_t)off & 0x3FFFF) << 6);
  uint32_t ps = xt_rsil(15);
  v[0] = (w0 & 0x00FFFFFF) | (insn << 24);
  v[1] = (w1 & 0xFFFF0000) | (insn >> 8);
  __asm__ __volatile__("isync" ::: "memory");
  xt_wsr_ps(ps);
  return true;
}
static bool s_fast_nmi;

static void arm_timer() {
  timer1_disable();
  ETS_FRC_TIMER1_INTR_ATTACH(NULL, NULL);
  ETS_FRC_TIMER1_NMI_INTR_ATTACH(egt1_nmi);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_SINGLE);
  vstore(g.idle, (uint8_t)1);
}

static void init_timer() {
  if (s_init) return;
  uint32_t mhz = ESP.getCpuFreqMHz();
  s_post_limit = POST_TIMEOUT_US * mhz;
#ifdef EG_TIMER1_STATS
  g.late_1us = mhz;
#endif
  g.x2 = X2;
  timer1_disable();
  s_fast_nmi = patch_nmi_vector();
  arm_timer();
  s_init = true;
}

// only ever brings it forward
static inline void IRAM_ATTR kick() {
  if (vload(g.idle) || T1V > EGT1_LEAD_TICKS) timer1_write(EGT1_LEAD_TICKS);
}

#ifdef EG_SERIAL_NMI
// CHANGE, the core drops a late FALLING
static void IRAM_ATTR rx_on_start() {
  uint32_t entry = ccount();
  GPC(g.rx_pin) &= ~(0xFu << GPCI);
  if (vload(g.rx.on)) return;
  egrx_start(g.rx, entry, EG_RX_GPIO_LAT, EG_RX_BIAS);
  int32_t d = (int32_t)(g.rx.next - (RX_EARLY_TICKS << g.x2) - ccount()) >> g.x2;
  if (d < (int32_t)EGT1_LEAD_TICKS) d = EGT1_LEAD_TICKS;
  if (vload(g.idle) || T1V > (uint32_t)d) timer1_write((uint32_t)d);
}
#endif

// irqs off only to fill the slot
static bool IRAM_ATTR post(uint8_t op, uint32_t mask, uint32_t high, uint32_t low,
                           uint32_t run) {
  if (!s_init) return false;
  uint32_t t0 = ccount();
  uint32_t ps;
  for (;;) {
    ps = xt_rsil(15);
    if (!g.cmd.op) break;
    xt_wsr_ps(ps);
    if (ccount() - t0 >= s_post_limit) return false;
  }
  uint32_t seq = ++s_seq;
  g.cmd.mask = mask;
  g.cmd.high = high;
  g.cmd.low = low;
  g.cmd.run = run;
  g.cmd.ok = 0;
  g.cmd_seq = seq;
  __asm__ __volatile__("" ::: "memory");
  g.cmd.op = op;
  kick();
  xt_wsr_ps(ps);
  while ((int32_t)(vload(g.done) - seq) < 0) {
    if (ccount() - t0 >= s_post_limit) {
      ps = xt_rsil(15);
      if (vload(g.cmd_seq) == seq) g.cmd.op = EGT1_CMD_NONE;
      xt_wsr_ps(ps);
      break;
    }
  }
  if ((int32_t)(vload(g.done) - seq) < 0) return false;
  return (vload(g.ok_bits) >> (seq & 31)) & 1;
}

// whole core waveform API, missing one = duplicate symbol
extern "C" {

int startWaveformClockCycles(uint8_t pin, uint32_t high, uint32_t low, uint32_t run,  // NOLINT(mixed-case,complexity) core API
                             int8_t, uint32_t, bool) {
  if (pin > 16 || isFlashInterfacePin(pin) || pin == s_pwm_pin) return false;
  init_timer();
  uint32_t bit = 1u << pin;
  uint32_t min = microsecondsToClockCycles(MIN_PHASE_US);
  if (high < min || low < min) {
    if (vload(g.soft_mask) & bit) post(EGT1_CMD_STOP, bit, 0, 0, 0);
    if (high < min) drive(0, bit);
    else drive(bit, 0);
    return true;
  }
  return post(EGT1_CMD_START, bit, high, low, run);
}

int startWaveform(uint8_t pin, uint32_t high_us, uint32_t low_us, uint32_t run_us,  // NOLINT(mixed-case,complexity) core API
                  int8_t align, uint32_t offset_us, bool auto_pwm) {
  return startWaveformClockCycles(pin, microsecondsToClockCycles(high_us),
                                  microsecondsToClockCycles(low_us),
                                  microsecondsToClockCycles(run_us), align,
                                  microsecondsToClockCycles(offset_us), auto_pwm);
}

IRAM_ATTR int stopWaveform(uint8_t pin) {  // NOLINT(mixed-case) core API
  if (pin > 16 || !(vload(g.soft_mask) & (1u << pin))) return false;
  return post(EGT1_CMD_STOP, 1u << pin, 0, 0, 0);
}

// refused, nothing else gets Timer1
void setTimer1Callback(uint32_t (*fn)()) { (void)fn; }  // NOLINT(mixed-case) core API

// false = analogWrite uses the waveform path
void _setPWMFreq(uint32_t freq) { (void)freq; }  // NOLINT(mixed-case) core API
IRAM_ATTR bool _stopPWM(uint8_t pin) { (void)pin; return false; }  // NOLINT(mixed-case) core API
bool _setPWM(int pin, uint32_t val, uint32_t range) {  // NOLINT(mixed-case) core API
  (void)pin; (void)val; (void)range;
  return false;
}

}

namespace EGTimer1 {

// no flag = link error, not silent
#ifdef EG_TIMER1_PWM
// GPIO16 isn't on GPOS/GPOC
bool pwm_begin(uint8_t pin) {
  if (pin > 15 || isFlashInterfacePin(pin)) return false;
#ifdef EG_SERIAL_NMI
  if (g.rx_bit) return false;   // rx has the precise channel
#endif
  init_timer();
  vstore(g.pwm_on, (uint8_t)0);
  if (vload(g.soft_mask) & (1u << pin)) post(EGT1_CMD_STOP, 1u << pin, 0, 0, 0);
  s_pwm_pin = pin;
  vstore(g.pwm_bit, 1u << pin);
  GPOC = g.pwm_bit;
  g.pwm = EGT1PwmState{};
  g.mb.ready = 0;
#ifdef EG_TIMER1_STATS
  g.have_rise = 0;
#endif
  vstore(g.reset, (uint8_t)1);
  g.pwm.next = ccount() + (START_TICKS << g.x2);
  __asm__ __volatile__("" ::: "memory");
  vstore(g.pwm_on, (uint8_t)1);
  uint32_t ps = xt_rsil(15);
  kick();
  xt_wsr_ps(ps);
  return true;
}

void pwm_set(uint32_t freq, uint32_t duty, uint32_t range) {
  uint32_t period, on;
  egt1_pwm_ticks(freq, duty, range, &period, &on);
  egt1_pwm_post(g.mb, period, on);
}

// the pin stays refused till reboot
void pwm_stop() {
  // pwm_on first, so the NMI leaves the pin low
  vstore(g.pwm_on, (uint8_t)0);
  uint32_t bit = vload(g.pwm_bit);
  if (bit) GPOC = bit;
}

bool pwm_running() { return vload(g.pwm_on); }
#endif

uint32_t entries() { return vload(g.entries); }

void restart() {
  if (!s_init) return;
  arm_timer();
  uint32_t ps = xt_rsil(15);
  kick();
  xt_wsr_ps(ps);
}

void timer_start(uint32_t (*fn)(), uint32_t first) {
  if (!fn) return;
  init_timer();
  vstore(g.tmr_on, (uint8_t)0);
  g.tmr_fn = fn;
  g.tmr_next = ccount() + (first << g.x2);
  __asm__ __volatile__("" ::: "memory");
  vstore(g.tmr_on, (uint8_t)1);
  uint32_t ps = xt_rsil(15);
  kick();
  xt_wsr_ps(ps);
}

void timer_stop() { vstore(g.tmr_on, (uint8_t)0); }

#ifdef EG_TIMER1_STATS
// ns unless named
static struct {
  uint32_t n, skips, on_min, on_max, on_avg, per_min, per_max, late_max, late_n;
  uint32_t isr_max, isr_us, spin_us, ent_min, ent_avg, ent_max;
} s_st;

void stats_tick() {
  static uint32_t last_entries;
  uint32_t n = entries();
  s_st.n = n - last_entries;
  last_entries = n;
  uint32_t mhz = ESP.getCpuFreqMHz();
  auto ns = [mhz](uint32_t cyc) { return (uint32_t)((uint64_t)cyc * 1000u / mhz); };
  s_st.skips = vload(g.pwm.skips) - g.skips0;
  s_st.on_min = g.on_min == UINT32_MAX ? 0 : ns(g.on_min);
  s_st.on_max = ns(g.on_max);
  s_st.on_avg = g.on_n ? ns(g.on_sum / g.on_n) : 0;
  s_st.per_min = g.per_min == UINT32_MAX ? 0 : ns(g.per_min);
  s_st.per_max = ns(g.per_max);
  s_st.late_max = ns(g.late_max);
  s_st.late_n = g.late_n;
  s_st.isr_max = ns(g.isr_max);
  s_st.isr_us = g.isr_sum / mhz;
  s_st.spin_us = g.spin_sum / mhz;
  s_st.ent_min = g.ent_n ? ns(g.ent_min) : 0;
  s_st.ent_avg = g.ent_n ? ns(g.ent_sum / g.ent_n) : 0;
  s_st.ent_max = ns(g.ent_max);
  vstore(g.reset, (uint8_t)1);
}

int stats_json(char* buf, size_t cap, uint8_t part) {
  switch (part) {
    case 0:
      return snprintf_P(buf, cap,
        PSTR(",\"pw_n\":%lu,\"pw_skip\":%lu,\"pw_onlo\":%lu,\"pw_onhi\":%lu,\"pw_onavg\":%lu"
             ",\"pw_plo\":%lu,\"pw_phi\":%lu"),
        (unsigned long)s_st.n, (unsigned long)s_st.skips, (unsigned long)s_st.on_min,
        (unsigned long)s_st.on_max, (unsigned long)s_st.on_avg, (unsigned long)s_st.per_min,
        (unsigned long)s_st.per_max);
    case 1:
      return snprintf_P(buf, cap,
        PSTR(",\"pw_late\":%lu,\"pw_late1\":%lu,\"pw_soft\":%lu,\"pw_isr\":%lu"
             ",\"pw_isrt\":%lu,\"pw_spin\":%lu"),
        (unsigned long)s_st.late_max, (unsigned long)s_st.late_n,
        (unsigned long)vload(g.soft_mask), (unsigned long)s_st.isr_max,
        (unsigned long)s_st.isr_us, (unsigned long)s_st.spin_us);
    case 2:
      return snprintf_P(buf, cap,
        PSTR(",\"pw_entlo\":%lu,\"pw_ent\":%lu,\"pw_enthi\":%lu,\"pw_fast\":%u"),
        (unsigned long)s_st.ent_min, (unsigned long)s_st.ent_avg,
        (unsigned long)s_st.ent_max, (unsigned)s_fast_nmi);
    default:
      return 0;
  }
}
#endif

#ifdef EG_SERIAL_NMI
bool rx_begin(int8_t pin, uint32_t baud) {
  if (pin < 0 || pin > 15 || isFlashInterfacePin(pin) || (uint8_t)pin == s_pwm_pin || !baud) {
    return false;
  }
  if (vload(g.pwm_on)) return false;   // pwm has the precise channel
  init_timer();
  uint32_t cpu = ESP.getCpuFreqMHz() * 1000000UL;
  g.rx.cpb = (cpu + baud / 2) / baud;
  g.rx_window = (2 * RX_EARLY_TICKS) << g.x2;
  g.rx_spin = (g.rx.cpb >> g.x2) <= RX_SPIN_MAX_TICKS;
  g.rx_pin = (uint8_t)pin;
  g.rx_bit = 1u << pin;
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), rx_on_start, CHANGE);
  uint32_t ps = xt_rsil(15);
  rx_listen();
  xt_wsr_ps(ps);
  return true;
}

int rx_available() { return (int)(vload(g.rx_head) - s_rx_tail); }

int rx_read() {
  uint32_t t = s_rx_tail;
  if (vload(g.rx_head) == t) return -1;
  uint8_t b = s_rx_ring[t & (RX_RING - 1)];
  vstore(s_rx_tail, t + 1);
  return b;
}

void rx_pause() {
  vstore(g.rx_paused, (uint8_t)1);
  GPC(g.rx_pin) &= ~(0xFu << GPCI);
  vstore(g.rx.on, (uint8_t)0);
}

void rx_resume() {
  vstore(g.rx_paused, (uint8_t)0);
  uint32_t ps = xt_rsil(15);
  rx_listen();
  xt_wsr_ps(ps);
}

void rx_stats(RxStats* out) {
  out->starts = vload(g.rx.starts);
  out->resyncs = vload(g.rx.resyncs);
  out->framing = vload(g.rx.framing);
  out->framing_first = vload(g.rx.framing_first);
  out->late = vload(g.rx.late);
  out->already_low = vload(g.rx.already_low);
  out->ovf = vload(g.rx_ovf);
  out->bytes = vload(g.rx_bytes);
}
#endif

}

#endif
