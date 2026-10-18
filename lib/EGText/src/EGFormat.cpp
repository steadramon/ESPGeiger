/*
  EGFormat.cpp - Numbers to text without the soft-float printf.

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
#include "EGFormat.h"

#include <stdio.h>
#include <string.h>
#include <pgmspace.h>

// f * scale, half to even as printf, scale 1 ties on ip
static uint32_t round_frac(float f, uint32_t scale, uint32_t ip) {
  uint32_t fb;
  memcpy(&fb, &f, sizeof(fb));
  if (!fb) return 0;
  uint64_t m = (fb & 0x7FFFFFu) | 0x800000u;
  int sh = 150 - (int)(fb >> 23);   // f = m / 2^sh; subnormals give sh >= 64
  if (sh >= 64) return 0;
  uint64_t p = m * scale;
  uint64_t qq = p >> sh;
  uint64_t r = p - (qq << sh);
  uint64_t half = (uint64_t)1 << (sh - 1);
  uint64_t last = scale > 1 ? qq : ip;
  if (r > half || (r == half && (last & 1))) qq++;
  return (uint32_t)qq;
}

int format_f(char* buf, size_t bufsz, float v, uint8_t decimals) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  // a "-" literal goes in DRAM
  size_t o = 0;
  if ((bits >> 31) && bufsz > 1) buf[o++] = '-';
  int neg = (int)(bits >> 31);
  bits &= 0x7FFFFFFFu;
  if (bits >= 0x7F800000u)
    return neg + snprintf_P(buf + o, bufsz - o, bits > 0x7F800000u ? PSTR("nan") : PSTR("inf"));
  if (bits >= 0x4F800000u) bits = 0x4F7FFFFFu;   // saturate below 2^32
  memcpy(&v, &bits, sizeof(v));

  uint32_t ip = (uint32_t)v;
  if (decimals > 9) decimals = 9;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  uint32_t q = round_frac(v - (float)ip, scale, ip);
  if (q >= scale) { q -= scale; ip++; }
  if (!decimals) return neg + snprintf_P(buf + o, bufsz - o, PSTR("%lu"), (unsigned long)ip);
  return neg + snprintf_P(buf + o, bufsz - o, PSTR("%lu.%0*lu"),
                          (unsigned long)ip, (int)decimals, (unsigned long)q);
}
