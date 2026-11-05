/*
  EGDnsAnswer.h - captive portal DNS: every A query gets one address.

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
#pragma once

// answers as the esp8266 core's DNSServer with start(port, "*", ip), but no records = NOERROR

#include <stddef.h>
#include <stdint.h>
#include "EGUdp.h"

#define EGDNS_MSG_MAX 512    // rfc 1035 udp, the core drops bigger
#define EGDNS_HEADER 12
#define EGDNS_LABEL_MAX 63
#define EGDNS_NAME_MAX 255   // on the wire, lengths and root counted
#define EGDNS_QTAIL 4        // qtype, qclass
#define EGDNS_ANSWER 16      // pointer, type, class, ttl, length, ip
#define EGDNS_REPLY_MAX (EGDNS_HEADER + EGDNS_NAME_MAX + EGDNS_QTAIL + EGDNS_ANSWER)

#ifndef EGDNS_TTL
#define EGDNS_TTL 60   // the core's
#endif
#ifndef EGDNS_SLOTS
#define EGDNS_SLOTS 4
#endif

// reply length, 0 = none; never reads past len or writes past max
size_t egdns_reply(const uint8_t* q, size_t len, const uint8_t ip[4], uint32_t ttl,
                   uint8_t* out, size_t max);

class EGDnsAnswer {
  public:
    EGDnsAnswer() : _udp(EGDNS_MSG_MAX, EGDNS_SLOTS) {}

    bool begin(uint16_t port, const EGTcpAddr& ip, uint32_t ttl = EGDNS_TTL);
    void end() { _udp.end(); }
    void poll() { _udp.poll(); }

  private:
    void answer(const uint8_t* q, size_t len, const EGTcpAddr& from, uint16_t port);

    EGUdp _udp;
    uint8_t _ip[4] = {0};
    uint32_t _ttl = EGDNS_TTL;
};
