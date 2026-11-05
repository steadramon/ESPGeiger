/*
  EGTune.cpp - RTTTL and step tunes, one step per call.

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
#include "EGTune.h"

namespace EGTune {

namespace {

const int8_t PAUSE = -1, NOT_NOTE = 12, OCTAVE = 12;
const uint8_t OCT_MIN = 3, OCT_MAX = 8;
const uint32_t WHOLE_NOTE_MS = 4 * 60000UL;

// octave 8, shift down for the rest
const uint16_t NOTE8[OCTAVE] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};
// semitone of a .. g
const uint8_t SEMI[7] = {9, 11, 0, 2, 4, 5, 7};

bool digit(char c) { return c >= '0' && c <= '9'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool pow2(uint32_t v) { return v && v <= 32 && !(v & (v - 1)); }

int8_t letter(char c) {
  c |= 0x20;
  if (c == 'p') return PAUSE;
  if (c == 'h') return 11;
  if (c >= 'a' && c <= 'g') return SEMI[c - 'a'];
  return NOT_NOTE;
}

}

void Reader::begin(const char* s, uint16_t len, Caps caps) {
  *this = Reader();
  _s = s;
  _len = len;
  _caps = caps;
  _rtttl = is_rtttl();
  if (_rtttl) header();
}

// name:d=  o=  b=
bool Reader::is_rtttl() const {
  uint16_t p = 0;
  while (p < _len && _s[p] != ':') p++;
  if (p >= _len) return false;
  p++;
  while (p < _len && space(_s[p])) p++;
  if (p >= _len || (_s[p] != 'd' && _s[p] != 'o' && _s[p] != 'b')) return false;
  p++;
  while (p < _len && space(_s[p])) p++;
  return p < _len && _s[p] == '=';
}

// up to the second colon
void Reader::header() {
  while (_s[_pos] != ':') _pos++;
  _pos++;
  for (;;) {
    skip();
    _tok = _pos;
    if (_pos >= _len) { fail(BAD); return; }
    if (_s[_pos] == ':') { _pos++; return; }
    char k = _s[_pos++];
    ws();
    if (!eat('=')) return;
    ws();
    uint32_t v;
    if (!num(v, 999) || !set(k, v)) return;
  }
}

bool Reader::set(char k, uint32_t v) {
  if (k == 'd') {
    if (!pow2(v)) return fail(RANGE);
    _d = v;
  } else if (k == 'o') {
    if (v < OCT_MIN || v > OCT_MAX) return fail(RANGE);
    _o = v;
  } else if (k == 'b') {
    if (!v || v > 900) return fail(RANGE);
    _b = v;
  } else {
    return fail(BAD);
  }
  return true;
}

bool Reader::next(Step& st) {
  while (!_err) {
    skip();
    _tok = _pos;
    if (_pos >= _len) return _ingrp ? fail(GROUP) : false;
    if (group(_s[_pos])) continue;
    st = Step();
    if (!(_rtttl ? note(st) : step(st)) || !ends()) return false;
    return count(st);
  }
  return false;
}

// steps only
bool Reader::group(char c) {
  if (_rtttl) return false;
  if (c == 'x') open();
  else if (c == ')') close();
  else return false;
  return true;
}

// no nesting
void Reader::open() {
  _pos++;
  uint32_t n;
  if (_ingrp) { fail(GROUP); return; }
  if (!num(n, MAX_REPEAT)) return;
  if (!n) { fail(RANGE); return; }
  if (!eat('(')) return;
  _ingrp = true;
  _left = n - 1;
  _grp = _pos;
}

void Reader::close() {
  if (!_ingrp) { fail(GROUP); return; }
  if (_left) {
    _left--;
    _pos = _grp;
    return;
  }
  _ingrp = false;
  _pos++;
}

bool Reader::step(Step& st) {
  char c = _s[_pos];
  if (c == 'p') {
    _pos++;
    st.op = REST;
    return length(st.ms);
  }
  if (c == 'c') {
    _pos++;
    st.op = PULSE;
    return opt(st.ms, 1, 1, MAX_PULSE_MS);
  }
  if (c == 't') {
    _pos++;
    st.op = BURST;
    return opt(st.ms, DEF_BURST, MIN_BURST, MAX_BURST);
  }
  return tone(st);
}

// hz[>to]:ms[@duty], hz 0 = rest
bool Reader::tone(Step& st) {
  uint32_t v;
  if (!num(v, 0xFFFF)) return false;
  st.hz = v;
  if (peek() == '>') {
    _pos++;
    if (!num(v, 0xFFFF)) return false;
    if (!v || !st.hz) return fail(st.hz ? RANGE : BAD);
    st.to = v;
  }
  if (!eat(':') || !length(st.ms)) return false;
  if (peek() == '@') {
    _pos++;
    if (!num(v, 100)) return false;
    if (!v) return fail(RANGE);
    st.duty = v;
  }
  st.op = st.hz ? TONE : REST;
  return true;
}

// [dur]note[#][.][oct][.]
bool Reader::note(Step& st) {
  uint32_t dur = _d, oct = _o;
  if (digit(peek()) && !num(dur, 32)) return false;
  if (!pow2(dur)) return fail(RANGE);
  int8_t semi = letter(peek());
  if (semi == NOT_NOTE) return fail(BAD);
  _pos++;
  if (semi >= 0 && peek() == '#') { _pos++; semi++; }
  bool dot = dotted();
  if (digit(peek()) && !num(oct, OCT_MAX)) return false;
  if (dotted()) dot = true;
  return pitch(st, semi, oct, beat(dur, dot));
}

bool Reader::dotted() {
  if (peek() != '.') return false;
  _pos++;
  return true;
}

uint32_t Reader::beat(uint32_t dur, bool dot) const {
  uint32_t ms = WHOLE_NOTE_MS / (_b * dur);
  return dot ? ms + ms / 2 : ms;
}

// b# = next octave's c
bool Reader::pitch(Step& st, int8_t semi, uint32_t oct, uint32_t ms) {
  if (semi == OCTAVE) { semi = 0; oct++; }
  if (oct < OCT_MIN || oct > OCT_MAX) return fail(RANGE);
  if (ms > _caps.step_ms) return fail(RANGE);
  st.ms = ms;
  st.op = semi == PAUSE ? REST : TONE;
  if (semi != PAUSE) st.hz = NOTE8[semi] >> (OCT_MAX - oct);
  return true;
}

bool Reader::length(uint16_t& ms) {
  uint32_t v;
  if (!num(v, _caps.step_ms)) return false;
  if (!v) return fail(RANGE);
  ms = v;
  return true;
}

bool Reader::opt(uint16_t& v, uint8_t def, uint8_t lo, uint8_t hi) {
  v = def;
  if (!digit(peek())) return true;
  uint32_t n;
  if (!num(n, hi)) return false;
  if (n < lo) return fail(RANGE);
  v = n;
  return true;
}

// digits, over max = RANGE
bool Reader::num(uint32_t& v, uint32_t max) {
  if (!digit(peek())) return fail(BAD);
  v = 0;
  while (digit(peek())) {
    v = v * 10 + (_s[_pos++] - '0');
    if (v > max) return fail(RANGE);
  }
  return true;
}

bool Reader::ends() {
  ws();
  char c = peek();
  return !c || c == ',' || c == ')' ? true : fail(BAD);
}

bool Reader::count(const Step& st) {
  if (++_n > _caps.steps) return fail(TOO_MANY);
  if (st.op != BURST) _total += st.ms;
  if (_total > _caps.total_ms) return fail(TOO_LONG);
  return true;
}

bool Reader::eat(char c) {
  if (peek() != c) return fail(BAD);
  _pos++;
  return true;
}

bool Reader::fail(uint8_t e) {
  if (!_err) {
    _err = e;
    _at = _tok;
  }
  return false;
}

void Reader::ws() {
  while (space(peek())) _pos++;
}

void Reader::skip() {
  while (space(peek()) || peek() == ',') _pos++;
}

uint8_t check(const char* s, uint16_t len, uint16_t* at, Caps caps) {
  Reader r;
  r.begin(s, len, caps);
  Step st;
  while (r.next(st)) {}
  if (at) *at = r.at();
  return r.err();
}

}
