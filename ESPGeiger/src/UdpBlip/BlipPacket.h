/*
  BlipPacket.h - UdpBlip OSC packets from plain values

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
#ifndef BLIP_PACKET_H
#define BLIP_PACKET_H

#include <stddef.h>
#include <stdint.h>

// "/espg/" id "/click", padded
constexpr size_t BLIP_ID_AT     = 6;
constexpr size_t BLIP_ID_LEN    = 6;
constexpr size_t BLIP_SUFFIX_AT = BLIP_ID_AT + BLIP_ID_LEN + 1;
constexpr size_t BLIP_ARGS_AT   = 20;
// ",ii" count ms
constexpr size_t BLIP_CLICK_COUNT_AT = BLIP_ARGS_AT + 4;
constexpr size_t BLIP_CLICK_MS_AT    = BLIP_CLICK_COUNT_AT + 4;
constexpr size_t BLIP_CLICK_LEN      = BLIP_CLICK_MS_AT + 4;
// ",fff" t h p
constexpr size_t BLIP_ENV_AT  = BLIP_ARGS_AT + 8;
constexpr size_t BLIP_ENV_LEN = BLIP_ENV_AT + 12;

struct BlipRad {
  float cpm, usv;
  const char* state;
  uint32_t clicks;
};
struct BlipHv {
  float v, target;
  uint32_t duty;
  int32_t trim;
};
struct BlipSys {
  uint32_t uptime;
  int32_t rssi;
  uint32_t lps, tick_max;
};

// 0 = no room
size_t blip_click(uint8_t* buf, size_t cap, const char* path);
size_t blip_rad(uint8_t* buf, size_t cap, const char* path, const BlipRad& r);
size_t blip_hv(uint8_t* buf, size_t cap, const char* path, const BlipHv& h);
// humidity NaN on BMP280
size_t blip_env(uint8_t* buf, size_t cap, const char* path, float t, float h, float p);
size_t blip_sys(uint8_t* buf, size_t cap, const char* path, const BlipSys& s);

#endif
