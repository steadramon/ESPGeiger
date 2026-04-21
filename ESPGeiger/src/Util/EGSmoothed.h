/*
  EGSmoothed.h - Lightweight smoothing classes for ESPGeiger.

  In-house replacement for <Smoothed.h> with O(1) running-sum math.
  Matches Smoothed's API (Smoothed<T>, SMOOTHED_AVERAGE,
  SMOOTHED_EXPONENTIAL) so call sites read the same. On ESP8266 this
  drops a 60-item average get() from ~280us to ~5us.

  Usage:
    #include "../Util/EGSmoothed.h"
    Smoothed<float> avg;
    avg.begin(SMOOTHED_AVERAGE, 60);
    avg.add(x);
    float v = avg.get();

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
#ifndef EGSMOOTHED_H
#define EGSMOOTHED_H

#include <Arduino.h>

#define SMOOTHED_AVERAGE      1
#define SMOOTHED_EXPONENTIAL  2

template<typename T>
class EGSmoothed {
  public:
    EGSmoothed()
      : _mode(0), _factor(0), _value(0), _buf(nullptr),
        _pos(0), _count(0), _alpha(0.0f) {}

    ~EGSmoothed() { delete[] _buf; }

    bool begin(uint8_t mode, uint16_t factor = 10) {
      _mode = mode;
      _factor = factor;
      _value = 0;
      _pos = 0;
      _count = 0;
      if (mode == SMOOTHED_AVERAGE) {
        delete[] _buf;
        _buf = new T[factor]();  // value-initialised to 0
        return _buf != nullptr;
      }
      // EXPONENTIAL: precompute alpha = 1/factor for the EMA update.
      _alpha = 1.0f / factor;
      return true;
    }

    // O(1) running-sum update - replaces the library's naive push.
    void add(T value) {
      if (_mode == SMOOTHED_AVERAGE) {
        if (!_buf) return;
        _value -= _buf[_pos];   // drop evicted element from sum
        _buf[_pos] = value;
        _value += value;
        if (++_pos >= _factor) _pos = 0;
        if (_count < _factor) _count++;
      } else {
        // EMA: new = alpha*input + (1-alpha)*prev
        _value = (T)(value * _alpha + _value * (1.0f - _alpha));
      }
    }

    // O(1) read for both modes. Average = running_sum / count.
    T get() const {
      if (_mode == SMOOTHED_AVERAGE) {
        return _count ? (_value / _count) : (T)0;
      }
      return _value;
    }

  private:
    uint8_t  _mode;
    uint16_t _factor;
    T        _value;   // running sum (AVG) or current smoothed value (EMA)
    T*       _buf;     // circular buffer - only used for AVG mode
    uint16_t _pos;
    uint16_t _count;
    float    _alpha;   // precomputed 1/factor for EMA
};

// Alias so existing callers using Smoothed<T> transparently get ours.
template<typename T> using Smoothed = EGSmoothed<T>;

#endif  // EGSMOOTHED_H
