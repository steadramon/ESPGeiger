/*
  fake_lwip.h - fake lwIP control surface for native unit tests.

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

// pcbs never reused, ASan sees stale ones

#ifndef FAKE_LWIP_H
#define FAKE_LWIP_H

#include <stddef.h>
#include <stdint.h>
#include <string>

extern "C" {
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/priv/tcp_priv.h"   // tcp_active_pcbs / tcp_tw_pcbs
}

namespace FakeLwip {

void reset();   // call from setUp

// pcbs
tcp_pcb* new_pcb();
void retire_pcb(tcp_pcb* pcb);   // frees, pointer dangles
void move_to_timewait(tcp_pcb* pcb);
bool is_active(const tcp_pcb* pcb);
void unlink_pcb(tcp_pcb* pcb);   // unlinked, not freed

// Drive
err_t fire_connected(tcp_pcb* pcb, err_t err);
err_t fire_recv(tcp_pcb* pcb, pbuf* pb, err_t err);
err_t fire_sent(tcp_pcb* pcb, uint16_t len);
err_t fire_poll(tcp_pcb* pcb);
void  fire_error(tcp_pcb* pcb, err_t err);

bool has_recv_cb(const tcp_pcb* pcb);

// lets a callback land after the client
struct Snapshot {
  tcp_recv_fn      recv;
  tcp_sent_fn      sent;
  tcp_err_fn       errf;
  tcp_poll_fn      poll;
  tcp_connected_fn connected;
  tcp_accept_fn    accept;
  void*            arg;
};
Snapshot snapshot_cbs(const tcp_pcb* pcb);

err_t deliver_recv(const Snapshot& s, tcp_pcb* pcb, pbuf* pb, err_t err);
err_t deliver_sent(const Snapshot& s, tcp_pcb* pcb, uint16_t len);
err_t deliver_poll(const Snapshot& s, tcp_pcb* pcb);
err_t deliver_connected(const Snapshot& s, tcp_pcb* pcb, err_t err);
void  deliver_error(const Snapshot& s, err_t err);
err_t deliver_accept(const Snapshot& s, tcp_pcb* new_pcb, err_t err);

void pin_pcb(tcp_pcb* pcb);   // close/abort unlink only
void fire_accept(tcp_pcb* listen_pcb, tcp_pcb* new_pcb, err_t err);

// DNS
void fire_dns_found(size_t index, const ip_addr_t* ip);   // nullptr ip = failed lookup
size_t dns_pending_count();
const char* dns_pending_name(size_t index);

// Observe
struct Counts {
  int tcp_new;
  int tcp_connect;
  int tcp_close;
  int tcp_abort;
  int tcp_write;
  int tcp_output;
  int tcp_recved;
  int pbuf_free;
  int dns_lookups;
  // must stay 0
  int recved_on_dead_pcb;
  int wrote_on_dead_pcb;
  int closed_dead_pcb;
};
const Counts& counts();

size_t bytes_written();
void   set_sndbuf(uint16_t bytes);

void fail_next_write(err_t err);   // one shot
void fail_next_dns(err_t err);     // one shot
void fail_next_pcb();              // one shot, tcp_new gives nullptr

void ack_on_output(bool on);   // peer acks at once

// kept until reset()
struct Conn {
  tcp_pcb*    pcb;      // nullptr once lwIP freed it
  std::string sent;
  size_t      recved;
  bool        closed;
};
Conn* conn_of(const tcp_pcb* pcb);

// pbuf
pbuf* make_pbuf(const void* data, uint16_t len);   // freed by pbuf_free

}

#endif
