/*
  Tm1637Driver.cpp - Minimal bit-banged TM1637 driver (4-6 digit 7-seg modules).

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
#include "Tm1637Driver.h"

enum : uint8_t {
  CMD_WRITE = 0x40,   // auto increment
  CMD_ADDR  = 0xC0,   // | address
  CMD_ON    = 0x88,   // | level 0-7
  CMD_OFF   = 0x80,
};

void Tm1637Driver::begin(int8_t clk, int8_t dio) {
  _clk = clk;
  _dio = dio;
  _ready = clk >= 0 && dio >= 0;
  if (!_ready) return;
  drive(_clk, true);
  drive(_dio, true);
  // fastest that acks and leaves DIO high
  static const uint8_t TRY_US[] = { 5, 10, 25, 50, SLOWEST_US };
  for (uint8_t us : TRY_US) {
    _us = us;
    if (command(CMD_WRITE) && digitalRead(_dio) == HIGH) return;
    delayMicroseconds(SLOWEST_US);   // chip sees a stop
  }
}

// driven both ways, pull-ups and module caps make a let-go edge slow
void Tm1637Driver::drive(int8_t pin, bool high) {
  digitalWrite(pin, high ? HIGH : LOW);
  pinMode(pin, OUTPUT);
  delayMicroseconds(_us);
}

// only for the ack, the chip pulls DIO low
void Tm1637Driver::release(int8_t pin) {
#ifdef ESP8266
  // GPIO16 has no pull-up, INPUT_PULLUP leaves it driving
  pinMode(pin, pin == 16 ? INPUT : INPUT_PULLUP);
#else
  pinMode(pin, INPUT_PULLUP);
#endif
  delayMicroseconds(_us);
}

// DIO falls while CLK is high
void Tm1637Driver::start() {
  drive(_dio, false);
}

// DIO rises while CLK is high
void Tm1637Driver::stop() {
  drive(_clk, false);
  drive(_dio, false);
  drive(_clk, true);
  drive(_dio, true);
}

// 9th clock = ack
bool Tm1637Driver::put(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    drive(_clk, false);
    drive(_dio, b & 1);
    b >>= 1;
    drive(_clk, true);
  }
  drive(_clk, false);
  release(_dio);
  drive(_clk, true);
  bool ack = digitalRead(_dio) == LOW;
  drive(_clk, false);
  return ack;
}

bool Tm1637Driver::command(uint8_t c) {
  start();
  bool ok = put(c);
  stop();
  return ok;
}

// write mode, first changed address + the changed run, control
bool Tm1637Driver::show(const uint8_t* segs, uint8_t n) {
  if (!_ready) return false;
  if (n > MAX_DIGITS) n = MAX_DIGITS;
  uint8_t from = 0, to = n;
  if (n == _lastN) {
    while (from < n && segs[from] == _last[from]) from++;
    if (from == n) return true;
    while (segs[to - 1] == _last[to - 1]) to--;
  }
  bool ok = command(CMD_WRITE);
  start();
  ok = put((uint8_t)(CMD_ADDR | from)) && ok;
  for (uint8_t i = from; i < to; i++) ok = put(segs[i]) && ok;
  stop();
  ok = command(_ctrl) && ok;
  _lastN = ok ? n : 0;
  if (ok) memcpy(_last, segs, n);
  return ok;
}

bool Tm1637Driver::light(uint8_t level, bool on) {
  _ctrl = on ? (uint8_t)(CMD_ON | (level > 7 ? 7 : level)) : CMD_OFF;
  if (!_ready) return false;
  return command(_ctrl);
}
