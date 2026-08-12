/*
  EGSpscRing.h - single-producer single-consumer byte ring.

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

// One producer (the RX interrupt) and one consumer (the polling loop). Any
// other pairing races: two producers can claim the same slot.
//
// std::atomic, not volatile. volatile orders volatile accesses against each
// other but leaves the plain store into m_buf[] free to sink past the index
// publish, which is the bug shape this exists to prevent. On ESP8266 the
// release store compiles to a plain store plus one MEMW, so the ordering is
// free; on ESP32 the same source emits the real barrier the second core needs.
//
// Indices are free-running uint32 counters masked on use, never wrapped
// manually. Their own wrap is harmless: h - t stays correct in modular
// arithmetic, and a full ring is h - t == cap with no slot sacrificed.

#ifndef EG_SPSC_RING_H
#define EG_SPSC_RING_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>

namespace EGTinySerial {

class SpscRing {
public:
  // cap must be a power of two so the index wrap is a mask. The ESP8266 has
  // no divide instruction, and this runs inside the interrupt handler.
  void init(uint8_t* buf, uint16_t cap) {
    m_buf  = buf;
    m_cap  = cap;
    m_mask = (uint32_t)cap - 1u;
    m_head.store(0, std::memory_order_relaxed);
    m_tail.store(0, std::memory_order_relaxed);
  }

  // Producer only. Drops the newest byte when full: for a line protocol,
  // losing the tail of a line beats overwriting the start of one the consumer
  // is part way through parsing.
  bool push(uint8_t b) {
    const uint32_t h = m_head.load(std::memory_order_relaxed);
    const uint32_t t = m_tail.load(std::memory_order_acquire);
    if ((uint32_t)(h - t) >= m_cap) return false;
    m_buf[h & m_mask] = b;
    m_head.store(h + 1u, std::memory_order_release);
    return true;
  }

  // Consumer only. -1 when empty.
  int pop() {
    const uint32_t t = m_tail.load(std::memory_order_relaxed);
    const uint32_t h = m_head.load(std::memory_order_acquire);
    if (h == t) return -1;
    const uint8_t b = m_buf[t & m_mask];
    m_tail.store(t + 1u, std::memory_order_release);
    return (int)b;
  }

  // Consumer only. -1 when empty.
  int peek() const {
    const uint32_t t = m_tail.load(std::memory_order_relaxed);
    const uint32_t h = m_head.load(std::memory_order_acquire);
    if (h == t) return -1;
    return (int)m_buf[t & m_mask];
  }

  uint32_t available() const {
    const uint32_t h = m_head.load(std::memory_order_acquire);
    const uint32_t t = m_tail.load(std::memory_order_relaxed);
    return (uint32_t)(h - t);
  }

  // Consumer only. A concurrent push is kept or dropped depending on where it
  // lands relative to the head read, never torn.
  void clear() {
    m_tail.store(m_head.load(std::memory_order_acquire), std::memory_order_release);
  }

  uint16_t capacity() const { return m_cap; }

private:
  uint8_t*              m_buf  = nullptr;
  uint32_t              m_mask = 0;
  uint16_t              m_cap  = 0;
  std::atomic<uint32_t> m_head{0};   // producer writes, consumer reads
  std::atomic<uint32_t> m_tail{0};   // consumer writes, producer reads
};

} // namespace EGTinySerial

#endif
