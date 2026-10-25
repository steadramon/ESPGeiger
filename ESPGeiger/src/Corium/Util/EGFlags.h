/*
  EGFlags.h - Named bits in one unsigned word, masks in, no heap.

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
#ifndef EGFLAGS_H
#define EGFLAGS_H

#include <stdint.h>
#include <type_traits>

// one task only, no lock
template <typename T>
class EGFlags {
  static_assert(std::is_unsigned<T>::value, "unsigned word");

public:
  constexpr EGFlags() : _v(0) {}

  void set(T m)          { _v = (T)(_v | m); }
  void clear(T m)        { _v = (T)(_v & (T)~m); }
  void put(T m, bool on) { if (on) set(m); else clear(m); }

  bool has(T m) const    { return (_v & m) != 0; }   // any of m
  bool all(T m) const    { return (T)(_v & m) == m; }
  bool any() const       { return _v != 0; }

  // the bits of m that were set, now clear
  T take(T m) {
    T got = (T)(_v & m);
    clear(m);
    return got;
  }
  T take_all() {
    T got = _v;
    _v = 0;
    return got;
  }

  T raw() const          { return _v; }

private:
  T _v;
};

#endif
