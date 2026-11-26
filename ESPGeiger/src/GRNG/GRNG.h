/*
  GRNG.h - RNG wrapper header file

  Copyright (C) 2023 @steadramon

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
#ifndef GRNG_H
#define GRNG_H

#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"

class EGHttpServer;

class GRNG
{
  public:
    GRNG();
    void begin();
    // Called from loop
    static void mix(uint32_t bits);
    // Stirs hw random into the pool and reseeds random()
    static uint32_t stir();
    // for keys and passwords
    static void extract(uint8_t* out, size_t n);
    static uint32_t fast_uint32();
    // xorshift seeded from the pool, bulk only, not for keys
    static void extract_fast(uint8_t* out, size_t n);
    // no modulo bias
    static void pick(char* out, size_t n, PGM_P set, uint8_t set_len);
};

namespace GRNGRoutes {
  void registerRoutes(EGHttpServer& http);
}

#endif
