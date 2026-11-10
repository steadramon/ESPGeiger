/*
  EGTune.h - RTTTL or step tunes, no heap

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

#ifndef EGTUNE_H
#define EGTUNE_H

#include <stdint.h>

// steps, comma separated:
//   2700:100 tone Hz:ms, @25 duty %, 1500>3000:200 sweep, p50 or 0:50 rest,
//   c / c2 pulse ms, t / t4 burst cycles, x3(...) plays it 3 times
// RTTTL: name:d=4,o=5,b=100:8c#6.,p,...
namespace EGTune {

enum Op : uint8_t { TONE, REST, PULSE, BURST };
enum Err : uint8_t { OK, BAD, RANGE, TOO_MANY, TOO_LONG, GROUP };

static constexpr uint8_t MAX_PULSE_MS = 10;
static constexpr uint8_t MIN_BURST    = 2;
static constexpr uint8_t MAX_BURST    = 5;
static constexpr uint8_t DEF_BURST    = 3;
static constexpr uint8_t MAX_REPEAT   = 99;

struct Step {
  uint16_t hz;     // burst: 0 = player's
  uint16_t to;     // sweep end, 0 = steady
  uint16_t ms;     // burst: cycles
  uint8_t  op;
  uint8_t  duty;   // 0 = player's
};

// limits: steps, ms a step, ms in all (bursts not counted)
struct Caps {
  uint8_t  steps;
  uint16_t step_ms;
  uint16_t total_ms;
};
static constexpr Caps DEFAULTS = {64, 5000, 15000};

class Reader {
 public:
  // s isn't copied, keep it until done. No NUL needed
  void begin(const char* s, uint16_t len, Caps caps = DEFAULTS);
  // false at the end or on an error
  bool next(Step& st);
  uint8_t err() const { return _err; }   // Err
  uint16_t at() const { return _at; }   // where the bad bit starts
  bool rtttl() const { return _rtttl; }

 private:
  bool is_rtttl() const;
  void header();
  __attribute__((warn_unused_result))
  bool set(char k, uint32_t v);
  bool group(char c);
  void open();
  void close();
  bool step(Step& st);
  bool tone(Step& st);
  bool note(Step& st);
  bool dotted();
  uint32_t beat(uint32_t dur, bool dot) const;
  bool pitch(Step& st, int8_t semi, uint32_t oct, uint32_t ms);
  bool length(uint16_t& ms);
  bool opt(uint16_t& v, uint8_t def, uint8_t lo, uint8_t hi);
  bool num(uint32_t& v, uint32_t max);
  bool ends();
  bool count(const Step& st);
  bool eat(char c);
  bool fail(uint8_t e);
  char peek() const { return _pos < _len ? _s[_pos] : 0; }
  void ws();
  void skip();

  const char* _s = nullptr;
  uint16_t _len = 0;
  uint16_t _pos = 0;
  uint16_t _tok = 0;
  uint16_t _at = 0;
  uint16_t _grp = 0;
  uint16_t _n = 0;
  uint32_t _total = 0;
  Caps _caps = DEFAULTS;
  uint16_t _bpm = 63;
  uint8_t _dur = 4;
  uint8_t _oct = 6;
  uint8_t _left = 0;
  uint8_t _err = OK;
  bool _rtttl = false;
  bool _ingrp = false;
};

// the whole tune, returns Err, at = where it went wrong
uint8_t check(const char* s, uint16_t len, uint16_t* at = nullptr, Caps caps = DEFAULTS);

}

#endif
