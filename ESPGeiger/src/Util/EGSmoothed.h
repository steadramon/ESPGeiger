/*
  EGSmoothed.h - O(1) ring-buffer running average + EMA

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

// EGRingAvg<T,MaxN> (in-class buffer) and EGEma<T> (no buffer). Heap-free,
// safe on ISR-adjacent paths. Kept in sync between ESPGeiger firmware and
// ESPGeiger-Gadget; treat both copies as one file.

#pragma once

#include <Arduino.h>

template<typename T, uint16_t MaxN>
class EGRingAvg {
public:
  EGRingAvg() { reset(); _window = MaxN; }

  // Configure runtime window (1..MaxN). Resets running state.
  void begin(uint16_t window = MaxN) {
    if (window < 1)    window = 1;
    if (window > MaxN) window = MaxN;
    _window = window;
    reset();
  }

  void reset() {
    _value = (T)0;
    _pos   = 0;
    _count = 0;
    for (uint16_t i = 0; i < MaxN; i++) _buf[i] = (T)0;
  }

  void add(T value) {
    _value     -= _buf[_pos];
    _buf[_pos]  = value;
    _value     += value;
    if (++_pos >= _window) _pos = 0;
    if (_count < _window)  _count++;
  }

  // Mean of populated slots. 0 before the first add().
  T get() const {
    return _count ? (T)(_value / (T)_count) : (T)0;
  }

  // Running sum of populated slots, O(1).
  T sum() const { return _value; }

  // Most recently added value. Equivalent to at(count()-1) without the
  // wrap-aware index math.
  T last() const {
    if (_count == 0) return (T)0;
    uint16_t idx = (_pos == 0) ? (uint16_t)(_window - 1) : (uint16_t)(_pos - 1);
    return _buf[idx];
  }

  // Slot i in chronological order (0 = oldest, count-1 = newest). For
  // sparkline / chart draws without copying the buffer.
  T at(uint16_t i) const {
    if (!_count) return (T)0;
    if (i >= _count) i = _count - 1;
    uint16_t oldest = (_count < _window) ? 0 : _pos;
    uint16_t idx = oldest + i;
    if (idx >= _window) idx -= _window;
    return _buf[idx];
  }

  uint16_t window() const { return _window; }
  uint16_t count()  const { return _count; }
  bool     warm()   const { return _count >= _window; }

private:
  T        _buf[MaxN];
  T        _value;        // running sum
  uint16_t _window;
  uint16_t _pos;
  uint16_t _count;
};

template<typename T>
class EGEma {
public:
  EGEma() : _value((T)0), _alpha(0.0f) {}

  void begin(uint16_t factor) {
    if (factor < 1) factor = 1;
    _alpha = 1.0f / (float)factor;
    _value = (T)0;
  }

  void add(T value) {
    _value = (T)(value * _alpha + _value * (1.0f - _alpha));
  }

  T get() const { return _value; }

private:
  T     _value;
  float _alpha;
};
