/*
  StatusWord.h - Named bits a module sets from the loop: on now, and on since boot.

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
#ifndef CORIUM_STATUSWORD_H
#define CORIUM_STATUSWORD_H

#include <Arduino.h>
#include <string.h>

namespace corium {

// 0 = all fine. Each bit has one writer, and the names live with whoever sets them
class StatusWord {
  public:
    constexpr StatusWord() {}

    void put(uint16_t mask, uint16_t bits) {
      _live = (uint16_t)((_live & ~mask) | bits);
      _ever |= bits;
    }
    void set(uint8_t bit, bool on) {
      uint16_t m = (uint16_t)(1u << bit);
      put(m, on ? m : 0);
    }
    uint16_t live() const { return _live; }
    uint16_t ever() const { return _ever; }

    // Gives "ok", else the names of the bits that are on, comma separated
    // lo = bits 0-7, hi = 8-15, PROGMEM "a\0b\0" with a last \0
    static __attribute__((noinline)) size_t text(char* buf, size_t cap, uint16_t bits,
                                                 const char* lo, const char* hi) {
      if (!cap) return 0;
      buf[0] = '\0';
      if (!bits) {
        if (cap < 3) return 0;
        memcpy_P(buf, PSTR("ok"), 3);
        return 2;
      }
      size_t len = 0;
      const char* names = lo;
      for (uint8_t i = 0; i < 16; i++) {
        if (i == 8) names = hi;
        size_t nl = strlen_P(names);
        if (!nl) continue;
        const char* name = names;
        names += nl + 1;
        if (!(bits & (1u << i))) continue;
        size_t sep = len ? 1 : 0;
        if (len + sep + nl + 1 > cap) break;
        if (sep) buf[len++] = ',';
        memcpy_P(buf + len, name, nl);
        len += nl;
        buf[len] = '\0';
      }
      return len;
    }

  private:
    uint16_t _live = 0;
    uint16_t _ever = 0;
};

// The device's word. The app's bits are 0-7, these are Corium's, only ever added to
enum : uint8_t { DEV_CRASHED = 8, DEV_NOCLOCK, DEV_SAVEFAIL, DEV_LOWHEAP };
#define CORIUM_DEV_NAMES "crashed\0noclock\0savefail\0lowheap\0"

static const uint16_t DEV_TIME = (1u << DEV_NOCLOCK) | (1u << DEV_LOWHEAP);   // the two the Reactor sets

static const uint32_t CLOCK_WAIT_S = 600;   // a normal boot syncs well before this
static const uint32_t HEAP_MIN_8266 = 4096;
static const uint32_t HEAP_MIN_32   = 16384;
#ifdef ESP8266
static const uint32_t HEAP_MIN = HEAP_MIN_8266;
#else
static const uint32_t HEAP_MIN = HEAP_MIN_32;
#endif

// A function, since the esp32 core is c++11
inline StatusWord& healthWord() {
  static StatusWord w;
  return w;
}

static const uint8_t HEAP_LOW_S = 2;   // seconds in a row, a single dip isn't low

// Each second. online = wifi is meant to be up
inline void statusTick(uint32_t up, bool synced, bool online, uint32_t heap) {
  static uint8_t low_s = 0;
  low_s = heap < HEAP_MIN ? (low_s < HEAP_LOW_S ? low_s + 1 : HEAP_LOW_S) : 0;
  healthWord().put(DEV_TIME, (uint16_t)((!synced && online && up > CLOCK_WAIT_S) << DEV_NOCLOCK |
                                        (low_s >= HEAP_LOW_S) << DEV_LOWHEAP));
}

}  // namespace corium

#endif
