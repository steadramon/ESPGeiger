// Minimal IPAddress for the async TCP suite.
//
// Lives here rather than in test/shim because it is lwIP-coupled: the client
// passes an IPAddress straight to tcp_connect and dns_gethostbyname, which is
// what the implicit ip_addr_t conversions are for. Only suites with the lwIP
// headers on their include path can use it.
//
// The type relationships mirror the ESP8266 core's IPAddress.h and must keep
// mirroring it. In particular `struct ip_addr` derives from the SDK's
// ipv4_addr when IPv6 is off, which is the only reason the client's
// `connect(const ip_addr*, port)` in _dns_found resolves.

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

    // The core carries all three integer widths; IPAddress(0) is ambiguous
    // without the int overload.
    IPAddress(uint32_t a)      { ip_addr_set_ip4_u32(&_ip, a); }
    IPAddress(unsigned long a) { ip_addr_set_ip4_u32(&_ip, (uint32_t)a); }
    IPAddress(int a)           { ip_addr_set_ip4_u32(&_ip, (uint32_t)a); }

    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
      IP4_ADDR(ip_2_ip4(&_ip), a, b, c, d);
    }

    IPAddress(const ip_addr_t& a) { ip_addr_copy(_ip, a); }
    IPAddress(const ip_addr_t* a) { if (a) ip_addr_copy(_ip, *a); else ip_addr_set_zero(&_ip); }

#if !LWIP_IPV6
    // Reached via the derived-to-base conversion from `const ip_addr*`.
    IPAddress(const ipv4_addr& a) { ip_addr_set_ip4_u32(&_ip, a.addr); }
    IPAddress(const ipv4_addr* a) { ip_addr_set_ip4_u32(&_ip, a ? a->addr : 0); }
#endif

    operator uint32_t() const         { return ip_addr_get_ip4_u32(&_ip); }
    operator const ip_addr_t*() const { return &_ip; }
    operator ip_addr_t*()             { return &_ip; }

    bool isSet() const { return ip_addr_get_ip4_u32(&_ip) != 0; }

    bool operator==(const IPAddress& o) const {
      return ip_addr_get_ip4_u32(&_ip) == ip_addr_get_ip4_u32(&o._ip);
    }
    bool operator!=(const IPAddress& o) const { return !(*this == o); }

  private:
    ip_addr_t _ip;
};

#endif
