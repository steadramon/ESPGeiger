/*
  EGLwipHop.h - onto lwIP's side, and its addresses. EGTcpLwip and EGUdp only.

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
#ifndef EGLWIPHOP_H
#define EGLWIPHOP_H

#include "EGTcp.h"
#include <string.h>

extern "C" {
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
}
#if defined(ESP32)
#include "lwip/priv/tcpip_priv.h"
#endif

// each file has its own Call, so nothing here may link across
namespace {

#if defined(ESP32)
template <typename C>
struct Hop {
  struct tcpip_api_call_data call;
  void (*fn)(C*);
  C* m;
};

template <typename C>
err_t hop(struct tcpip_api_call_data* d) {
  Hop<C>* h = reinterpret_cast<Hop<C>*>(d);
  h->fn(h->m);
  return ERR_OK;
}
#endif

// fn fills m.err
template <typename C>
err_t on_stack(void (*fn)(C*), C& m) {
#if defined(ESP32)
  Hop<C> h{};
  h.fn = fn;
  h.m = &m;
  tcpip_api_call(hop<C>, &h.call);
#else
  fn(&m);
#endif
  return m.err;
}

void to_addr(const ip_addr_t& ip, EGTcpAddr* a) {
  *a = EGTcpAddr();
#if LWIP_IPV6
  if (IP_IS_V6(&ip)) {
    a->v6 = 1;
    memcpy(a->b, ip_2_ip6(&ip)->addr, 16);
    return;
  }
#endif
  uint32_t v = ip4_addr_get_u32(ip_2_ip4(&ip));
  memcpy(a->b, &v, 4);   // network order both
}

void from_addr(const EGTcpAddr& a, ip_addr_t* ip) {
  memset(ip, 0, sizeof(*ip));
#if LWIP_IPV6
  if (a.v6) {
    IP_SET_TYPE_VAL(*ip, IPADDR_TYPE_V6);
    memcpy(ip_2_ip6(ip)->addr, a.b, 16);
    return;
  }
#endif
  uint32_t v;
  memcpy(&v, a.b, 4);
  ip_addr_set_ip4_u32(ip, v);
}

}

#endif
