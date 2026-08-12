/*
  EGTinySerial.h - RX-first software serial for ESP8266 and ESP32.

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

// 8N1 only, one port per pin, no heap.
//
// Not a Stream. Stream would make read/write/peek/flush virtual, the vtable
// would hold them all live, and --gc-sections could not then drop the
// transmit path from a receive-only build. That is worth hundreds of bytes of
// IRAM on an ESP8266. EGTinySerialStream.h has an adapter for anything that
// genuinely needs the interface.
//
// Bytes are decoded in the interrupt handler and pushed as bytes, not as edge
// timestamps decoded later. That is what keeps the buffer a fixed uint8_t
// array in .bss instead of a heap allocated array of 32-bit ticks.

#ifndef EG_TINYSERIAL_H
#define EG_TINYSERIAL_H

#include <stdint.h>
#include <stddef.h>

#include "EGTinySerialPlatform.h"
#include "EGTinySerialDecoder.h"
#include "EGSpscRing.h"

namespace EGTinySerial {

// Two handlers. Below the threshold one interrupt per transition, which is
// cheap and never blocks. At or above it the handler samples the whole frame
// inline, because per-edge interrupt service (5 to 6 us) stops fitting inside
// the bit period (8.68 us at 115200): measured on a GC10Next with WiFi busy,
// the edge handler accumulates framing errors and coalesced transitions at
// 115200, so it is not the right tool up there.
//
// Set this past every baud in use to force the edge path anyway, which is
// worth doing to compare the two on the same traffic.
#ifndef EG_TINYSERIAL_SYNC_BAUD
#define EG_TINYSERIAL_SYNC_BAUD 78432UL
#endif

// Share of the CPU the receive handler may take. Past it the pin is masked
// until the window rolls over, so a storm of edges cannot starve the loop. Per
// window and never cumulative, or it would latch off for good.
#ifndef EG_TINYSERIAL_BUDGET_WINDOW_US
#define EG_TINYSERIAL_BUDGET_WINDOW_US 10000UL
#endif
#ifndef EG_TINYSERIAL_BUDGET_US
#define EG_TINYSERIAL_BUDGET_US 5000UL
#endif

// Monotonic since begin(). Never cleared, so two readers cannot lose events
// between them and neither has to own the counter. Written by the handler and
// read by the poller: on a 32-bit target each field is a single load or store,
// so a reader sees an old value, never a torn one.
struct Stats {
  uint32_t bytes     = 0;   // delivered to the ring
  uint32_t dropped   = 0;   // ring full: the caller is not polling
  uint32_t framing   = 0;   // stop bit low, frame discarded
  uint32_t coalesced = 0;   // a transition the handler did not observe
  uint32_t breaks    = 0;   // line held low past a whole frame
  uint32_t muted     = 0;   // windows the pin interrupt was masked
#ifdef EG_TINYSERIAL_BENCH
  uint32_t isrCalls    = 0;
  uint32_t isrMaxCycles = 0;
#endif
};

class Core {
public:
  Core() = default;
  Core(const Core&)            = delete;
  Core& operator=(const Core&) = delete;
  ~Core() { end(); }

  // buf must outlive the port and cap must be a power of two. Returns false
  // and leaves the port closed on a bad pin, or on a baud this CPU clock
  // cannot resolve. Failing is deliberate: a port that opens at the wrong bit
  // period looks alive and delivers nothing but rubbish.
  bool begin(uint32_t baud, int8_t rxPin, int8_t txPin, uint8_t* buf, uint16_t cap);
  void end();

  // Gate inline at the caller so an idle port never fetches pump() or the
  // decoder's idle path out of flash. That is the whole of the common case:
  // the ring answers, or there is nothing to flush. idle() can only do work
  // while a frame is part built, and pump() is otherwise only needed to re-arm
  // the pin after a mute, so this test is exact rather than approximate.
  //
  // m_bitPos is written by the handler and read here without a lock. A stale
  // read costs one more poll before the last byte of a burst appears, which
  // the caller was going to make anyway.
  int available() {
    const uint32_t n = m_ring.available();
    if (n) return (int)n;
    if (!pumpNeeded()) return 0;
    pump();
    return (int)m_ring.available();
  }

  int read() {
    const int b = m_ring.pop();
    if (b >= 0) return b;
    if (!pumpNeeded()) return -1;
    pump();
    return m_ring.pop();
  }

  int peek() {
    const int b = m_ring.peek();
    if (b >= 0) return b;
    if (!pumpNeeded()) return -1;
    pump();
    return m_ring.peek();
  }

  size_t readInto(uint8_t* dst, size_t n);
  void   clear();

  // Re-derive the bit period if the CPU clock moved. ESP32 changes it at
  // runtime from a pref save, and this library times in CPU cycles rather
  // than wall clock, so a change silently corrupts every frame after it.
  // Call at a low rate, once a second is plenty: it reads the clock config,
  // which is far too expensive for the poll path.
  void tick();

  // Stop and restart the handler without losing the configuration. Meant for
  // a flash write: the handler is IRAM safe, but it has no business spending
  // cycles during an OTA, and anything it received mid update is stale.
  void pause();
  void resume();

  // Defined in EGTinySerialTx.cpp, which the linker leaves out entirely when
  // nothing calls it.
  size_t write(const uint8_t* src, size_t n);
  size_t write(uint8_t b) { return write(&b, 1); }

  const Stats& stats() const { return m_stats; }
  bool         rxActive() const { return m_rxValid; }
  bool         txActive() const { return m_txValid; }
  uint32_t     baud() const     { return m_baud; }

private:
  static void isrEdge(void* arg);
  static void isrSync(void* arg);

  void consume(const Emit& e);
  bool admit(uint32_t entry);        // false when over budget, and mutes
  void pump();                       // idle flush and re-arm, main thread only
  // True only when pump() has something to do. See the callers above.
  //
  // The low-level test is not redundant: a low run is consumed on the rising
  // edge that ends it, so the decoder is never mid frame while the line is
  // down, and break detection lives entirely on that branch of idle().
  bool pumpNeeded() const {
    return m_dec.midFrame() || !m_dec.lastLevel() || m_muted;
  }
  void derive(uint32_t cpb, uint32_t cpuHz);
  bool readPin() const { return EGTS_PIN_READ(m_rxReg, m_rxMask); }
  void writeByte(uint8_t b);

  Decoder  m_dec;
  SpscRing m_ring;
  Stats    m_stats;

  volatile uint32_t* m_rxReg  = nullptr;
  uint32_t           m_rxMask = 0;
  uint32_t           m_cpb    = 0;   // CPU cycles per bit
  uint32_t           m_baud   = 0;
  uint32_t           m_cpuHz  = 0;   // the clock m_cpb was derived from

  uint32_t m_windowCycles = 0;
  uint32_t m_budgetCycles = 0;
  uint32_t m_windowStart  = 0;
  uint32_t m_spent        = 0;

  int8_t  m_rxPin   = -1;
  int8_t  m_txPin   = -1;
  uint8_t m_isrMode = 0;
  bool    m_rxValid = false;
  bool    m_txValid = false;
  bool    m_muted   = false;
  bool    m_paused  = false;
};

// Owns the receive buffer. Nothing on a hot path is declared here: GCC does
// not honour IRAM_ATTR on template members, so a handler defined in a header
// would land in flash without a diagnostic and fault during a flash write.
template <uint16_t Cap>
class Port : public Core {
  static_assert(Cap >= 8 && Cap <= 4096,  "capacity must be 8..4096");
  static_assert((Cap & (Cap - 1)) == 0,   "capacity must be a power of two: the index wrap is a mask, and the ESP8266 has no divide instruction");
  uint8_t m_storage[Cap];
public:
  bool begin(uint32_t baud, int8_t rxPin, int8_t txPin = -1) {
    return Core::begin(baud, rxPin, txPin, m_storage, Cap);
  }
};

} // namespace EGTinySerial

#endif
