/*
  EGTinySerialPlatform.h - the only file that knows which chip this is.

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

// Everything platform specific lives here so the rest of the library reads as
// plain C++. Keep it that way: a new SoC should need edits in this file only.

#ifndef EG_TINYSERIAL_PLATFORM_H
#define EG_TINYSERIAL_PLATFORM_H

#include <Arduino.h>
#include <stdint.h>

namespace EGTinySerial {

// Cycle counter, not micros(). A bit at 115200 is 8.68 us, so micros() rounds
// away 11.5% of it; the cycle counter resolves 12.5 ns. It wraps every 53.7 s
// at 80 MHz, which is safe here because every comparison is an unsigned delta
// and nothing holds a tick across an interval that long: the poller flushes
// any part built frame within microseconds.
inline uint32_t IRAM_ATTR egtsTicks() { return ESP.getCycleCount(); }

inline uint32_t egtsCpuHz() { return (uint32_t)ESP.getCpuFreqMHz() * 1000000UL; }

// Fast pin read. The register and mask are cached at begin() so the handler
// does not pay a digitalRead() lookup per edge.
#define EGTS_PIN_READ(reg, mask) ((*(reg) & (mask)) != 0)

#if defined(ESP8266)

// Poller side only, so not IRAM. The handler needs no lock: it cannot be
// preempted by the code it shares state with.
typedef uint32_t egts_crit_t;
inline egts_crit_t egtsEnterCritical()             { return xt_rsil(15); }
inline void        egtsExitCritical(egts_crit_t s) { xt_wsr_ps(s); }

// Mask the pin's interrupt without going through detachInterrupt(), which is
// in flash and takes a lock. Clearing the type field is what the core's own
// detach does. This is the storm defence: declining a frame still pays full
// interrupt entry, and at a high enough edge rate that alone starves loop().
// Both IRAM: the sampling handler masks on entry and re-arms before it
// returns, and flash is unmapped while it is being written. A byte arriving
// during an OTA or a filesystem save would otherwise fault on the fetch.
inline void IRAM_ATTR egtsMutePin(uint8_t pin) {
  GPC(pin) &= ~(0xFu << GPCI);
}
inline void IRAM_ATTR egtsUnmutePin(uint8_t pin, uint8_t mode) {
  GPC(pin) = (GPC(pin) & ~(0xFu << GPCI)) | ((uint32_t)mode << GPCI);
}

inline bool egtsRxPinValid(int8_t pin) {
  // 16 has no interrupt, and 6..11 are the flash bus.
  return pin >= 0 && pin <= 15 && (pin < 6 || pin > 11);
}
inline bool egtsTxPinValid(int8_t pin) {
  return pin >= 0 && pin <= 16 && (pin < 6 || pin > 11);
}

inline void IRAM_ATTR egtsPinHigh(uint8_t pin) {
  if (pin < 16) GPOS = (1u << pin); else GP16O = 1;
}
inline void IRAM_ATTR egtsPinLow(uint8_t pin) {
  if (pin < 16) GPOC = (1u << pin); else GP16O = 0;
}

#elif defined(ESP32)

// Poller side only, and deliberately not IRAM: portENTER_CRITICAL needs a
// literal pool, and forcing it into IRAM gives the linker an l32r it cannot
// reach ("literal placed after use").
//
// The handler takes no lock. A GPIO interrupt is serviced on the core that
// registered it, begin() runs from the Arduino task on core 1, and so does
// every caller of pump(). Same core means the handler cannot be preempted by
// the code it shares state with, and masking interrupts here is enough.
// Opening a port from a task on the other core would break that.
extern portMUX_TYPE egtsMux;
typedef uint32_t egts_crit_t;
inline egts_crit_t egtsEnterCritical()      { portENTER_CRITICAL(&egtsMux); return 0; }
inline void        egtsExitCritical(egts_crit_t) { portEXIT_CRITICAL(&egtsMux); }

// Not implemented here. Starving the loop past a hardware watchdog is an
// ESP8266 failure mode: the ESP32 watchdog is task based and the admission
// budget alone keeps the handler's share bounded. Add a masking path only if
// a bench shows one is needed.
inline void IRAM_ATTR egtsMutePin(uint8_t)          {}
inline void egtsUnmutePin(uint8_t, uint8_t)         {}

inline bool egtsRxPinValid(int8_t pin) { return pin >= 0 && digitalPinIsValid(pin); }
inline bool egtsTxPinValid(int8_t pin) { return pin >= 0 && digitalPinCanOutput(pin); }

// digitalWrite(), not a register poke. The register names move between SoC
// variants and IDF releases, and the only caller is the traffic simulator on a
// chip with cycles to spare.
inline void IRAM_ATTR egtsPinHigh(uint8_t pin) { digitalWrite(pin, HIGH); }
inline void IRAM_ATTR egtsPinLow(uint8_t pin)  { digitalWrite(pin, LOW); }

#else
#error "EGTinySerial supports ESP8266 and ESP32"
#endif

} // namespace EGTinySerial

#endif
