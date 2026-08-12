/*
  EGTinySerialTx.cpp - bit banged transmit.

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

// Its own translation unit so a receive-only build never links it. Nothing
// else in the library refers to write(), and Core has no virtuals, so the
// linker drops this whole file rather than carrying it in a vtable.

#include "EGTinySerial.h"

namespace EGTinySerial {

// IRAM: a flash fetch between two bits stretches one of them past the bit
// period, and there is no way to recover a frame once that happens.
void IRAM_ATTR Core::writeByte(uint8_t b) {
  const uint32_t cpb = m_cpb;
  const uint8_t  pin = (uint8_t)m_txPin;
  uint32_t at = egtsTicks();

  // Absolute deadlines from one start point, so a late bit does not push the
  // rest of the frame along with it.
  egtsPinLow(pin);                              // start
  for (uint8_t i = 0; i < 8; i++) {
    at += cpb;
    while ((int32_t)(egtsTicks() - at) < 0) { }
    if (b & 1) egtsPinHigh(pin); else egtsPinLow(pin);
    b >>= 1;
  }
  at += cpb;
  while ((int32_t)(egtsTicks() - at) < 0) { }
  egtsPinHigh(pin);                             // stop
  at += cpb;
  while ((int32_t)(egtsTicks() - at) < 0) { }
}

size_t Core::write(const uint8_t* src, size_t n) {
  if (!m_txValid || src == nullptr) return 0;
  for (size_t i = 0; i < n; i++) {
    writeByte(src[i]);
    // Interrupts stay enabled throughout: one landing mid bit corrupts that
    // bit, which for a traffic simulator beats blocking the whole frame. The
    // gap between bytes is idle line, so yielding in it is free.
    if ((i & 0x0F) == 0x0F) yield();
  }
  return n;
}

} // namespace EGTinySerial
