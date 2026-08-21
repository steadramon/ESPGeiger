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

// Stands in for the core's <Arduino.h> on the host. Keep it thin: if faking a
// dependency needs more code than the unit under test, extract a seam instead.
//
// Header-only. PlatformIO does not compile a non-suite directory under
// test_dir into each test binary, so a shim .cpp never links.
//
// Absent by design: String, Stream, Serial, WiFi, EEPROM. Test the char*
// paths; add a stub only when a unit worth testing needs it.

#ifndef EG_TEST_ARDUINO_H
#define EG_TEST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pgmspace.h"

// ---------------------------------------------------------------------------
// Fake clock
//
// Returns uint32_t, not the core's `unsigned long`: host `unsigned long` is
// 64-bit, so a seam holding time in one cannot wrap and a rollover test
// against it passes for the wrong reason.
//
// Time moves only when a test moves it. Held as uint64_t microseconds so a
// test can park below a millis() wrap and step across.
// ---------------------------------------------------------------------------
inline uint64_t& eg_clock_ref() { static uint64_t us = 0; return us; }

inline uint32_t millis() { return (uint32_t)(eg_clock_ref() / 1000ULL); }
inline uint32_t micros() { return (uint32_t)eg_clock_ref(); }

// Advance the fake clock. There is no real sleep.
inline void delay(uint32_t ms)             { eg_clock_ref() += (uint64_t)ms * 1000ULL; }
inline void delayMicroseconds(uint32_t us) { eg_clock_ref() += us; }
inline void yield()                        {}

// Test control. Call eg_clock_reset() from setUp() so cases cannot leak time
// into each other.
inline void     eg_clock_reset()                 { eg_clock_ref() = 0; }
inline void     eg_clock_set_ms(uint32_t ms)     { eg_clock_ref() = (uint64_t)ms * 1000ULL; }
inline void     eg_clock_advance_ms(uint32_t ms) { eg_clock_ref() += (uint64_t)ms * 1000ULL; }
inline void     eg_clock_set_us(uint64_t us)     { eg_clock_ref() = us; }
inline void     eg_clock_advance_us(uint64_t us) { eg_clock_ref() += us; }
inline uint64_t eg_clock_us()                    { return eg_clock_ref(); }

// ---------------------------------------------------------------------------
// Arduino odds and ends
// ---------------------------------------------------------------------------
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

// Placement attributes. No IRAM on a host, and the target rules they encode
// (ISR-safety, no flash access) are not observable here.
#define IRAM_ATTR
#define ICACHE_RAM_ATTR
#define ICACHE_FLASH_ATTR
#define DRAM_ATTR

// Interrupts are never delivered here; a unit under test drives its ISR by
// calling it. These exist so registration code links.
#define digitalPinToInterrupt(p) (p)
inline void attachInterrupt(uint8_t pin, void (*fn)(), int mode) {}
inline void detachInterrupt(uint8_t pin) {}
inline void interrupts() {}
inline void noInterrupts() {}

// ESP32 cross-task critical sections. Single-threaded here, so no-ops.
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

// Must NOT be macros: libstdc++ uses `min`/`max` as parameter names, so a
// macro breaks any header that reaches <algorithm>. Both ESP cores pull in
// std::min/std::max for C++ and keep only the underscored macros.
#include <algorithm>
using std::max;
using std::min;

#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))

// Same integer truncation as the core. A zero in_max-in_min span divides by
// zero here exactly as it faults on target; see the OLED graph regression.
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Hardware RNG stand-in. Entropy quality is a device property and untestable
// here; what is testable is that the mixing and extraction logic handles
// whatever words it is given. Default 0 is the adversarial case: a stuck
// source must not degenerate the pool. Install a hook for anything else.
typedef uint32_t (*eg_hw_word_fn)();
inline eg_hw_word_fn& eg_hw_word_hook() { static eg_hw_word_fn f = nullptr; return f; }
inline void eg_set_hw_word(eg_hw_word_fn f) { eg_hw_word_hook() = f; }

inline uint32_t esp_random() {
  eg_hw_word_fn f = eg_hw_word_hook();
  return f ? f() : 0u;
}

// Arduino's PRNG. GRNG::stir() seeds it; nothing under test reads it back.
inline void randomSeed(unsigned long seed) { srand((unsigned)seed); }
inline long random(long howbig) { return howbig > 0 ? (rand() % howbig) : 0; }
inline long random(long howsmall, long howbig) {
  return howbig > howsmall ? howsmall + random(howbig - howsmall) : howsmall;
}

// GPIO is a no-op. Nothing that only wiggles pins earns a host test; these
// exist so a unit with an incidental pinMode() still links.
inline void pinMode(uint8_t pin, uint8_t mode)     {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int  digitalRead(uint8_t pin)               { return LOW; }
inline int  analogRead(uint8_t pin)                { return 0; }
inline void analogWrite(uint8_t pin, int val)      {}
inline void tone(uint8_t pin, unsigned int freq, unsigned long dur = 0) {}
inline void noTone(uint8_t pin)                    {}

// getCycleCount derives from the fake clock at a nominal 80 MHz, so it is
// deterministic and advances only when a test advances time.
class EspClass {
public:
  uint32_t getCycleCount() const       { return (uint32_t)(eg_clock_ref() * 80ULL); }
  uint32_t getFreeHeap() const         { return _free_heap; }
  uint32_t getChipId() const           { return 0x00c0ffee; }
  uint32_t getMaxFreeBlockSize() const { return _max_block; }
  uint8_t  getHeapFragmentation() const { return 0; }
  uint32_t getCpuFreqMHz() const       { return 80; }
  uint32_t getFlashChipSize() const    { return 4u * 1024u * 1024u; }
  void     wdtFeed() const             {}
  void     wdtDisable() const          {}
  void     wdtEnable(uint32_t) const   {}

  // A unit that reboots mid-test is a bug in the unit, not something to
  // emulate. Counted so a test can assert it did NOT happen.
  void     restart()                   { _restarts++; }
  uint32_t eg_restarts() const         { return _restarts; }

  // Test control for the heap figures above.
  void eg_set_free_heap(uint32_t v)      { _free_heap = v; }
  void eg_set_max_free_block(uint32_t v) { _max_block = v; }

private:
  uint32_t _free_heap = 40000;
  uint32_t _max_block = 20000;
  uint32_t _restarts  = 0;
};

inline EspClass ESP;

#endif
