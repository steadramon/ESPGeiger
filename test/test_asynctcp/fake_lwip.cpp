// Fake lwIP implementation. Lives in the suite directory because a .cpp under
// a shared test/ directory is compiled but never linked.
//
// Callbacks live in the real tcp_pcb slots that LWIP_CALLBACK_API defines, so
// the client registers them exactly as it does on device.

#include "fake_lwip.h"

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

extern "C" {

// The client's liveness guards walk these. lwIP declares them in
// lwip/priv/tcp_priv.h; here the fake owns them.
struct tcp_pcb* tcp_active_pcbs = nullptr;
struct tcp_pcb* tcp_tw_pcbs     = nullptr;

// IP4_ADDR_ANY / IP_ANY_TYPE resolve to these. AsyncServer's port-only
// constructor takes their address.
const ip_addr_t ip_addr_any       = IPADDR4_INIT(IPADDR_ANY);
const ip_addr_t ip_addr_broadcast = IPADDR4_INIT(IPADDR_BROADCAST);

}

namespace {

FakeLwip::Counts g_counts{};
size_t   g_bytes_written = 0;
uint16_t g_sndbuf        = 4 * TCP_MSS;
err_t    g_write_fail    = ERR_OK;
bool     g_write_fail_armed = false;

struct PendingDns {
  std::string  name;
  dns_found_callback found;
  void*        arg;
};
std::vector<PendingDns> g_dns;

// Every pcb the fake handed out and has not freed. reset() drains it.
std::vector<tcp_pcb*> g_live;

// pcbs lwIP keeps past a close, and the accept callback per listening pcb.
std::vector<tcp_pcb*> g_pinned;
struct AcceptReg { tcp_pcb* pcb; tcp_accept_fn fn; };
std::vector<AcceptReg> g_accept;

bool is_pinned(const tcp_pcb* pcb) {
  for (const tcp_pcb* p : g_pinned) if (p == pcb) return true;
  return false;
}

// A pcb the fake still owns. Anything else has been freed, and touching it
// would be the fault under test.
bool is_allocated(const tcp_pcb* pcb) {
  for (const tcp_pcb* p : g_live) if (p == pcb) return true;
  return false;
}

void list_remove(tcp_pcb** head, tcp_pcb* pcb) {
  tcp_pcb** p = head;
  while (*p) {
    if (*p == pcb) { *p = pcb->next; pcb->next = nullptr; return; }
    p = &(*p)->next;
  }
}

void list_push(tcp_pcb** head, tcp_pcb* pcb) {
  pcb->next = *head;
  *head = pcb;
}

bool in_list(const tcp_pcb* head, const tcp_pcb* pcb) {
  for (const tcp_pcb* p = head; p; p = p->next) {
    if (p == pcb) return true;
  }
  return false;
}

void free_pcb(tcp_pcb* pcb) {
  list_remove(&tcp_active_pcbs, pcb);
  list_remove(&tcp_tw_pcbs, pcb);
  for (size_t i = 0; i < g_live.size(); i++) {
    if (g_live[i] == pcb) { g_live.erase(g_live.begin() + i); break; }
  }
  free(pcb);
}

}

