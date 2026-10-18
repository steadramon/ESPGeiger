/*
  sout_fakes.h - stand-ins for what SerialOut.cpp reads besides its own state.

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

// include first

#ifndef TEST_SOUT_FAKES_H
#define TEST_SOUT_FAKES_H

#define SERIALOUT

#include <Arduino.h>
#include <string.h>

struct FakeSerial {
  char   out[160];
  size_t n;
  size_t write(const uint8_t* b, size_t len) {
    if (n + len >= sizeof(out)) len = sizeof(out) - 1 - n;
    memcpy(out + n, b, len);
    n += len;
    out[n] = 0;
    return len;
  }
  void print(const char* s) { write((const uint8_t*)s, strlen(s)); }
  void flush() {}
  void updateBaudRate(uint32_t) {}
};
extern FakeSerial Serial;

#define COUNTER_H
class Counter {
  public:
    int   cpm;
    float cps, usv;
    int   get_cpm() { return cpm; }
    float get_cps() { return cps; }
    float get_usv() { return usv; }
};
extern Counter gcounter;

#endif
