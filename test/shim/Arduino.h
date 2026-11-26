/*
  Arduino.h - host shim for native unit tests.

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

// header only, a .cpp here never links

#ifndef EG_TEST_ARDUINO_H
#define EG_TEST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "pgmspace.h"

// us, millis() and micros() cut it to 32 bits so they wrap
inline uint64_t& eg_clock_ref() { static uint64_t us = 0; return us; }

inline uint32_t millis() { return (uint32_t)(eg_clock_ref() / 1000ULL); }
inline uint32_t micros() { return (uint32_t)eg_clock_ref(); }

inline void delay(uint32_t ms)             { eg_clock_ref() += (uint64_t)ms * 1000ULL; }
inline void delayMicroseconds(uint32_t us) { eg_clock_ref() += us; }
// Counted, no yield in network callbacks
inline uint32_t& eg_yield_count()          { static uint32_t n = 0; return n; }
// the 8266 runs lwIP in yield, a test can too
typedef void (*eg_yield_fn)();
inline eg_yield_fn& eg_yield_hook()        { static eg_yield_fn f = nullptr; return f; }
inline void yield() {
  eg_yield_count()++;
  if (eg_yield_hook()) eg_yield_hook()();
}

// eg_clock_reset() from setUp()
inline void     eg_clock_reset()                 { eg_clock_ref() = 0; }
inline void     eg_clock_set_ms(uint32_t ms)     { eg_clock_ref() = (uint64_t)ms * 1000ULL; }
inline void     eg_clock_advance_ms(uint32_t ms) { eg_clock_ref() += (uint64_t)ms * 1000ULL; }
inline void     eg_clock_set_us(uint64_t us)     { eg_clock_ref() = us; }
inline void     eg_clock_advance_us(uint64_t us) { eg_clock_ref() += us; }
inline uint64_t eg_clock_us()                    { return eg_clock_ref(); }

#define HIGH 0x1
#define LOW  0x0

#define INPUT        0x00
#define OUTPUT       0x01
#define INPUT_PULLUP 0x02

#define LSBFIRST 0
#define MSBFIRST 1

#define RISING  0x01
#define FALLING 0x02
#define CHANGE  0x03

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

typedef uint8_t byte;
typedef bool    boolean;

#define IRAM_ATTR
#define ICACHE_RAM_ATTR
#define ICACHE_FLASH_ATTR
#define DRAM_ATTR

#define digitalPinToInterrupt(p) (p)
inline void attachInterrupt(uint8_t pin, void (*fn)(), int mode) {}
inline void detachInterrupt(uint8_t pin) {}
inline void interrupts() {}
inline void noInterrupts() {}

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)         do { (void)(m); } while (0)
#define portEXIT_CRITICAL(m)          do { (void)(m); } while (0)
#define portENTER_CRITICAL_ISR(m)     do { (void)(m); } while (0)
#define portEXIT_CRITICAL_ISR(m)      do { (void)(m); } while (0)

#ifndef PI
#define PI         3.1415926535897932384626433832795
#define HALF_PI    1.5707963267948966192313216916398
#define TWO_PI     6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#ifndef sq
#define sq(x) ((x) * (x))
#endif

#define bitRead(value, bit)     (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)      ((value) |= (1UL << (bit)))
#define bitClear(value, bit)    ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, b) ((b) ? bitSet(value, bit) : bitClear(value, bit))
#define lowByte(w)              ((uint8_t)((w) & 0xff))
#define highByte(w)             ((uint8_t)((w) >> 8))

// not macros, libstdc++ clash
#include <algorithm>
using std::max;
using std::min;

#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// 0 unless hooked
typedef uint32_t (*eg_hw_word_fn)();
inline eg_hw_word_fn& eg_hw_word_hook() { static eg_hw_word_fn f = nullptr; return f; }
inline void eg_set_hw_word(eg_hw_word_fn f) { eg_hw_word_hook() = f; }

inline uint32_t esp_random() {
  eg_hw_word_fn f = eg_hw_word_hook();
  return f ? f() : 0u;
}

inline void randomSeed(unsigned long seed) { srand((unsigned)seed); }
inline long random(long howbig) { return howbig > 0 ? (rand() % howbig) : 0; }
inline long random(long howsmall, long howbig) {
  return howbig > howsmall ? howsmall + random(howbig - howsmall) : howsmall;
}

inline void pinMode(uint8_t pin, uint8_t mode)     {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int  digitalRead(uint8_t pin)               { return LOW; }
inline int  analogRead(uint8_t pin)                { return 0; }
inline void analogWrite(uint8_t pin, int val)      {}
inline void tone(uint8_t pin, unsigned int freq, unsigned long dur = 0) {}
inline void noTone(uint8_t pin)                    {}

enum WDTO_t { WDTO_8S = 8000 };   // esp8266 Esp.h

// 80 MHz
class EspClass {
public:
  uint32_t getCycleCount() const       { return (uint32_t)(eg_clock_ref() * 80ULL); }
  uint32_t getFreeHeap() const         { return _free_heap; }
  uint32_t getChipId() const           { return 0x00c0ffee; }
  uint32_t getMaxFreeBlockSize() const { return _max_block; }
  uint8_t  getHeapFragmentation() const { return 0; }
  uint32_t getFreeContStack() const    { return 0; }
  void     getHeapStats(uint32_t* f, uint16_t* m, uint8_t* fr) const {
    *f = _free_heap; *m = (uint16_t)_max_block; *fr = 7;
  }
  uint32_t getMinFreeHeap() const      { return 18000; }   // esp32
  uint32_t getMaxAllocHeap() const     { return _max_block; }   // esp32
  uint32_t getCpuFreqMHz() const       { return 80; }
  uint32_t getFlashChipSize() const    { return 4u * 1024u * 1024u; }
  uint32_t getFlashChipRealSize() const { return 8u * 1024u * 1024u; }
  uint8_t  getBootVersion() const      { return 31; }
  uint32_t getSketchSize() const       { return 500000; }
  uint32_t getFreeSketchSpace() const  { return 1000000; }
  void     wdtFeed() const             {}
  void     wdtDisable() const          {}
  void     wdtEnable(uint32_t) const   {}

  void     restart()                   { _restarts++; }
  uint32_t eg_restarts() const         { return _restarts; }

  void eg_set_free_heap(uint32_t v)      { _free_heap = v; }
  void eg_set_max_free_block(uint32_t v) { _max_block = v; }

  // one nor sector at any address: erase = 0xFF, a write only clears bits
  bool flashEraseSector(uint32_t sector) {
    if (eg_flash_fail) return false;
    memset(eg_flash, 0xFF, sizeof(eg_flash));
    eg_flash_erases++;
    return true;
  }
  bool flashWrite(uint32_t addr, const uint32_t* data, size_t size) {
    if (eg_flash_fail || addr % 4 || size % 4 || addr % 4096 + size > sizeof(eg_flash)) return false;
    const uint8_t* s = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) eg_flash[addr % 4096 + i] &= s[i];
    return true;
  }
  bool flashRead(uint32_t addr, uint32_t* data, size_t size) {
    if (addr % 4 || size % 4 || addr % 4096 + size > sizeof(eg_flash)) return false;
    memcpy(data, eg_flash + addr % 4096, size);
    return true;
  }
  uint8_t eg_flash[4096];
  uint32_t eg_flash_erases = 0;
  bool eg_flash_fail = false;

  // rtc user memory, 128 words
  bool rtcUserMemoryRead(uint32_t offset, uint32_t* data, size_t size) {
    if (size == 0 || offset * 4 + size > sizeof(eg_rtc)) return false;
    memcpy(data, (uint8_t*)eg_rtc + offset * 4, size);
    return true;
  }
  bool rtcUserMemoryWrite(uint32_t offset, const uint32_t* data, size_t size) {
    if (size == 0 || offset * 4 + size > sizeof(eg_rtc)) return false;
    memcpy((uint8_t*)eg_rtc + offset * 4, data, size);
    return true;
  }
  uint32_t eg_rtc[128];

private:
  uint32_t _free_heap = 40000;
  uint32_t _max_block = 20000;
  uint32_t _restarts  = 0;
};

#ifndef SPI_FLASH_SEC_SIZE
#define SPI_FLASH_SEC_SIZE 4096
#endif

inline EspClass ESP;

#endif
