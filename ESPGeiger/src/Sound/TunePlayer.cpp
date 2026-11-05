/*
  TunePlayer.cpp - steps to buzzer calls: tone chunks, sweep slices, pulses, bursts.

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
#include "TunePlayer.h"
#include <string.h>

namespace TunePlayer {

namespace {

uint32_t period_of(uint16_t hz) { return 1000000UL / hz; }

// pulse and burst: one call, then the step's over
bool one(const EGTune::Step& st, uint32_t done, const Config& c, Slice& out) {
  if (done) return false;
  out.op = st.op;
  if (st.op == EGTune::PULSE) {
    out.n = st.ms * 1000;
    out.us = out.n;
    return true;
  }
  out.hz = st.hz ? st.hz : c.burst_hz;
  if (!out.hz) out.hz = DEFAULTS.burst_hz;
  out.n = st.ms;
  out.us = st.ms * period_of(out.hz);
  return true;
}

uint16_t sweep_hz(const EGTune::Step& st, uint32_t done) {
  int32_t d = (int32_t)st.to - st.hz;
  return st.hz + d * (int32_t)(done / 1000) / st.ms;
}

}

// tones in chunks the rmt can loop, sweeps in slices, rtttl notes end quiet
bool slice(const EGTune::Step& st, uint32_t done, bool rtttl, const Config& c, Slice& out) {
  out = Slice();
  if (st.op == EGTune::PULSE || st.op == EGTune::BURST) return one(st, done, c, out);
  uint32_t len = st.ms * 1000UL;
  if (done >= len) return false;
  uint32_t gap = c.gap_pct < 100 ? c.gap_pct : 100;
  uint32_t on = rtttl ? len - len * gap / 100 : len;
  out.op = EGTune::REST;
  out.us = len - done;
  if (st.op == EGTune::REST || done >= on) return true;
  uint16_t hz = st.to ? sweep_hz(st, done) : st.hz;
  uint32_t period = period_of(hz);
  uint32_t left = on - done;
  if (st.to && left > SWEEP_US) left = SWEEP_US;
  uint32_t n = left / period;
  if (!n) return true;   // under a cycle left
  if (n > corium::Buzzer::MAX_CYCLES) n = corium::Buzzer::MAX_CYCLES;
  out.op = EGTune::TONE;
  out.hz = hz;
  out.n = n;
  out.duty = st.duty ? st.duty : c.duty_pct;
  out.us = n * period;
  return true;
}

#if BUZZER_BUILT

namespace {

Config s_cfg = DEFAULTS;
char s_text[MAX_TEXT + 1];   // pref values can be freed mid-tune
EGTune::Reader s_rd;
EGTune::Step s_st;
uint32_t s_done = 0;
uint32_t s_at = 0;
uint8_t s_prio = 0;
uint8_t s_vol = 100;
bool s_on = false;
bool s_have = false;

void send(uint32_t now, const Slice& sl) {
  if (sl.op == EGTune::TONE) corium::Buzzer::tone(now, sl.hz, sl.n, sl.duty);
  else if (sl.op == EGTune::PULSE) corium::Buzzer::pulse(now, sl.n);
  else if (sl.op == EGTune::BURST) corium::Buzzer::burst(now, sl.hz, sl.n);
}

}

void begin(Config c) { s_cfg = c; }

bool play(uint32_t now, const char* s, uint16_t len, uint8_t prio, uint8_t vol) {
  if (s_on && prio <= s_prio) return false;
  if (len > MAX_TEXT) len = MAX_TEXT;
  memcpy(s_text, s, len);
  s_rd.begin(s_text, len);
  s_prio = prio;
  s_vol = vol;
  s_have = false;
  s_on = true;
  s_at = now;
  return true;
}

void stop() { s_on = false; }

bool busy() { return s_on; }

uint32_t poll(uint32_t now) {
  if (!s_on) return 0;
  if ((int32_t)(now - s_at) < 0) return s_at - now;
  for (;;) {
    if (!s_have) {
      if (!s_rd.next(s_st)) {
        s_on = false;
        return 0;
      }
      s_have = true;
      s_done = 0;
    }
    Slice sl;
    if (!slice(s_st, s_done, s_rd.rtttl(), s_cfg, sl)) {
      s_have = false;
      continue;
    }
    if (sl.op != EGTune::REST) {
      // budget after a pulse or burst: wait, don't drop
      uint32_t w = corium::Buzzer::waitUs(now);
      if (w) return w;
      if (sl.op == EGTune::TONE) sl.duty = at_volume(sl.duty, s_vol);
      send(now, sl);
    }
    s_done += sl.us;
    s_at = now + sl.us;
    return sl.us;
  }
}

#endif

}
