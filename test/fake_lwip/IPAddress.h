/*
  IPAddress.h - host IPAddress for native unit tests.

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

// mirror the core's IPAddress.h

#ifndef FAKE_IPADDRESS_H
#define FAKE_IPADDRESS_H

#include <stdint.h>
#include "ipv4_addr.h"

extern "C" {
#include "lwip/init.h"
#include "lwip/ip_addr.h"
}

#if !LWIP_IPV6
struct ip_addr : ipv4_addr { };
#endif

class IPAddress {
  public:
    IPAddress() { ip_addr_set_zero(&_ip); }

    // int one stops IPAddress(0) being ambiguous
    IPAddress(uint32_t a)      { ip_addr_set_ip4_u32(&_ip, a); }
    IPAddress(unsigned long a) { ip_addr_set_ip4_u32(&_ip, (uint32_t)a); }
    IPAddress(int a)           { ip_addr_set_ip4_u32(&_ip, (uint32_t)a); }

    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
      IP4_ADDR(ip_2_ip4(&_ip), a, b, c, d);
    }

    IPAddress(const ip_addr_t& a) { ip_addr_copy(_ip, a); }
    IPAddress(const ip_addr_t* a) { if (a) ip_addr_copy(_ip, *a); else ip_addr_set_zero(&_ip); }

#if !LWIP_IPV6
    IPAddress(const ipv4_addr& a) { ip_addr_set_ip4_u32(&_ip, a.addr); }
    IPAddress(const ipv4_addr* a) { ip_addr_set_ip4_u32(&_ip, a ? a->addr : 0); }
#endif

    operator uint32_t() const         { return ip_addr_get_ip4_u32(&_ip); }
    operator const ip_addr_t*() const { return &_ip; }
    operator ip_addr_t*()             { return &_ip; }

    bool isSet() const { return ip_addr_get_ip4_u32(&_ip) != 0; }

    uint8_t operator[](int i) const { return ip4_addr_get_byte(ip_2_ip4(&_ip), i); }

    bool operator==(const IPAddress& o) const {
      return ip_addr_get_ip4_u32(&_ip) == ip_addr_get_ip4_u32(&o._ip);
    }
    bool operator!=(const IPAddress& o) const { return !(*this == o); }

  private:
    ip_addr_t _ip;
};

#endif
