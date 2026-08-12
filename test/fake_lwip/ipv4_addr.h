// ESP8266 SDK type. The core's IPAddress.h derives struct ip_addr from this
// when IPv6 is off, which is what lets a `const ip_addr*` bind to an IPAddress
// parameter. Layout matches ip4_addr_t; the core relies on that.

#ifndef FAKE_IPV4_ADDR_H
#define FAKE_IPV4_ADDR_H

#include <stdint.h>

struct ipv4_addr {
  uint32_t addr;
};

typedef struct ipv4_addr ipv4_addr_t;

#endif