namespace FakeLwip {

void reset() {
  g_pinned.clear();
  g_accept.clear();
  while (!g_live.empty()) free_pcb(g_live.back());
  tcp_active_pcbs = nullptr;
  tcp_tw_pcbs     = nullptr;
  g_dns.clear();
  g_counts = Counts{};
  g_bytes_written = 0;
  g_sndbuf = 4 * TCP_MSS;
  g_write_fail = ERR_OK;
  g_write_fail_armed = false;
}

tcp_pcb* new_pcb() {
  tcp_pcb* pcb = (tcp_pcb*)calloc(1, sizeof(tcp_pcb));
  pcb->state    = ESTABLISHED;
  pcb->mss      = TCP_MSS;
  pcb->snd_buf  = g_sndbuf;
  list_push(&tcp_active_pcbs, pcb);
  g_live.push_back(pcb);
  return pcb;
}

void retire_pcb(tcp_pcb* pcb) { if (pcb) free_pcb(pcb); }

void move_to_timewait(tcp_pcb* pcb) {
  if (!pcb) return;
  list_remove(&tcp_active_pcbs, pcb);
  pcb->state = TIME_WAIT;
  list_push(&tcp_tw_pcbs, pcb);
}

bool is_active(const tcp_pcb* pcb) { return in_list(tcp_active_pcbs, pcb); }

void unlink_pcb(tcp_pcb* pcb) {
  if (!pcb) return;
  list_remove(&tcp_active_pcbs, pcb);
  list_remove(&tcp_tw_pcbs, pcb);
  g_pinned.push_back(pcb);   // close/abort must not free it either
}

bool has_recv_cb(const tcp_pcb* pcb) { return pcb && pcb->recv != nullptr; }

err_t fire_connected(tcp_pcb* pcb, err_t err) {
  if (!pcb || !pcb->connected) return ERR_VAL;
  // lwIP has completed the handshake before it calls back. The client reads
  // pcb->state directly in connected() and space(), so it must be ESTABLISHED
  // by now or neither reports the connection as usable.
  if (err == ERR_OK) pcb->state = ESTABLISHED;
  return pcb->connected(pcb->callback_arg, pcb, err);
}

Snapshot snapshot_cbs(const tcp_pcb* pcb) {
  Snapshot s{};
  if (!pcb) return s;
  s.recv      = pcb->recv;
  s.sent      = pcb->sent;
  s.errf      = pcb->errf;
  s.poll      = pcb->poll;
  s.connected = pcb->connected;
  s.arg       = pcb->callback_arg;
  for (const AcceptReg& a : g_accept) {
    if (a.pcb == pcb) { s.accept = a.fn; break; }
  }
  return s;
}

err_t deliver_recv(const Snapshot& s, tcp_pcb* pcb, pbuf* pb, err_t err) {
  return s.recv ? s.recv(s.arg, pcb, pb, err) : ERR_VAL;
}
err_t deliver_sent(const Snapshot& s, tcp_pcb* pcb, uint16_t len) {
  return s.sent ? s.sent(s.arg, pcb, len) : ERR_VAL;
}
err_t deliver_poll(const Snapshot& s, tcp_pcb* pcb) {
  return s.poll ? s.poll(s.arg, pcb) : ERR_VAL;
}
err_t deliver_connected(const Snapshot& s, tcp_pcb* pcb, err_t err) {
  return s.connected ? s.connected(s.arg, pcb, err) : ERR_VAL;
}
void deliver_error(const Snapshot& s, err_t err) {
  if (s.errf) s.errf(s.arg, err);
}
err_t deliver_accept(const Snapshot& s, tcp_pcb* new_pcb, err_t err) {
  return s.accept ? s.accept(s.arg, new_pcb, err) : ERR_VAL;
}

void pin_pcb(tcp_pcb* pcb) { if (pcb) g_pinned.push_back(pcb); }

void fire_accept(tcp_pcb* listen_pcb, tcp_pcb* new_pcb, err_t err) {
  for (const AcceptReg& a : g_accept) {
    if (a.pcb == listen_pcb && a.fn) { a.fn(listen_pcb->callback_arg, new_pcb, err); return; }
  }
}

err_t fire_recv(tcp_pcb* pcb, pbuf* pb, err_t err) {
  if (!pcb || !pcb->recv) return ERR_VAL;
  // Snapshot before dispatch: the handler may retire this pcb, and lwIP holds
  // the callback itself rather than re-reading it afterwards.
  tcp_recv_fn fn = pcb->recv;
  void* arg = pcb->callback_arg;
  return fn(arg, pcb, pb, err);
}

err_t fire_sent(tcp_pcb* pcb, uint16_t len) {
  if (!pcb || !pcb->sent) return ERR_VAL;
  tcp_sent_fn fn = pcb->sent;
  void* arg = pcb->callback_arg;
  return fn(arg, pcb, len);
}

err_t fire_poll(tcp_pcb* pcb) {
  if (!pcb || !pcb->poll) return ERR_VAL;
  tcp_poll_fn fn = pcb->poll;
  void* arg = pcb->callback_arg;
  return fn(arg, pcb);
}

void fire_error(tcp_pcb* pcb, err_t err) {
  if (!pcb || !pcb->errf) return;
  tcp_err_fn fn = pcb->errf;
  void* arg = pcb->callback_arg;
  // lwIP has already freed the pcb by the time it calls the error callback.
  free_pcb(pcb);
  fn(arg, err);
}

void fire_dns_found(size_t index, const ip_addr_t* ip) {
  if (index >= g_dns.size()) return;
  PendingDns p = g_dns[index];
  g_dns.erase(g_dns.begin() + index);
  if (p.found) p.found(p.name.c_str(), ip, p.arg);
}

size_t dns_pending_count() { return g_dns.size(); }

const char* dns_pending_name(size_t index) {
  return index < g_dns.size() ? g_dns[index].name.c_str() : nullptr;
}

const Counts& counts() { return g_counts; }
size_t bytes_written()  { return g_bytes_written; }

void set_sndbuf(uint16_t bytes) {
  g_sndbuf = bytes;
  for (tcp_pcb* p : g_live) p->snd_buf = bytes;
}

void fail_next_write(err_t err) { g_write_fail = err; g_write_fail_armed = true; }

pbuf* make_pbuf(const void* data, uint16_t len) {
  pbuf* pb = (pbuf*)calloc(1, sizeof(pbuf));
  pb->payload = malloc(len ? len : 1);
  if (data && len) memcpy(pb->payload, data, len);
  pb->len = len;
  pb->tot_len = len;
  pb->next = nullptr;
  pb->ref = 1;
  return pb;
}

}

