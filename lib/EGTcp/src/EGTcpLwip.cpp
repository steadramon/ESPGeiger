/*
  EGTcpLwip.cpp - lwIP raw backend. ESP32 hands each call to lwIP's thread,
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
#include "EGTcpPort.h"
#include "EGLwipHop.h"

#include <new>
#include <string.h>

extern "C" {
#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/priv/tcp_priv.h"
}
#if defined(ESP32)
#include <Arduino.h>
#endif

// lwIP coarse ticks, 500 ms
#define EGTCP_POLL_TICKS 1

// closed, peer silent this long: abort, else lwIP retries 30+ min
#ifndef EGTCP_LINGER_S
#define EGTCP_LINGER_S 180
#endif
#define EGTCP_LINGER_TICKS 10

#define EGTCP_BACKLOG 5   // half-open SYNs

static_assert(EGTcpClient::COPY == TCP_WRITE_FLAG_COPY, "flags pass straight");
static_assert(EGTcpClient::MORE == TCP_WRITE_FLAG_MORE, "flags pass straight");
static_assert(EGTCP_LATER == ERR_INPROGRESS, "lwIP's number");

/*
 * Per chip: the owner's task, and the way onto lwIP's side
 */

#if defined(ESP32)
static TaskHandle_t s_owner = nullptr;

void EGTcpCore::owner() {
  if (!s_owner) s_owner = xTaskGetCurrentTaskHandle();
}

// once, a wrong task is a bug in the caller
void EGTcpCore::check() {
  static bool told = false;
  if (!s_owner || told || xTaskGetCurrentTaskHandle() == s_owner) return;
  told = true;
  log_e("EGTcp used off the task that polls");
}
#endif

struct Call {
  EGTcpClient* c;
  EGTcpServer* s;
  err_t err;
  const ip_addr_t* addr;
  uint16_t port;
  const void* data;
  size_t len;
  uint8_t flags;
  uint32_t ms;
  const char* host;
  ip_addr_t* out;
  EGTcpAsk* ask;
  EGTcpInfo* info;
};

typedef void (*StackFn)(Call* m);

// tmr = last heard
static err_t linger_poll(void*, tcp_pcb* p) {
  if (tcp_ticks - p->tmr < EGTCP_LINGER_S * 1000UL / TCP_SLOW_INTERVAL) return ERR_OK;
  tcp_abort(p);
  return ERR_ABRT;
}

// these trip lwIP write asserts
static bool writable(const tcp_pcb* p) {
  switch (p->state) {
    case ESTABLISHED: case CLOSE_WAIT: case SYN_SENT: case SYN_RCVD:
      return true;
    default:
      return false;
  }
}

static void close_or_abort(tcp_pcb* p) {
  if (tcp_close(p) != ERR_OK) tcp_abort(p);
}

struct EGTcpLwip {
  static tcp_pcb* pcb(const EGTcpClient* c) {
    return (tcp_pcb*)c->_h.load();
  }

  static void cache(EGTcpClient* c, tcp_pcb* p) {
    c->_snd.store(p ? tcp_sndbuf(p) : 0, std::memory_order_relaxed);
  }

  static void own(EGTcpClient* c, tcp_pcb* p) {
    c->_h.store(p);
    cache(c, p);
  }

  static void post(EGTcpClient* c, uint16_t ev) {
    EGTcpHold h;
    EGTcpCore::post(c, ev);
  }

  /*
   * lwIP's callbacks: note it, post it, never call the app
   */

  static err_t on_connected(void* arg, tcp_pcb* p, err_t) {
    EGTcpClient* c = (EGTcpClient*)arg;
    if (!c) return ERR_OK;
    cache(c, p);
    post(c, EV_CONNECTED);
    return ERR_OK;
  }

  static err_t on_recv(void* arg, tcp_pcb* p, pbuf* pb, err_t) {
    EGTcpClient* c = (EGTcpClient*)arg;
    if (!c) {
      if (pb) pbuf_free(pb);
      return ERR_OK;
    }
    cache(c, p);   // FIN moves the state
    EGTcpHold h;
    if (!pb) {
      EGTcpCore::post(c, EV_FIN);
      return ERR_OK;
    }
    if (c->_rx) pbuf_cat((pbuf*)c->_rx, pb);
    else c->_rx = pb;
    // lock held, no fetch_add on the 8266
    c->_rx_len.store(c->_rx_len.load(std::memory_order_relaxed) + pb->tot_len, std::memory_order_release);
    EGTcpCore::post(c, EV_RX);
    return ERR_OK;
  }

