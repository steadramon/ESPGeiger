/*
  EGUdp.cpp - lwIP raw UDP. ESP32 hands each call to lwIP's thread,
  the ESP8266 and the host tests call lwIP straight.

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
#include "EGUdp.h"
#include "EGTcpPort.h"
#include "EGLwipHop.h"

#include <new>
#include <string.h>

extern "C" {
#include "lwip/opt.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/pbuf.h"
}

namespace {
struct Call {
  EGUdp* u;
  const ip_addr_t* addr;
  ip4_addr_t group;
  ip4_addr_t iface;
  uint16_t port;
  const void* data;
  size_t len;
  uint8_t ttl;
  bool on;
  err_t err;
};
}

static void to_ip4(const EGTcpAddr& a, ip4_addr_t* ip) {
  uint32_t v;
  memcpy(&v, a.b, 4);
  ip4_addr_set_u32(ip, v);
}

struct EGUdpLwip {
  static void bump(std::atomic<uint32_t>& n) {
    locked_add<uint32_t>(n, 1, std::memory_order_relaxed);
  }

  static void take(EGUdp* u, const pbuf* p, const ip_addr_t* addr, u16_t port) {
    EGTcpHold h;
    uint8_t n = u->_count.load(std::memory_order_relaxed);
    if (p->tot_len > u->_size) {
      bump(u->_big);
      return;
    }
    if (n >= u->_slots) {
      bump(u->_full);
      return;
    }
    EGUdp::Slot& s = u->_slot[u->_tail];
    pbuf_copy_partial(p, u->_buf + (size_t)u->_tail * u->_size, p->tot_len, 0);
    s.len = p->tot_len;
    s.port = port;
    if (addr) to_addr(*addr, &s.from);
    else s.from = EGTcpAddr();
    u->_tail = u->_tail + 1 < u->_slots ? u->_tail + 1 : 0;
    u->_count.store(n + 1, std::memory_order_release);
  }

  // lwIP's thread: copy it, free it, never call the app
  static void on_recv(void* arg, udp_pcb*, pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    EGUdp* u = (EGUdp*)arg;
    if (u) take(u, p, addr, port);
    pbuf_free(p);
  }

  /*
   * Calls, each body on lwIP's side
   */

  static void open_api(Call* m) {
    udp_pcb* p = udp_new();
    if (!p) {
      m->err = ERR_MEM;
      return;
    }
    m->err = udp_bind(p, IP_ADDR_ANY, m->port);
    if (m->err != ERR_OK) {
      udp_remove(p);
      return;
    }
    udp_recv(p, on_recv, m->u);
    m->u->_h = p;
    m->u->_port = p->local_port;
  }

  // no callback after this
  static void close_api(Call* m) {
    udp_pcb* p = (udp_pcb*)m->u->_h;
    m->u->_h = nullptr;
    if (p) udp_remove(p);
  }

  static void send_api(Call* m) {
    pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)m->len, PBUF_RAM);
    if (!p) {
      m->err = ERR_MEM;
      return;
    }
    memcpy(p->payload, m->data, m->len);   // PBUF_RAM is one piece
    m->err = udp_sendto((udp_pcb*)m->u->_h, p, m->addr, m->port);
    pbuf_free(p);
  }

  static void group_api(Call* m) {
    if (m->on) m->err = igmp_joingroup(&m->iface, &m->group);
    else m->err = igmp_leavegroup(&m->iface, &m->group);
  }

  static void mcast_api(Call* m) {
    udp_pcb* p = (udp_pcb*)m->u->_h;
    if (!p) return;
    udp_set_multicast_netif_addr(p, &m->iface);
    udp_set_multicast_ttl(p, m->ttl);
  }

  static bool group(const EGTcpAddr& group, const EGTcpAddr& iface, bool on) {
    Call m{};
    to_ip4(group, &m.group);
    to_ip4(iface, &m.iface);
    m.on = on;
    m.err = ERR_VAL;
    return on_stack(group_api, m) == ERR_OK;
  }
};

/*
 * The owner's side
 */

EGUdp::EGUdp(uint16_t max, uint8_t slots) : _size(max), _slots(slots) {}

EGUdp::~EGUdp() {
  end();
}

bool EGUdp::begin(uint16_t port) {
  if (_h) return false;
  if (_slots) {
    _slot = new (std::nothrow) Slot[_slots];
    _buf = new (std::nothrow) uint8_t[(size_t)_slots * _size];
  }
  Call m{};
  m.u = this;
  m.port = port;
  m.err = ERR_MEM;
  if ((_slots && (!_slot || !_buf)) || on_stack(EGUdpLwip::open_api, m) != ERR_OK) {
    freeRing();
    return false;
  }
  return true;
}

void EGUdp::freeRing() {
  delete[] _slot;
  delete[] _buf;
  _slot = nullptr;
  _buf = nullptr;
}

void EGUdp::end() {
  leave();
  if (_h) {
    Call m{};
    m.u = this;
    on_stack(EGUdpLwip::close_api, m);
  }
  freeRing();
  _head = _tail = 0;
  _count.store(0, std::memory_order_relaxed);
  _port = 0;
  _life++;
}

bool EGUdp::join(const EGTcpAddr& group, const EGTcpAddr& iface) {
  if (group.v6 || iface.v6) return false;
  leave();
  if (!EGUdpLwip::group(group, iface, true)) return false;
  _group = group;
  _iface = iface;
  _joined = true;
  return true;
}

void EGUdp::leave() {
  if (!_joined) return;
  _joined = false;
  EGUdpLwip::group(_group, _iface, false);
}

void EGUdp::setMulticast(const EGTcpAddr& iface, uint8_t ttl) {
  if (!_h || iface.v6) return;
  Call m{};
  m.u = this;
  to_ip4(iface, &m.iface);
  m.ttl = ttl;
  on_stack(EGUdpLwip::mcast_api, m);
}

bool EGUdp::sendTo(const EGTcpAddr& to, uint16_t port, const void* data, size_t len) {
  if (!_h || !data || !len || len > UINT16_MAX) return false;
  ip_addr_t ip;
  from_addr(to, &ip);
  Call m{};
  m.u = this;
  m.addr = &ip;
  m.port = port;
  m.data = data;
  m.len = len;
  m.err = ERR_VAL;
  return on_stack(EGUdpLwip::send_api, m) == ERR_OK;
}

// only what was queued on entry, a flood waits for the next pass
void EGUdp::run() {
  if (_in_run) return;   // polled from onPacket
  _in_run = true;
  uint8_t life = _life;
  uint8_t n = _count.load(std::memory_order_acquire);
  while (n--) {
    const Slot& s = _slot[_head];
    if (_on_packet) _on_packet(_buf + (size_t)_head * _size, s.len, s.from, s.port);
    if (_life != life) break;   // end() in onPacket
    EGTcpHold h;
    _head = _head + 1 < _slots ? _head + 1 : 0;
    locked_sub<uint8_t>(_count, 1, std::memory_order_release);
  }
  _in_run = false;
}
