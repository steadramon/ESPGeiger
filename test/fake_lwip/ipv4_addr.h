/*
  ipv4_addr.h - host ipv4_addr for native unit tests.

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

// layout matches ip4_addr_t

#ifndef FAKE_IPV4_ADDR_H
#define FAKE_IPV4_ADDR_H

#include <stdint.h>

struct ipv4_addr {
  uint32_t addr;
};

typedef struct ipv4_addr ipv4_addr_t;

#endif
