// SDK type the core's IPAddress.h derives struct ip_addr from. Layout matches
// ip4_addr_t.


#ifndef FAKE_IPV4_ADDR_H
#define FAKE_IPV4_ADDR_H

#include <stdint.h>

struct ipv4_addr {
  uint32_t addr;
};

typedef struct ipv4_addr ipv4_addr_t;

#endif