  static err_t on_sent(void* arg, tcp_pcb* p, u16_t len) {
    EGTcpClient* c = (EGTcpClient*)arg;
    if (!c) return ERR_OK;
    cache(c, p);   // room grew
    EGTcpHold h;
    c->_ev_acked += len;
    EGTcpCore::post(c, EV_SENT);
    return ERR_OK;
  }

  static err_t on_poll(void* arg, tcp_pcb* p) {
    EGTcpClient* c = (EGTcpClient*)arg;
    if (!c) return ERR_OK;
    cache(c, p);   // backstop
    // one word the owner keeps, idle costs the loop nothing
    uint32_t at = c->_due_at.load(std::memory_order_relaxed);
    if (at && (int32_t)(EGTcpCore::now() - at) >= 0) post(c, EV_POLL);
    return ERR_OK;
  }

  // lwIP freed the pcb, nothing to clear
  static void on_error(void* arg, err_t err) {
    EGTcpClient* c = (EGTcpClient*)arg;
    if (!c) return;
    EGTcpHold h;
    own(c, nullptr);
    c->_ev_err = err;
    EGTcpCore::post(c, EV_ERROR);
  }

  static void on_dns(const char*, const ip_addr_t* ip, void* arg) {
    EGTcpAsk* ask = (EGTcpAsk*)arg;
    {
      EGTcpHold h;
      EGTcpClient* c = ask->c;   // null: gone or asked again
      if (c) {
        c->_ask = nullptr;
        c->_ev_found = ip != nullptr;
        if (ip) to_addr(*ip, &c->_ev_addr);
        c->_ev_dns = ask->id;
        EGTcpCore::post(c, EV_DNS);
      }
    }
    delete ask;
  }

  // listening till end(), so the server is live
  static err_t on_accept(void* arg, tcp_pcb* p, err_t err) {
    EGTcpServer* s = (EGTcpServer*)arg;
    if (!s || !p || err != ERR_OK) return ERR_VAL;
    EGTcpClient* c = new (std::nothrow) EGTcpClient();
    if (!c) {
      tcp_abort(p);
      return ERR_ABRT;
    }
    if (s->_nodelay) tcp_nagle_disable(p);
    if (s->_max) tcp_backlog_delayed(p);   // holds a slot till closed
    bind(p, c);
    own(c, p);
    c->_state = EGTcpClient::CONNECTING;
    c->_srv = s;
    post(c, EV_ACCEPT | EV_CONNECTED);
    return ERR_OK;
  }

  static void bind(tcp_pcb* p, EGTcpClient* c) {
    tcp_arg(p, c);
    tcp_recv(p, on_recv);
    tcp_sent(p, on_sent);
    tcp_err(p, on_error);
    tcp_poll(p, on_poll, EGTCP_POLL_TICKS);
  }

  static void unbind(tcp_pcb* p) {
    tcp_arg(p, nullptr);
    tcp_recv(p, nullptr);
    tcp_sent(p, nullptr);
    tcp_err(p, nullptr);
    tcp_poll(p, nullptr, 0);
  }

  // drops it from the client, lwIP's after
  static tcp_pcb* release(EGTcpClient* c) {
    tcp_pcb* p = pcb(c);
    if (!p) return nullptr;
    own(c, nullptr);
    unbind(p);
    tcp_backlog_accepted(p);
    return p;
  }

  /*
   * Calls, each body on lwIP's side: _h is exact there
   */

  // nullptr = ERR_MEM in m
  static tcp_pcb* new_pcb(Call* m) {
#if LWIP_IPV4 && LWIP_IPV6
    tcp_pcb* p = tcp_new_ip_type(IP_GET_TYPE(m->addr));
#else
    tcp_pcb* p = tcp_new();
#endif
    if (!p) m->err = ERR_MEM;
    return p;
  }

  static void connect_api(Call* m) {
    tcp_pcb* p = new_pcb(m);
    if (!p) return;
    bind(p, m->c);
    own(m->c, p);
    m->err = tcp_connect(p, m->addr, m->port, on_connected);
    if (m->err != ERR_OK) close_or_abort(release(m->c));   // nothing left
  }

