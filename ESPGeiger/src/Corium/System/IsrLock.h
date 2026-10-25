/*
  IsrLock.h - Short lock against an interrupt, one shape for both chips.

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
#ifndef ISRLOCK_H
#define ISRLOCK_H

#include <Arduino.h>
#include <atomic>
#ifdef NATIVE_TEST
#include <mutex>
#endif

// isr code kept in ram
#define EG_ISR_ATTR IRAM_ATTR

namespace IsrLock {

  static inline __attribute__((always_inline)) void pin_on_fall(int pin, void (*fn)()) {
    attachInterrupt(digitalPinToInterrupt(pin), fn, FALLING);
  }

  static inline __attribute__((always_inline)) void pin_off(int pin) {
    detachInterrupt(digitalPinToInterrupt(pin));
  }

  // esp32 isr may be on the other core, 8266 just masks, host a real lock for tsan
  struct Mux {
#if defined(ESP32)
    portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
#elif defined(NATIVE_TEST)
    std::mutex m;
#endif
  };

  // from the loop
  static inline __attribute__((always_inline)) void enter(Mux& x) {
#if defined(ESP32)
    portENTER_CRITICAL(&x.m);
#elif defined(NATIVE_TEST)
    x.m.lock();
#else
    (void)x;
    noInterrupts();
#endif
  }

  static inline __attribute__((always_inline)) void leave(Mux& x) {
#if defined(ESP32)
    portEXIT_CRITICAL(&x.m);
#elif defined(NATIVE_TEST)
    x.m.unlock();
#else
    (void)x;
    interrupts();
#endif
  }

  // in the isr, 8266 isrs don't nest
  static inline __attribute__((always_inline)) void enter_isr(Mux& x) {
#if defined(ESP32)
    portENTER_CRITICAL_ISR(&x.m);
#elif defined(NATIVE_TEST)
    x.m.lock();
#else
    (void)x;
#endif
  }

  static inline __attribute__((always_inline)) void leave_isr(Mux& x) {
#if defined(ESP32)
    portEXIT_CRITICAL_ISR(&x.m);
#elif defined(NATIVE_TEST)
    x.m.unlock();
#else
    (void)x;
#endif
  }

  // bits set from another task, read lock-free by the loop, taken under the lock
  // load + store, 8266 has no atomic or
  struct Bits {
    Mux mux;
    std::atomic<uint32_t> w{0};

    void set(uint32_t b) {
      enter(mux);
      w.store(w.load(std::memory_order_relaxed) | b, std::memory_order_relaxed);
      leave(mux);
    }
    bool any() const { return w.load(std::memory_order_relaxed) != 0; }
    uint32_t take() {
      enter(mux);
      uint32_t v = w.load(std::memory_order_relaxed);
      w.store(0, std::memory_order_relaxed);
      leave(mux);
      return v;
    }
  };

}

#endif
