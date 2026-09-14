// Control surface for the fake lwIP. Owns tcp_active_pcbs and tcp_tw_pcbs.
// Every pcb is its own heap allocation, never reused, so a stale pointer stays
// poisoned under ASan.

#ifndef FAKE_LWIP_H
#define FAKE_LWIP_H

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/priv/tcp_priv.h"   // tcp_active_pcbs / tcp_tw_pcbs
}

namespace FakeLwip {

// Call from setUp.
void reset();

// --- pcb lifecycle ----------------------------------------------------------

// As tcp_new.
tcp_pcb* new_pcb();

// Unlinks and frees; the pointer dangles afterwards.
void retire_pcb(tcp_pcb* pcb);

// Moves to tcp_tw_pcbs without freeing.
void move_to_timewait(tcp_pcb* pcb);

bool is_active(const tcp_pcb* pcb);

// Off both lists, memory kept, so guards read it dead without a use-after-free.
void unlink_pcb(tcp_pcb* pcb);

// --- driving the client -----------------------------------------------------
//
// Each returns what the client's callback returned.

err_t fire_connected(tcp_pcb* pcb, err_t err);
err_t fire_recv(tcp_pcb* pcb, pbuf* pb, err_t err);
err_t fire_sent(tcp_pcb* pcb, uint16_t len);
err_t fire_poll(tcp_pcb* pcb);
void  fire_error(tcp_pcb* pcb, err_t err);

// recv callback registered.
bool has_recv_cb(const tcp_pcb* pcb);

// What lwIP holds for a pcb, so a callback can be delivered after the client
// is gone.
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

// tcp_close/tcp_abort unlink but do not free: the pcb outlives the client.
void pin_pcb(tcp_pcb* pcb);

// Drives an accept on a live listening pcb.
void fire_accept(tcp_pcb* listen_pcb, tcp_pcb* new_pcb, err_t err);

// --- DNS --------------------------------------------------------------------

// nullptr ip is a failed lookup. Callable after the client is destroyed.
void fire_dns_found(size_t index, const ip_addr_t* ip);
size_t dns_pending_count();
const char* dns_pending_name(size_t index);

// --- observation ------------------------------------------------------------

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
  // Calls naming a freed pcb. Must stay 0.
  int recved_on_dead_pcb;
  int wrote_on_dead_pcb;
  int closed_dead_pcb;
};
const Counts& counts();

// Bytes handed to tcp_write since reset, and the space it reports.
size_t bytes_written();
void   set_sndbuf(uint16_t bytes);

// Next tcp_write returns this instead of ERR_OK. Cleared after one use.
void fail_next_write(err_t err);

// --- pbuf helpers -----------------------------------------------------------

// Freed by pbuf_free.

pbuf* make_pbuf(const void* data, uint16_t len);

}

#endif