  // clamps to room; queued counts as taken even if output fails
  static void write_api(Call* m) {
    size_t want = m->len;
    m->len = 0;
    tcp_pcb* p = pcb(m->c);
    if (!p || !writable(p)) return;
    size_t n = want < tcp_sndbuf(p) ? want : tcp_sndbuf(p);
    if (n && tcp_write(p, m->data, (u16_t)n, m->flags) == ERR_OK) {
      m->len = n;
      if (!(m->flags & TCP_WRITE_FLAG_MORE)) tcp_output(p);   // lwIP retries unsent itself
    }
    cache(m->c, p);
  }

  static void recved_api(Call* m) {
    tcp_pcb* p = pcb(m->c);
    if (!p || p->rcv_wnd >= TCP_WND_MAX(p)) return;
    // lwIP asserts past the window
    size_t room = (size_t)(TCP_WND_MAX(p) - p->rcv_wnd);
    size_t len = m->len < room ? m->len : room;
    if (len) tcp_recved(p, (u16_t)len);
  }

  // never fails: a pcb that won't close is aborted
  static void close_api(Call* m) {
    tcp_pcb* p = release(m->c);
    if (!p) return;
    tcp_poll(p, linger_poll, EGTCP_LINGER_TICKS);   // before tcp_close, it can free p
    close_or_abort(p);
  }

  static void abort_api(Call* m) {
    tcp_pcb* p = release(m->c);   // no errf, the owner knows
    if (p) tcp_abort(p);
  }

  static void shutdown_api(Call* m) {
    tcp_pcb* p = pcb(m->c);
    if (p && writable(p)) tcp_shutdown(p, 0, 1);
  }

  static void info_api(Call* m) {
    tcp_pcb* p = pcb(m->c);
    if (!p) return;
    to_addr(p->local_ip, &m->info->local);
    to_addr(p->remote_ip, &m->info->remote);
    m->info->local_port = p->local_port;
    m->info->remote_port = p->remote_port;
    m->info->mss = tcp_mss(p);
  }

  static void nodelay_api(Call* m) {
    tcp_pcb* p = pcb(m->c);
    if (!p) return;
    if (m->flags) tcp_nagle_disable(p);
    else tcp_nagle_enable(p);
  }

  static void keepalive_api(Call* m) {
    tcp_pcb* p = pcb(m->c);
    if (!p) return;
    if (!m->ms) {
      p->so_options &= ~SOF_KEEPALIVE;
      return;
    }
    p->so_options |= SOF_KEEPALIVE;
    p->keep_idle = m->ms;
#if LWIP_TCP_KEEPALIVE
    p->keep_intvl = m->ms;
    p->keep_cnt = m->flags;
#endif
  }

  // lwIP's thread owns the DNS cache
  static void dns_api(Call* m) {
    m->err = dns_gethostbyname(m->host, m->out, on_dns, m->ask);
  }

  static void listen_api(Call* m) {
    tcp_pcb* p = new_pcb(m);
    if (!p) return;
    ip_set_option(p, SOF_REUSEADDR);   // rebind with clients in TIME_WAIT
    m->err = tcp_bind(p, m->addr, m->port);
    tcp_pcb* l = m->err == ERR_OK ? tcp_listen_with_backlog(p, m->flags) : nullptr;
    if (!l) {   // a failed listen keeps p
      close_or_abort(p);
      if (m->err == ERR_OK) m->err = ERR_MEM;
      return;
    }
    tcp_arg(l, m->s);
    tcp_accept(l, on_accept);
    m->s->_h.store(l);
  }

  static void unlisten_api(Call* m) {
    tcp_pcb* l = (tcp_pcb*)m->s->_h.load();
    if (!l) return;
    m->s->_h.store(nullptr);
    tcp_arg(l, nullptr);
    tcp_accept(l, nullptr);
    close_or_abort(l);
  }

  /*
   * The ops, owner's task
   */

  static int8_t connect(EGTcpClient* c, const EGTcpAddr& a, uint16_t port) {
    ip_addr_t ip;
    from_addr(a, &ip);
    Call m{};
    m.c = c;
    m.addr = &ip;
    m.port = port;
    m.err = ERR_CONN;
    return on_stack(connect_api, m);
  }

  static int8_t resolve(EGTcpClient* c, const char* host, EGTcpAddr* out) {
    EGTcpAsk* ask = new (std::nothrow) EGTcpAsk{c, c->_dns_id};
    if (!ask) return ERR_MEM;
    {
      // an older ask answers nobody
      EGTcpHold h;
      if (c->_ask) c->_ask->c = nullptr;
      c->_ask = ask;
    }
    ip_addr_t ip;
    memset(&ip, 0, sizeof(ip));
    Call m{};
    m.host = host;
    m.out = &ip;
    m.ask = ask;
    m.err = ERR_VAL;
    on_stack(dns_api, m);
    if (m.err != ERR_INPROGRESS) {
      // cached or failed, lwIP never calls back
      EGTcpHold h;
      c->_ask = nullptr;
      delete ask;
    }
    if (m.err == ERR_OK) to_addr(ip, out);
    return m.err;
  }