// --- the lwIP API the client calls -------------------------------------------

extern "C" {

struct tcp_pcb* tcp_new(void) {
  g_counts.tcp_new++;
  tcp_pcb* pcb = FakeLwip::new_pcb();
  // Not connected yet: tcp_new hands back a fresh pcb in CLOSED.
  pcb->state = CLOSED;
  return pcb;
}

struct tcp_pcb* tcp_new_ip_type(u8_t type) { (void)type; return tcp_new(); }

void tcp_arg(struct tcp_pcb* pcb, void* arg)          { if (pcb) pcb->callback_arg = arg; }
void tcp_recv(struct tcp_pcb* pcb, tcp_recv_fn f)     { if (pcb) pcb->recv = f; }
void tcp_sent(struct tcp_pcb* pcb, tcp_sent_fn f)     { if (pcb) pcb->sent = f; }
void tcp_err(struct tcp_pcb* pcb, tcp_err_fn f)       { if (pcb) pcb->errf = f; }
// The accept slot lives on tcp_pcb_listen, not tcp_pcb, so the fake keeps it
// on the side.
void tcp_accept(struct tcp_pcb* pcb, tcp_accept_fn f) {
  if (!pcb) return;
  for (AcceptReg& a : g_accept) {
    if (a.pcb == pcb) { a.fn = f; return; }
  }
  g_accept.push_back({ pcb, f });
}
void tcp_setprio(struct tcp_pcb* pcb, u8_t prio)      { if (pcb) pcb->prio = prio; }

void tcp_poll(struct tcp_pcb* pcb, tcp_poll_fn f, u8_t interval) {
  if (!pcb) return;
  pcb->poll = f;
  pcb->pollinterval = interval;
}

void tcp_recved(struct tcp_pcb* pcb, u16_t len) {
  g_counts.tcp_recved++;
  if (!pcb) return;
  if (!in_list(tcp_active_pcbs, pcb) && !in_list(tcp_tw_pcbs, pcb)) {
    // The caller acked against a pcb lwIP has already freed. Record it rather
    // than dereference, so the failure is a clear assertion instead of an
    // ASan report in whichever test happens to run first.
    g_counts.recved_on_dead_pcb++;
    return;
  }
  pcb->rcv_wnd = (tcpwnd_size_t)(pcb->rcv_wnd + len);
}

err_t tcp_bind(struct tcp_pcb* pcb, const ip_addr_t* ipaddr, u16_t port) {
  if (!pcb) return ERR_ARG;
  if (ipaddr) ip_addr_copy(pcb->local_ip, *ipaddr);
  pcb->local_port = port;
  return ERR_OK;
}

err_t tcp_connect(struct tcp_pcb* pcb, const ip_addr_t* ipaddr, u16_t port,
                  tcp_connected_fn connected) {
  g_counts.tcp_connect++;
  if (!pcb) return ERR_ARG;
  if (ipaddr) ip_addr_copy(pcb->remote_ip, *ipaddr);
  pcb->remote_port = port;
  pcb->connected   = connected;
  pcb->state       = SYN_SENT;
  return ERR_OK;
}

struct tcp_pcb* tcp_listen_with_backlog(struct tcp_pcb* pcb, u8_t backlog) {
  (void)backlog;
  if (pcb) pcb->state = LISTEN;
  return pcb;
}

err_t tcp_write(struct tcp_pcb* pcb, const void* dataptr, u16_t len, u8_t apiflags) {
  (void)dataptr; (void)apiflags;
  g_counts.tcp_write++;
  if (!pcb) return ERR_ARG;
  if (g_write_fail_armed) { g_write_fail_armed = false; return g_write_fail; }
  if (!is_allocated(pcb)) { g_counts.wrote_on_dead_pcb++; return ERR_ARG; }
  if (len > pcb->snd_buf) return ERR_MEM;
  pcb->snd_buf = (tcpwnd_size_t)(pcb->snd_buf - len);
  g_bytes_written += len;
  return ERR_OK;
}

err_t tcp_output(struct tcp_pcb* pcb) {
  g_counts.tcp_output++;
  return pcb ? ERR_OK : ERR_ARG;
}

err_t tcp_close(struct tcp_pcb* pcb) {
  g_counts.tcp_close++;
  if (!pcb) return ERR_ARG;
  if (!is_allocated(pcb)) { g_counts.closed_dead_pcb++; return ERR_ARG; }
  // lwIP frees the pcb on a successful close, so the caller's pointer is
  // dangling from here on. A pinned pcb is only unlinked: it models one lwIP
  // still owns, which is the only way a later callback is reachable.
  if (is_pinned(pcb)) {
    list_remove(&tcp_active_pcbs, pcb);
    list_remove(&tcp_tw_pcbs, pcb);
    return ERR_OK;
  }
  FakeLwip::retire_pcb(pcb);
  return ERR_OK;
}

void tcp_abort(struct tcp_pcb* pcb) {
  g_counts.tcp_abort++;
  if (!pcb) return;
  if (!is_allocated(pcb)) { g_counts.closed_dead_pcb++; return; }
  if (is_pinned(pcb)) {
    list_remove(&tcp_active_pcbs, pcb);
    list_remove(&tcp_tw_pcbs, pcb);
    return;
  }
  FakeLwip::retire_pcb(pcb);
}

// --- pbuf ---------------------------------------------------------------------

u8_t pbuf_free(struct pbuf* p) {
  u8_t freed = 0;
  while (p) {
    struct pbuf* next = p->next;
    g_counts.pbuf_free++;
    free(p->payload);
    free(p);
    freed++;
    p = next;
  }
  return freed;
}

void pbuf_chain(struct pbuf* h, struct pbuf* t) {
  if (!h || !t) return;
  struct pbuf* p = h;
  while (p->next) p = p->next;
  p->next = t;
  for (p = h; p; p = p->next) {
    u16_t tot = 0;
    for (const struct pbuf* q = p; q; q = q->next) tot = (u16_t)(tot + q->len);
    p->tot_len = tot;
  }
}

// --- dns ----------------------------------------------------------------------

err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr,
                        dns_found_callback found, void* callback_arg) {
  (void)addr;
  g_counts.dns_lookups++;
  g_dns.push_back({ hostname ? hostname : "", found, callback_arg });
  return ERR_INPROGRESS;
}

}
