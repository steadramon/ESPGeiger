/*
  EGDnsAnswer.cpp - captive portal DNS: every A query gets one address.

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
#include "EGDnsAnswer.h"

#include <string.h>

// header byte 2
#define DNS_QR     0x80
#define DNS_OPCODE 0x78

#define RC_NOERROR  0
#define RC_FORMERR  1
#define RC_NOTIMP   4

#define T_A    1
#define T_ANY  255
#define C_IN   1
#define C_ANY  255

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)(p[0] << 8 | p[1]);
}

static void wr16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void set_rcode(uint8_t* h, uint8_t rc) {
  h[3] = (uint8_t)((h[3] & 0xF0) | rc);
}

// no question, no records
static size_t header_only(const uint8_t* q, uint8_t rc, uint8_t* out, size_t max) {
  if (max < EGDNS_HEADER) return 0;
  memcpy(out, q, EGDNS_HEADER);
  out[2] |= DNS_QR;
  set_rcode(out, rc);
  memset(out + 4, 0, EGDNS_HEADER - 4);
  return EGDNS_HEADER;
}

// bytes up to and with the root, 0 = bad; no pointers
static size_t name_len(const uint8_t* p, size_t left) {
  size_t n = 0;
  for (;;) {
    if (n >= left) return 0;
    uint8_t l = p[n];
    if (l == 0) return n + 1;
    if (l > EGDNS_LABEL_MAX) return 0;
    n += 1 + l;
    if (n >= EGDNS_NAME_MAX) return 0;   // the root makes it 256
  }
}

// one question, nothing else: edns gets FORMERR
static bool one_question(const uint8_t* q) {
  return rd16(q + 4) == 1 && !rd16(q + 6) && !rd16(q + 8) && !rd16(q + 10);
}

static bool asks_a(const uint8_t* q, size_t end) {
  uint16_t type = rd16(q + end - 4);
  uint16_t cls = rd16(q + end - 2);
  return (type == T_A || type == T_ANY) && (cls == C_IN || cls == C_ANY);
}

static void put_a(uint8_t* a, const uint8_t ip[4], uint32_t ttl) {
  wr16(a, 0xC000 | EGDNS_HEADER);   // the question's name
  wr16(a + 2, T_A);
  wr16(a + 4, C_IN);
  wr16(a + 6, (uint16_t)(ttl >> 16));
  wr16(a + 8, (uint16_t)ttl);
  wr16(a + 10, 4);
  memcpy(a + 12, ip, 4);
}

size_t egdns_reply(const uint8_t* q, size_t len, const uint8_t ip[4], uint32_t ttl,
                   uint8_t* out, size_t max) {
  if (!q || !ip || !out || len < EGDNS_HEADER || len > EGDNS_MSG_MAX) return 0;
  if (q[2] & DNS_QR) return 0;
  if (q[2] & DNS_OPCODE) return header_only(q, RC_NOTIMP, out, max);
  if (!one_question(q)) return header_only(q, RC_FORMERR, out, max);
  size_t name = name_len(q + EGDNS_HEADER, len - EGDNS_HEADER);
  if (!name || len - EGDNS_HEADER - name < 4) return header_only(q, RC_FORMERR, out, max);

  size_t end = EGDNS_HEADER + name + 4;   // bytes past the question not echoed
  bool ok = asks_a(q, end);
  size_t n = ok ? end + EGDNS_ANSWER : end;
  if (n > max) return 0;

  memcpy(out, q, end);
  out[2] |= DNS_QR;
  wr16(out + 6, ok ? 1 : 0);
  memset(out + 8, 0, 4);
  if (!ok) {
    set_rcode(out, RC_NOERROR);   // the name is there, no records
    return n;
  }
  // today: rcode echoed
  put_a(out + end, ip, ttl);
  return n;
}

bool EGDnsAnswer::begin(uint16_t port, const EGTcpAddr& ip, uint32_t ttl) {
  memcpy(_ip, ip.b, 4);
  _ttl = ttl;
  _udp.onPacket([this](const uint8_t* q, size_t len, const EGTcpAddr& from, uint16_t p) {
    answer(q, len, from, p);
  });
  return _udp.begin(port);
}

void EGDnsAnswer::answer(const uint8_t* q, size_t len, const EGTcpAddr& from, uint16_t port) {
  uint8_t out[EGDNS_REPLY_MAX];
  size_t n = egdns_reply(q, len, _ip, _ttl, out, sizeof(out));
  if (n) _udp.sendTo(from, port, out, n);
}
