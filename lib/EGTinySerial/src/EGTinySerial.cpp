/*
  EGTinySerial.cpp - pins, interrupt handlers, admission budget.

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

#include "EGTinySerial.h"

namespace EGTinySerial {

#if defined(ESP32)
portMUX_TYPE egtsMux = portMUX_INITIALIZER_UNLOCKED;
#endif

bool Core::begin(uint32_t baud, int8_t rxPin, int8_t txPin, uint8_t* buf, uint16_t cap) {
  end();

  const uint32_t cpb = cyclesPerBit(baud, egtsCpuHz());
  if (cpb == 0) return false;
  if (cap < 8 || (cap & (cap - 1)) != 0) return false;

  m_baud = baud;
  derive(cpb, egtsCpuHz());

  if (rxPin >= 0 && egtsRxPinValid(rxPin) && buf != nullptr) {
    m_ring.init(buf, cap);
    pinMode(rxPin, INPUT_PULLUP);
    m_rxReg  = portInputRegister(digitalPinToPort(rxPin));
    m_rxMask = digitalPinToBitMask(rxPin);
    m_rxPin  = rxPin;

    const uint32_t now = egtsTicks();
    m_dec.reset(now, EGTS_PIN_READ(m_rxReg, m_rxMask));
    m_windowStart = now;
    m_spent       = 0;
    m_muted       = false;

    m_isrMode = (baud >= EG_TINYSERIAL_SYNC_BAUD) ? FALLING : CHANGE;
    m_rxValid = true;
    attachInterruptArg(digitalPinToInterrupt(rxPin),
                       (m_isrMode == FALLING) ? isrSync : isrEdge,
                       this, m_isrMode);
  }

  if (txPin >= 0 && egtsTxPinValid(txPin)) {
    m_txPin = txPin;
    digitalWrite(txPin, HIGH);          // idle before enabling the driver
    pinMode(txPin, OUTPUT);
    digitalWrite(txPin, HIGH);
    m_txValid = true;
  }

  return m_rxValid || m_txValid;
}

// Budget in cycles so the handler never touches micros(), which on the
// ESP8266 is a timer read plus arithmetic.
void Core::derive(uint32_t cpb, uint32_t cpuHz) {
  m_cpb   = cpb;
  m_cpuHz = cpuHz;
  m_dec.configure(cpb);
  const uint32_t perUs = cpuHz / 1000000UL;
  m_windowCycles = EG_TINYSERIAL_BUDGET_WINDOW_US * perUs;
  m_budgetCycles = EG_TINYSERIAL_BUDGET_US * perUs;
}

void Core::tick() {
  if (!m_rxValid && !m_txValid) return;
  const uint32_t hz = egtsCpuHz();
  if (hz == 0 || hz == m_cpuHz) return;

  const uint32_t cpb = cyclesPerBit(m_baud, hz);
  if (cpb == 0) return;                 // keep the old rate rather than a broken one

  // Whatever was part way through was timed against the old clock.
  const egts_crit_t s = egtsEnterCritical();
  derive(cpb, hz);
  m_windowStart = egtsTicks();
  m_spent       = 0;
  if (m_rxValid) m_dec.reset(egtsTicks(), readPin());
  egtsExitCritical(s);
}

void Core::end() {
  if (m_rxValid) {
    detachInterrupt(digitalPinToInterrupt(m_rxPin));
    m_rxValid = false;
  }
  m_txValid = false;
  m_rxPin   = -1;
  m_txPin   = -1;
  m_muted   = false;
}

void IRAM_ATTR Core::consume(const Emit& e) {
  if (e.flags == 0) return;                 // the overwhelmingly common case
  if (e.valid()) {
    if (m_ring.push(e.byte)) m_stats.bytes++;
    else                     m_stats.dropped++;
  }
  if (e.framing())   m_stats.framing++;
  if (e.coalesced()) m_stats.coalesced++;
  if (e.brk())       m_stats.breaks++;
}

bool IRAM_ATTR Core::admit(uint32_t entry) {
  if ((uint32_t)(entry - m_windowStart) >= m_windowCycles) {
    m_windowStart = entry;
    m_spent       = 0;
    return true;
  }
  if (m_spent > m_budgetCycles) {
    // Declining the frame would still pay full interrupt entry, which at a
    // high enough edge rate is the whole CPU on its own. Mask the pin instead
    // and let the poller re-arm it. A storm is not carrying data anyway.
    if (!m_muted) {
      m_muted = true;
      egtsMutePin((uint8_t)m_rxPin);
      m_stats.muted++;
    }
    return false;
  }
  return true;
}

void IRAM_ATTR Core::isrEdge(void* arg) {
  Core* self = static_cast<Core*>(arg);
  const uint32_t entry = egtsTicks();
  if (!self->admit(entry)) return;

  const bool level = EGTS_PIN_READ(self->m_rxReg, self->m_rxMask);
  self->consume(self->m_dec.edge(entry, level));

#ifdef EG_TINYSERIAL_BENCH
  self->m_stats.isrCalls++;
#endif
  const uint32_t spent = egtsTicks() - entry;
  self->m_spent += spent;
#ifdef EG_TINYSERIAL_BENCH
  if (spent > self->m_stats.isrMaxCycles) self->m_stats.isrMaxCycles = spent;
#endif
}

void IRAM_ATTR Core::isrSync(void* arg) {
  Core* self = static_cast<Core*>(arg);
  const uint32_t entry = egtsTicks();
  if (!self->admit(entry)) return;

  // Mask the pin for the duration. This handler is attached to FALLING and
  // stays inside the frame, and data bits fall too: 0x30 has an edge at bit 7.
  // Left armed, that edge sets the pin's interrupt status while we are still
  // sampling, the core dispatcher re-fires us the moment we return, and a mid
  // frame edge is taken as a fresh start bit. Lines then never assemble, and
  // each frame costs several times the CPU it should.
  egtsMutePin((uint8_t)self->m_rxPin);

  const uint32_t cpb = self->m_cpb;
  bool level = false;                       // the start edge that woke us
  self->consume(self->m_dec.edge(entry, false));

  // No half bit offset. `entry` is read inside the handler, so it already
  // trails the real edge by the dispatch latency, around 0.35 of a bit at
  // 115200, and that is what centres the sample. Adding another half pushes it
  // against the far edge of the bit, where the stop bit is the first to fall
  // out. This is the offset upstream uses and it is the one that works.
  for (uint32_t i = 1; i <= Decoder::PDU_BITS; i++) {
    const uint32_t at = entry + i * cpb;
    while ((int32_t)(egtsTicks() - at) < 0) { }
    const bool now = EGTS_PIN_READ(self->m_rxReg, self->m_rxMask);
    // Re-arm as soon as the last data bit has been read, and before the work
    // below, which can run a few microseconds. A masked edge is gone rather
    // than late, and the next start bit is only one bit away. In a valid frame
    // the stop bit is high, so no falling edge remains that could re-trigger
    // this handler mid frame.
    if (i == Decoder::PDU_BITS) egtsUnmutePin((uint8_t)self->m_rxPin, self->m_isrMode);
    if (now != level) {
      self->consume(self->m_dec.edge(at, now));
      level = now;
    }
  }

#ifdef EG_TINYSERIAL_BENCH
  self->m_stats.isrCalls++;
#endif
  const uint32_t spent = egtsTicks() - entry;
  self->m_spent += spent;
#ifdef EG_TINYSERIAL_BENCH
  if (spent > self->m_stats.isrMaxCycles) self->m_stats.isrMaxCycles = spent;
#endif
}

void Core::pause() {
  if (!m_rxValid || m_paused) return;
  m_paused = true;
  egtsMutePin((uint8_t)m_rxPin);
}

void Core::resume() {
  if (!m_rxValid || !m_paused) return;
  m_paused = false;
  const egts_crit_t s = egtsEnterCritical();
  m_dec.reset(egtsTicks(), readPin());
  m_windowStart = egtsTicks();
  m_spent       = 0;
  m_muted       = false;
  egtsExitCritical(s);
  m_ring.clear();                     // whatever arrived mid update is stale
  egtsUnmutePin((uint8_t)m_rxPin, m_isrMode);
}

void Core::pump() {
  if (!m_rxValid || m_paused) return;

  if (m_muted && (uint32_t)(egtsTicks() - m_windowStart) >= m_windowCycles) {
    m_muted       = false;
    m_windowStart = egtsTicks();
    m_spent       = 0;
    egtsUnmutePin((uint8_t)m_rxPin, m_isrMode);
  }

  const bool level = readPin();
  // The tick has to be read under the lock. Sampled outside it, a handler
  // running in between would leave m_lastTick ahead of it, and the unsigned
  // delta would look like a whole wrap of elapsed time.
  const egts_crit_t s = egtsEnterCritical();
  const Emit e = m_dec.idle(egtsTicks(), level);
  consume(e);
  egtsExitCritical(s);
}

size_t Core::readInto(uint8_t* dst, size_t n) {
  pump();
  size_t got = 0;
  while (got < n) {
    const int b = m_ring.pop();
    if (b < 0) break;
    dst[got++] = (uint8_t)b;
  }
  return got;
}

void Core::clear() {
  m_ring.clear();
  const egts_crit_t s = egtsEnterCritical();
  m_dec.reset(egtsTicks(), readPin());
  egtsExitCritical(s);
}

} // namespace EGTinySerial