  static size_t write(EGTcpClient* c, const void* data, size_t len, uint8_t flags) {
    Call m{};
    m.c = c;
    m.data = data;
    m.len = len;
    m.flags = flags;
    on_stack(write_api, m);
    return m.len;
  }

  static void simple(StackFn fn, EGTcpClient* c, size_t len = 0, uint8_t flags = 0) {
    Call m{};
    m.c = c;
    m.len = len;
    m.flags = flags;
    on_stack(fn, m);
  }

  static void recved(EGTcpClient* c, size_t len) { simple(recved_api, c, len); }
  static void close(EGTcpClient* c) { simple(close_api, c); }
  static void abort(EGTcpClient* c) { simple(abort_api, c); }
  static void shutdown(EGTcpClient* c) { simple(shutdown_api, c); }
  static void nodelay(EGTcpClient* c, bool on) { simple(nodelay_api, c, 0, on); }

  static void keepalive(EGTcpClient* c, uint32_t ms, uint8_t count) {
    Call m{};
    m.c = c;
    m.ms = ms;
    m.flags = count;
    on_stack(keepalive_api, m);
  }

  static void info(const EGTcpClient* c, EGTcpInfo* i) {
    Call m{};
    m.c = const_cast<EGTcpClient*>(c);
    m.info = i;
    on_stack(info_api, m);
  }

  // lock held from here down

  // frees the head, gives the next
  static pbuf* rx_pop(EGTcpClient* c) {
    pbuf* p = (pbuf*)c->_rx;
    c->_rx = p->next;
    p->next = nullptr;
    pbuf_free(p);
    c->_rx_off = 0;
    return (pbuf*)c->_rx;
  }

  static const uint8_t* rx_peek(EGTcpClient* c, size_t* len) {
    pbuf* p = (pbuf*)c->_rx;
    while (p && c->_rx_off >= p->len) p = rx_pop(c);   // empty ones
    if (!p) return nullptr;
    *len = p->len - c->_rx_off;
    return (const uint8_t*)p->payload + c->_rx_off;
  }

  static void rx_drop(EGTcpClient* c, size_t len) {
    pbuf* p = (pbuf*)c->_rx;
    while (len && p) {
      size_t n = p->len - c->_rx_off;
      if (n > len) n = len;
      c->_rx_off += n;
      len -= n;
      c->_rx_len.store(c->_rx_len.load(std::memory_order_relaxed) - n, std::memory_order_release);
      if (c->_rx_off >= p->len) p = rx_pop(c);
    }
  }

  static void rx_clear(EGTcpClient* c) {
    if (c->_rx) pbuf_free((pbuf*)c->_rx);   // whole chain
    c->_rx = nullptr;
    c->_rx_off = 0;
    c->_rx_len.store(0, std::memory_order_release);
  }

  static bool listen(EGTcpServer* s, const EGTcpAddr& a, uint16_t port, uint8_t backlog) {
    ip_addr_t ip;
    from_addr(a, &ip);
    Call m{};
    m.s = s;
    m.addr = a.any() && !a.v6 ? IP_ANY_TYPE : &ip;
    m.port = port;
    m.flags = backlog ? backlog : EGTCP_BACKLOG;
    m.err = ERR_VAL;
    return on_stack(listen_api, m) == ERR_OK;
  }

  static void unlisten(EGTcpServer* s) {
    Call m{};
    m.s = s;
    on_stack(unlisten_api, m);
  }
};

static const EGTcpOps s_lwip = {
  EGTcpLwip::connect, EGTcpLwip::resolve, EGTcpLwip::write, EGTcpLwip::recved,
  EGTcpLwip::close, EGTcpLwip::abort, EGTcpLwip::shutdown, EGTcpLwip::info,
  EGTcpLwip::nodelay, EGTcpLwip::keepalive,
  EGTcpLwip::rx_peek, EGTcpLwip::rx_drop, EGTcpLwip::rx_clear,
  EGTcpLwip::listen, EGTcpLwip::unlisten,
};

const EGTcpOps* const egtcp_ops = &s_lwip;
