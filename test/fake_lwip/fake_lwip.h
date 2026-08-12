// Control surface for the fake lwIP, used by the async TCP suite.
//
// The fake owns tcp_active_pcbs and tcp_tw_pcbs, which is what the client's
// liveness guards walk. A test can therefore retire a pcb the way lwIP does
// and then deliver a callback carrying the stale pointer; an unguarded deref
// is a genuine use-after-free that ASan traps.
//
// Every pcb is a separate heap allocation, never pooled or reused, so a stale
// pointer stays poisoned for the life of the test.

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

// Drops every pcb and pending lookup, and clears the counters. Call from
// setUp so one test cannot leak state into the next.
void reset();

// --- pcb lifecycle ----------------------------------------------------------

// Allocates a pcb and puts it on tcp_active_pcbs, as tcp_new does.
tcp_pcb* new_pcb();

// Moves the pcb off tcp_active_pcbs and frees it, as lwIP does when a
// connection is torn down. The pointer is dangling afterwards: that is the
// point. Liveness guards must reject it.
void retire_pcb(tcp_pcb* pcb);

// Moves the pcb to tcp_tw_pcbs without freeing. Still reachable, so guards
// that walk both lists must accept it.
void move_to_timewait(tcp_pcb* pcb);

bool is_active(const tcp_pcb* pcb);

// Takes the pcb off both lwIP lists without freeing it, so liveness guards
// read it as dead while the memory stays valid. Models a pcb the client's
// _pcb still names after lwIP has finished with it, without the synthetic
// use-after-free that freeing it outright would create.
void unlink_pcb(tcp_pcb* pcb);

// --- driving the client -----------------------------------------------------
//
// Each returns the value the client's callback returned, so a test can assert
// on ERR_ABRT propagation.

err_t fire_connected(tcp_pcb* pcb, err_t err);
err_t fire_recv(tcp_pcb* pcb, pbuf* pb, err_t err);
err_t fire_sent(tcp_pcb* pcb, uint16_t len);
err_t fire_poll(tcp_pcb* pcb);
void  fire_error(tcp_pcb* pcb, err_t err);

// True once the client has registered a recv callback for this pcb.
bool has_recv_cb(const tcp_pcb* pcb);

// Everything lwIP holds for a pcb. Captured so a test can deliver a callback
// after the client is gone: lwIP owns these, not the client, which is why a
// late delivery is reachable at all.
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

// Marks the pcb as still owned by lwIP: tcp_close/tcp_abort unlink it but do
// not free it. Models a pcb that outlives the client holding it, which is the
// only way a callback can be dispatched after the client is destroyed.
void pin_pcb(tcp_pcb* pcb);

// Drives an accept on a live listening pcb.
void fire_accept(tcp_pcb* listen_pcb, tcp_pcb* new_pcb, err_t err);

// --- DNS --------------------------------------------------------------------

// Delivers the found-callback for the pending lookup at `index`. Pass nullptr
// for ip to signal lookup failure. Safe to call after the requesting client
// has been destroyed: that is the crash this models.
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
  // Calls naming a pcb lwIP has already freed. All must stay 0.
  int recved_on_dead_pcb;
  int wrote_on_dead_pcb;
  int closed_dead_pcb;
};
const Counts& counts();

// Bytes handed to tcp_write since reset, and the free space tcp_write reports.
size_t bytes_written();
void   set_sndbuf(uint16_t bytes);

// Next tcp_write returns this instead of ERR_OK. Cleared after one use.
void fail_next_write(err_t err);

// --- pbuf helpers -----------------------------------------------------------

// Heap-allocates a pbuf carrying a copy of the payload. Freed by pbuf_free.
pbuf* make_pbuf(const void* data, uint16_t len);

}

#endif
