/*
  EGTinySerialDecoder.h - 8N1 line decoder.

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

// Pure: no Arduino, no pins, no interrupts, no atomics. Feed it transitions
// and it hands back bytes, so the whole of the bit-level logic is host
// testable and the platform file stays thin enough to read.
//
// Only transitions are recorded, so a frame whose trailing bits are high ends
// without an edge. Every ASCII byte has bit 7 clear, which makes that the last
// byte of every burst, including the newline terminating every line. idle()
// is the common path, not an escape hatch.
//
// 8N1 only. Parity, 5/6/7-bit words and two stop bits have no consumer here
// and each one widens the interrupt handler.

#ifndef EG_TINYSERIAL_DECODER_H
#define EG_TINYSERIAL_DECODER_H

#include <stdint.h>

// Whatever the interrupt handler reaches has to be in IRAM. Flash is
// unmapped while it is being written, so a byte arriving during a filesystem
// save or an OTA would fault on the instruction fetch. Nothing forces this at
// build time, so the placement has to be deliberate.
//
// On a host neither macro is defined and this stays out of the way, which is
// what keeps the decoder buildable without Arduino.h.
#if defined(ESP8266) || defined(ESP32)
#include <Arduino.h>
#define EGTS_ISR_ATTR IRAM_ATTR
#else
#define EGTS_ISR_ATTR
#endif

namespace EGTinySerial {

// Two bytes, so it comes back in a register. As five separate bools it was
// returned through a hidden pointer, and the stores that cost showed up in
// IRAM on every path that builds one.
struct Emit {
  enum : uint8_t {
    VALID     = 1,   // complete and framed, byte is good
    FRAMING   = 2,   // stop bit low, byte discarded
    COALESCED = 4,   // a transition the caller did not observe
    BREAK     = 8,   // line held low past a whole frame
  };
  uint8_t byte  = 0;
  uint8_t flags = 0;

  bool valid()     const { return (flags & VALID)     != 0; }
  bool framing()   const { return (flags & FRAMING)   != 0; }
  bool coalesced() const { return (flags & COALESCED) != 0; }
  bool brk()       const { return (flags & BREAK)     != 0; }
};

class Decoder {
public:
  // Start bit is not counted: 8 data plus 1 stop.
  static constexpr uint8_t PDU_BITS = 9;

  // Longest run worth measuring. Past this the line is idle whatever the
  // exact figure, so clamping here bounds the run loop at 10 iterations and
  // keeps a wrapped tick delta from spinning.
  static constexpr uint8_t MAX_RUN = PDU_BITS + 1;

  void configure(uint32_t cyclesPerBit);

  // Drop any part-built frame and adopt the line as it stands.
  // In IRAM: the sampling handler calls this when it abandons a frame on
  // its own deadline, and a flash call from an ISR faults during an OTA.
  EGTS_ISR_ATTR void reset(uint32_t tick, bool level);

  // A transition to `level` observed at `tick`. Reached from the handler.
  EGTS_ISR_ATTR Emit edge(uint32_t tick, bool level);

  // No transition for a while. `level` is the line as it stands now: low
  // means the line is being held down, so there is no stop bit to infer and
  // manufacturing one would invent a byte.
  Emit idle(uint32_t tick, bool level);

  bool     midFrame()    const { return m_bitPos >= 0; }
  uint32_t lastTick()    const { return m_lastTick; }
  bool     lastLevel()   const { return m_lastLevel; }
  uint32_t idleCycles()  const { return m_idleCycles; }
  uint32_t cyclesPerBit() const { return m_cyclesPerBit; }

private:
  // Bits of `level`, oldest first. At most one byte can complete per run: a
  // low run cannot reach a stop bit, and a high run that completes one leaves
  // the line idle, which cannot start another.
  EGTS_ISR_ATTR Emit runOfBits(uint8_t bits, bool level);

  // Rounded to nearest and clamped to MAX_RUN. Clamping first also keeps the
  // rounding addend from overflowing on a wrapped delta.
  EGTS_ISR_ATTR uint8_t bitsIn(uint32_t delta) const;

  uint32_t m_cyclesPerBit = 0;
  uint32_t m_halfBit      = 0;
  uint32_t m_idleCycles   = 0;
  uint32_t m_lastTick     = 0;
  bool     m_lastLevel    = true;   // idle high
  bool     m_resync       = false;  // discard the line until it returns high
  int8_t   m_bitPos       = -1;     // -1 idle, 0..8 data bits taken so far
  uint8_t  m_cur          = 0;
};

// Below this a bit is too short to carry the rounding tolerance the decoder
// needs. A port applies its own, stricter, platform-aware limit on top.
static constexpr uint32_t MIN_CYCLES_PER_BIT = 16;

// Round to nearest. Returns 0 when the clock cannot resolve the baud, which
// callers must treat as a refusal to open the port rather than a slow one.
uint32_t cyclesPerBit(uint32_t baud, uint32_t cpuHz);

} // namespace EGTinySerial

#endif
