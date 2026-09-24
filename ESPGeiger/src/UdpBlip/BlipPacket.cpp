/*
  BlipPacket.cpp - UdpBlip OSC packets from plain values

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
#include "BlipPacket.h"
#include <string.h>
#include <pgmspace.h>

// OSC, big-endian, 4-byte aligned

static inline size_t osc_pad(size_t n) { return (n + 3) & ~3u; }

static size_t osc_strn(uint8_t* buf, size_t cap, size_t off, const char* s, size_t len) {
  size_t padded = osc_pad(len + 1);
  if (off + padded > cap) return 0;
  memcpy_P(buf + off, s, len);
  for (size_t i = len; i < padded; i++) buf[off + i] = 0;
  return padded;
}

static inline size_t osc_str(uint8_t* buf, size_t cap, size_t off, const char* s) {
  return osc_strn(buf, cap, off, s, strlen_P(s));
}

static size_t osc_i32(uint8_t* buf, size_t cap, size_t off, uint32_t v) {
  if (off + 4 > cap) return 0;
  buf[off    ] = (uint8_t)(v >> 24);
  buf[off + 1] = (uint8_t)(v >> 16);
  buf[off + 2] = (uint8_t)(v >>  8);
  buf[off + 3] = (uint8_t)(v);
  return 4;
}

static size_t osc_f32(uint8_t* buf, size_t cap, size_t off, float v) {
  uint32_t u;
  memcpy(&u, &v, 4);
  return osc_i32(buf, cap, off, u);
}

size_t blip_click(uint8_t* buf, size_t cap, const char* path) {
  size_t off = 0, n;
  if (!(n = osc_str(buf, cap, off, path))) return 0; off += n;
  if (!(n = osc_strn(buf, cap, off, PSTR(",ii"), 3))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, 0))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, 0))) return 0; off += n;
  return off;
}

size_t blip_rad(uint8_t* buf, size_t cap, const char* path, const BlipRad& r) {
  size_t off = 0, n;
  if (!(n = osc_str(buf, cap, off, path))) return 0; off += n;
  if (!(n = osc_strn(buf, cap, off, PSTR(",ffsi"), 5))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, r.cpm))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, r.usv))) return 0; off += n;
  if (!(n = osc_str(buf, cap, off, r.state))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, r.clicks))) return 0; off += n;
  return off;
}

size_t blip_hv(uint8_t* buf, size_t cap, const char* path, const BlipHv& h) {
  size_t off = 0, n;
  if (!(n = osc_str(buf, cap, off, path))) return 0; off += n;
  if (!(n = osc_strn(buf, cap, off, PSTR(",ffii"), 5))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, h.v))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, h.target))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, h.duty))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, (uint32_t)h.trim))) return 0; off += n;
  return off;
}

size_t blip_env(uint8_t* buf, size_t cap, const char* path, float t, float h, float p) {
  size_t off = 0, n;
  if (!(n = osc_str(buf, cap, off, path))) return 0; off += n;
  if (!(n = osc_strn(buf, cap, off, PSTR(",fff"), 4))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, t))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, h))) return 0; off += n;
  if (!(n = osc_f32(buf, cap, off, p))) return 0; off += n;
  return off;
}

size_t blip_sys(uint8_t* buf, size_t cap, const char* path, const BlipSys& s) {
  size_t off = 0, n;
  if (!(n = osc_str(buf, cap, off, path))) return 0; off += n;
  if (!(n = osc_strn(buf, cap, off, PSTR(",iiii"), 5))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, s.uptime))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, (uint32_t)s.rssi))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, s.lps))) return 0; off += n;
  if (!(n = osc_i32(buf, cap, off, s.tick_max))) return 0; off += n;
  return off;
}
