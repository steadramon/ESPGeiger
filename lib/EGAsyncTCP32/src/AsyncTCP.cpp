// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_idf_version.h>
#if (ESP_IDF_VERSION_MAJOR >= 5)
#include <NetworkInterface.h>
#endif
#else
#include "esp_timer.h"
static unsigned long millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}
#endif

extern "C" {
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"   // tcp_active_pcbs / tcp_tw_pcbs
}
#include "lwip/priv/tcpip_priv.h"

// lwIP coarse ticks, 500 ms
#define ASYNC_TCP_POLL_TIMER 1

// events per poll(), the loop keeps going
#ifndef ASYNC_TCP_BATCH
#define ASYNC_TCP_BATCH 16
#endif

// closed, peer silent this long: abort, else lwIP retries for minutes
#ifndef EGASYNC_CLOSE_LINGER_S
#define EGASYNC_CLOSE_LINGER_S 180
#endif
#define EGASYNC_LINGER_POLL 10   // 500 ms ticks

// lwIP's arg till it answers, client cleared under the queue lock
struct dns_ask {
  AsyncClient *client;
  uint32_t id;
};
static uint32_t _dns_seq = 0;

class AsyncTCP_detail {
public:
  static void handle_async_event(lwip_tcp_event_packet_t *e);

  // lwIP callbacks, tiT
  static int8_t tcp_connected(void *arg, tcp_pcb *pcb, int8_t err);
  static int8_t tcp_recv(void *arg, tcp_pcb *pcb, pbuf *pb, int8_t err);
  static int8_t tcp_sent(void *arg, tcp_pcb *pcb, uint16_t len);
  static void tcp_error(void *arg, int8_t err);
  static int8_t tcp_poll(void *arg, tcp_pcb *pcb);
  static int8_t tcp_accept(void *arg, tcp_pcb *pcb, int8_t err);
  static void dns_found(const char *, const ip_addr_t *ipaddr, void *arg);
};

/*
 * Event queue: tiT pushes, the owner pops
 */

namespace {
static SemaphoreHandle_t _queue_mutex = nullptr;

class queue_mutex_guard {
  bool holds_mutex;

public:
  // mutex may not exist yet
  inline queue_mutex_guard() : holds_mutex(_queue_mutex ? xSemaphoreTake(_queue_mutex, portMAX_DELAY) : false){};
  inline ~queue_mutex_guard() {
    if (holds_mutex) {
      xSemaphoreGive(_queue_mutex);
    }
  };
};
}  // anonymous namespace

static SimpleIntrusiveList<lwip_tcp_event_packet_t> _async_queue;
std::atomic<bool> AsyncTCP::pending{false};

static TaskHandle_t _owner = nullptr;      // first to poll()
static AsyncClient *_running = nullptr;    // whose callback, ~AsyncClient clears it
static std::atomic<uint16_t> _client_count{0};   // leak checks

static bool _ready() {
  if (!_queue_mutex) _queue_mutex = xSemaphoreCreateMutex();
  return _queue_mutex != nullptr;
}

// once, a wrong task is a bug in the caller
static void _check_task() {
  static bool told = false;
  if (!_owner || told || xTaskGetCurrentTaskHandle() == _owner) return;
  told = true;
  async_tcp_log_e("AsyncClient used off the task that polls");
}

static uint32_t _xor_shift_state = 31;  // any nonzero seed
static uint32_t _xor_shift_next() {
  uint32_t x = _xor_shift_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return _xor_shift_state = x;
}

static void _free_event(lwip_tcp_event_packet_t *e) {
  if (e->event == LWIP_TCP_RELEASE) {
    return;   // lives in the client
  }
  if (e->event == LWIP_TCP_RECV && e->recv.pb) {
    pbuf_free(e->recv.pb);
  }
  delete e;
}

static size_t _free_chain(lwip_tcp_event_packet_t *chain) {
  size_t n = 0;
  while (chain) {
    auto t = chain;
    chain = t->next;
    _free_event(t);
    n++;
  }
  return n;
}

// queue lock held
static inline void _send_async_event(lwip_tcp_event_packet_t *e, bool front = false) {
  if (!e) return;
  if (front) {
    _async_queue.push_front(e);
  } else {
    _async_queue.push_back(e);
  }
  AsyncTCP::pending.store(true, std::memory_order_release);
}

static void _post(lwip_tcp_event_packet_t *e) {
  queue_mutex_guard guard;
  _send_async_event(e);
}

static lwip_tcp_event_packet_t *_new_event(lwip_tcp_event_t ev, AsyncClient *c) {
  auto *e = new (std::nothrow) lwip_tcp_event_packet_t{ev, c};
  if (!e) async_tcp_log_e("Failed to allocate event packet");
  return e;
}

static lwip_tcp_event_packet_t *_get_async_event() {
  queue_mutex_guard guard;
  while (1) {
    lwip_tcp_event_packet_t *e = _async_queue.pop_front();
    if (!e || e->event != LWIP_TCP_POLL) {
      return e;
    }
    // back to back polls for one client: keep one
    for (auto *n = _async_queue.begin(); n && n->client == e->client && n->event == LWIP_TCP_POLL; n = _async_queue.begin()) {
      _free_event(_async_queue.pop_front());
    }
    // queue over 3/4: drop polls, more likely the fuller it gets
    if (_async_queue.size() > (_xor_shift_next() % CONFIG_ASYNC_TCP_QUEUE_SIZE / 4 + CONFIG_ASYNC_TCP_QUEUE_SIZE * 3 / 4)) {
      _free_event(e);
      continue;
    }
    return e;
  }
}

// lock held; accepted: an unseen accept went; all: the release too
static lwip_tcp_event_packet_t *_purge_unlocked(AsyncClient *c, bool *accepted, bool all = false) {
  // a queued release still has to free it
  lwip_tcp_event_packet_t *chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &p) {
    return p.client == c && (all || p.event != LWIP_TCP_RELEASE);
  });
  for (auto *t = chain; t && accepted; t = t->next) {
    if (t->event == LWIP_TCP_ACCEPT) *accepted = true;
  }
  return chain;
}

static size_t _remove_events_for_client(AsyncClient *c, bool all = false) {
  lwip_tcp_event_packet_t *chain;
  {
    queue_mutex_guard guard;
    chain = _purge_unlocked(c, nullptr, all);
  }
  return _free_chain(chain);
}

// never handed to user code
static void _remove_events_for_server(AsyncServer *server) {
  lwip_tcp_event_packet_t *chain;
  {
    queue_mutex_guard guard;
    chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &p) {
      return p.event == LWIP_TCP_ACCEPT && p.accept.server == server;
    });
  }
  while (chain) {
    auto t = chain;
    chain = t->next;
    delete t->client;
    _free_event(t);
  }
}

void AsyncTCP_detail::handle_async_event(lwip_tcp_event_packet_t *e) {
  AsyncClient *c = e->client;
  if (e->event == LWIP_TCP_RELEASE) {
    delete c;   // the event goes with it
    return;
  }
  _running = c;
  switch (e->event) {
    case LWIP_TCP_RECV:
      c->_recv(e->recv.pb);
      e->recv.pb = nullptr;  // given to client
      break;
    case LWIP_TCP_FIN:       c->close(); break;
    case LWIP_TCP_SENT:      c->_sent(e->sent.len); break;
    case LWIP_TCP_POLL:      c->_poll(); break;
    case LWIP_TCP_ERROR:     c->_error(e->error.err); break;
    case LWIP_TCP_CONNECTED: c->_connected(); break;
    case LWIP_TCP_ACCEPT: {
      AsyncServer *s = e->accept.server;
      if (s->_connect_cb) s->_connect_cb(s->_connect_cb_arg, c);
      break;
    }
    case LWIP_TCP_DNS:
      if (e->dns.id == c->_dns_id) c->_dns_found(e->dns.found ? &e->dns.addr : nullptr);
      break;
    default: break;
  }
  _running = nullptr;
  _free_event(e);
}

void AsyncTCP::run() {
  static bool in_run = false;
  if (in_run) return;   // a callback polled
  in_run = true;
  if (!_owner) _owner = xTaskGetCurrentTaskHandle();
  // before the drain, a push after sets it again
  pending.store(false, std::memory_order_relaxed);
  for (uint8_t n = 0; n < ASYNC_TCP_BATCH; n++) {
    lwip_tcp_event_packet_t *e = _get_async_event();
    if (!e) {
      in_run = false;
      return;
    }
    AsyncTCP_detail::handle_async_event(e);
  }
  pending.store(true, std::memory_order_relaxed);   // more next pass
  in_run = false;
}

/*
 * lwIP callbacks, all on tiT: refresh the copy, queue an event
 */

static void _bind_tcp_callbacks(tcp_pcb *pcb, AsyncClient *c) {
  tcp_arg(pcb, c);
  tcp_recv(pcb, &AsyncTCP_detail::tcp_recv);
  tcp_sent(pcb, &AsyncTCP_detail::tcp_sent);
  tcp_err(pcb, &AsyncTCP_detail::tcp_error);
  tcp_poll(pcb, &AsyncTCP_detail::tcp_poll, ASYNC_TCP_POLL_TIMER);
}

static void _reset_tcp_callbacks(tcp_pcb *pcb) {
  tcp_arg(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
}

int8_t AsyncTCP_detail::tcp_connected(void *arg, tcp_pcb *pcb, int8_t) {
  auto *c = reinterpret_cast<AsyncClient *>(arg);
  if (!c) return ERR_OK;
  c->_cache(pcb);
  auto *e = _new_event(LWIP_TCP_CONNECTED, c);
  if (!e) return ERR_MEM;
  _post(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_poll(void *arg, tcp_pcb *pcb) {
  auto *c = reinterpret_cast<AsyncClient *>(arg);
  if (!c) return ERR_OK;
  c->_cache(pcb);   // backstop
  {
    // filling up, let the acks through
    queue_mutex_guard guard;
    if (_async_queue.size() > (_xor_shift_next() % CONFIG_ASYNC_TCP_QUEUE_SIZE / 2 + CONFIG_ASYNC_TCP_QUEUE_SIZE / 4)) {
      return ERR_OK;
    }
  }
  auto *e = _new_event(LWIP_TCP_POLL, c);
  if (!e) return ERR_MEM;
  _post(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_recv(void *arg, tcp_pcb *pcb, pbuf *pb, int8_t) {
  auto *c = reinterpret_cast<AsyncClient *>(arg);
  if (!c) {
    if (pb) pbuf_free(pb);
    return ERR_OK;
  }
  c->_cache(pcb);   // FIN moves the state
  auto *e = _new_event(pb ? LWIP_TCP_RECV : LWIP_TCP_FIN, c);
  if (!e) return ERR_MEM;   // lwIP keeps pb, tries again
  if (pb) e->recv.pb = pb;
  _post(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_sent(void *arg, tcp_pcb *pcb, uint16_t len) {
  auto *c = reinterpret_cast<AsyncClient *>(arg);
  if (!c) return ERR_OK;
  c->_cache(pcb);   // room grew
  auto *e = _new_event(LWIP_TCP_SENT, c);
  if (!e) return ERR_MEM;
  e->sent.len = len;
  _post(e);
  return ERR_OK;
}

// lwIP freed the pcb, nothing to clear
void AsyncTCP_detail::tcp_error(void *arg, int8_t err) {
  auto *c = reinterpret_cast<AsyncClient *>(arg);
  if (!c) return;
  lwip_tcp_event_packet_t *chain;
  {
    // one hold: ~AsyncClient purges under it after
    queue_mutex_guard guard;
    c->_own(nullptr);
    bool unseen = false;
    chain = _purge_unlocked(c, &unseen);
    if (unseen) {
      _send_async_event(&c->_release_ev);   // server never had it
    } else {
      auto *e = _new_event(LWIP_TCP_ERROR, c);
      if (e) e->error.err = err;
      _send_async_event(e);
    }
  }
  _free_chain(chain);
}

void AsyncTCP_detail::dns_found(const char *, const ip_addr_t *ipaddr, void *arg) {
  auto *ask = reinterpret_cast<dns_ask *>(arg);
  {
    queue_mutex_guard guard;
    AsyncClient *c = ask->client;   // null: gone or asked again
    if (c) {
      c->_ask = nullptr;
      auto *e = _new_event(LWIP_TCP_DNS, c);
      if (e) {
        e->dns.id = ask->id;
        e->dns.found = ipaddr != nullptr;
        if (ipaddr) {
          memcpy(&e->dns.addr, ipaddr, sizeof(ip_addr_t));
        } else {
          memset(&e->dns.addr, 0, sizeof(e->dns.addr));
        }
        _send_async_event(e);
      }
    }
  }
  delete ask;
}

// tmr = last heard
static err_t _linger_poll(void *, tcp_pcb *pcb) {
  if (tcp_ticks - pcb->tmr < EGASYNC_CLOSE_LINGER_S * 1000UL / TCP_SLOW_INTERVAL) return ERR_OK;
  tcp_abort(pcb);
  return ERR_ABRT;
}

// listening till end(), so the server is live
int8_t AsyncTCP_detail::tcp_accept(void *arg, tcp_pcb *pcb, int8_t) {
  if (!pcb) {
    async_tcp_log_e("_accept failed: pcb is NULL");
    return ERR_ABRT;
  }
  auto *server = reinterpret_cast<AsyncServer *>(arg);
  // event first, a failed client has nothing to undo
  lwip_tcp_event_packet_t *e = server->_connect_cb ? _new_event(LWIP_TCP_ACCEPT, nullptr) : nullptr;
  AsyncClient *c = e ? new (std::nothrow) AsyncClient(pcb) : nullptr;
  if (!c) {
    delete e;
    async_tcp_log_e("_accept failed");
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  if (server->_noDelay) {
    tcp_nagle_disable(pcb);
  } else {
    tcp_nagle_enable(pcb);
  }
  e->client = c;
  e->accept.server = server;
  // holds a backlog slot till closed
  if (server->_maxClients) tcp_backlog_delayed(pcb);
  queue_mutex_guard guard;
  _send_async_event(e, true);
  return ERR_OK;
}

/*
 * Calls into tiT. Each body reads c->pcb() there: tiT is its only writer,
 * so it is exact.
 */

// tiT side of the cold getters
struct pcb_info {
  ip_addr_t local_ip;
  ip_addr_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;
  uint16_t mss;
  bool nodelay;
  bool ok;   // had a pcb
};

struct tcp_call {
  struct tcpip_api_call_data call;
  AsyncClient *client;
  err_t err;
  union {
    struct {
      const char *buf;
      size_t size;
      uint8_t flags;
      bool now;       // tcp_output too
      size_t taken;
    } write;
    size_t received;
    struct {
      ip_addr_t *addr;
      uint16_t port;
    } connect;
    pcb_info *info;
    bool nodelay;
    struct {
      uint32_t ms;
      uint8_t cnt;
    } keepalive;
  };
};

typedef err_t (*tit_fn)(struct tcpip_api_call_data *);

static err_t _tit(tit_fn fn, tcp_call &m) {
  m.err = ERR_CONN;
  tcpip_api_call(fn, &m.call);
  return m.err;
}

static inline bool _pcb_in_active_lists(const tcp_pcb *pcb) {
  for (const tcp_pcb *p = tcp_active_pcbs; p; p = p->next) {
    if (p == pcb) return true;
  }
  for (const tcp_pcb *p = tcp_tw_pcbs; p; p = p->next) {
    if (p == pcb) return true;
  }
  return false;
}

// these trip lwIP write asserts
static inline bool _pcb_state_writable(const tcp_pcb *pcb) {
  switch (pcb->state) {
    case LISTEN: case CLOSED: case TIME_WAIT:
    case FIN_WAIT_2: case CLOSING: case LAST_ACK:
      return false;
    default:
      return true;
  }
}

// tiT: ours, listed and writable
static tcp_pcb *_live(AsyncClient *c) {
  tcp_pcb *pcb = c->pcb();
  if (!pcb || !_pcb_in_active_lists(pcb) || !_pcb_state_writable(pcb)) return nullptr;
  return pcb;
}

// clamps to room; queued counts as taken even if output fails
static err_t _write_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  m->write.taken = 0;
  tcp_pcb *pcb = _live(m->client);
  if (!pcb) return m->err;
  size_t n = m->write.size;
  if (n > tcp_sndbuf(pcb)) n = tcp_sndbuf(pcb);
  m->err = n ? tcp_write(pcb, m->write.buf, n, m->write.flags) : ERR_MEM;
  if (m->err == ERR_OK) {
    m->write.taken = n;
    if (m->write.now) tcp_output(pcb);   // lwIP retries unsent itself
  }
  m->client->_cache(pcb);
  return m->err;
}

static err_t _output_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  tcp_pcb *pcb = _live(m->client);
  if (!pcb) return m->err;
  m->err = tcp_output(pcb);
  m->client->_cache(pcb);
  return m->err;
}

static err_t _recved_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  tcp_pcb *pcb = _live(m->client);
  if (!pcb) return m->err;
  // lwIP asserts past the window, rcv_wnd can pass max
  size_t len = m->received;
  if (pcb->rcv_wnd >= TCP_WND_MAX(pcb)) {
    len = 0;
  } else {
    tcpwnd_size_t headroom = (tcpwnd_size_t)(TCP_WND_MAX(pcb) - pcb->rcv_wnd);
    if (len > (size_t)headroom) len = (size_t)headroom;
  }
  if (len) tcp_recved(pcb, len);
  m->err = ERR_OK;
  return m->err;
}

// never fails: a pcb that won't close is aborted
static err_t _close_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  AsyncClient *c = m->client;
  tcp_pcb *pcb = c->pcb();
  if (pcb) {
    c->_own(nullptr);   // lwIP's now
    _reset_tcp_callbacks(pcb);
    tcp_backlog_accepted(pcb);   // frees the backlog slot
    tcp_poll(pcb, &_linger_poll, EGASYNC_LINGER_POLL);   // before tcp_close, it can free the pcb
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    m->err = ERR_OK;
  }
  // an error queued: its dispose still runs
  if (_remove_events_for_client(c)) m->err = ERR_OK;
  return m->err;
}

// errf runs, tcp_error tells the owner
static err_t _abort_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  tcp_pcb *pcb = m->client->pcb();
  if (!pcb) return m->err;
  tcp_abort(pcb);
  m->client->_own(nullptr);
  m->err = ERR_ABRT;
  return m->err;
}

// new pcb, callbacks, dial: one trip, nothing left on a fail
static err_t _connect_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  AsyncClient *c = m->client;
#if LWIP_IPV4 && LWIP_IPV6
  tcp_pcb *pcb = tcp_new_ip_type(m->connect.addr->type);
#else
  tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
#endif
  if (!pcb) {
    m->err = ERR_MEM;
    return m->err;
  }
  _bind_tcp_callbacks(pcb, c);
  c->_own(pcb);
  m->err = tcp_connect(pcb, m->connect.addr, m->connect.port, &AsyncTCP_detail::tcp_connected);
  if (m->err != ERR_OK && c->pcb() == pcb) {
    c->_own(nullptr);
    _reset_tcp_callbacks(pcb);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
  }
  return m->err;
}

static err_t _info_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  pcb_info *i = m->info;
  memset(i, 0, sizeof(*i));
  tcp_pcb *pcb = m->client->pcb();
  if (!pcb) return m->err;
  ip_addr_copy(i->local_ip, pcb->local_ip);
  ip_addr_copy(i->remote_ip, pcb->remote_ip);
  i->local_port = pcb->local_port;
  i->remote_port = pcb->remote_port;
  i->mss = tcp_mss(pcb);
  i->nodelay = tcp_nagle_disabled(pcb);
  i->ok = true;
  m->err = ERR_OK;
  return m->err;
}

static err_t _nodelay_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  tcp_pcb *pcb = m->client->pcb();
  if (!pcb) return m->err;
  if (m->nodelay) {
    tcp_nagle_disable(pcb);
  } else {
    tcp_nagle_enable(pcb);
  }
  m->err = ERR_OK;
  return m->err;
}

static err_t _keepalive_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_call *>(d);
  tcp_pcb *pcb = m->client->pcb();
  if (!pcb) return m->err;
  if (m->keepalive.ms) {
    pcb->so_options |= SOF_KEEPALIVE;
    pcb->keep_idle = m->keepalive.ms;
    pcb->keep_intvl = m->keepalive.ms;
    pcb->keep_cnt = m->keepalive.cnt;
  } else {
    pcb->so_options &= ~SOF_KEEPALIVE;
  }
  m->err = ERR_OK;
  return m->err;
}

static size_t _write(AsyncClient *c, const char *data, size_t size, uint8_t flags, bool now) {
  tcp_call m{};
  m.client = c;
  m.write.buf = data;
  m.write.size = size;
  m.write.flags = flags;
  m.write.now = now;
  _tit(_write_api, m);
  return m.write.taken;
}

static void _recved(AsyncClient *c, size_t len) {
  tcp_call m{};
  m.client = c;
  m.received = len;
  _tit(_recved_api, m);
}

static void _info(const AsyncClient *c, pcb_info *i) {
  tcp_call m{};
  m.client = const_cast<AsyncClient *>(c);
  m.info = i;
  _tit(_info_api, m);
}

// lwIP thread owns the DNS cache
struct tcp_dns_call {
  struct tcpip_api_call_data call;
  const char *host;
  ip_addr_t *addr;
  dns_ask *ask;
  err_t err;
};

static err_t _dns_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_dns_call *>(d);
  m->err = dns_gethostbyname(m->host, m->addr, &AsyncTCP_detail::dns_found, m->ask);
  return ERR_OK;
}

// listen pcb: made, armed or closed on tiT
struct tcp_server_call {
  struct tcpip_api_call_data call;
  AsyncServer *server;
  ip_addr_t *addr;
  uint16_t port;
  uint8_t backlog;
  tcp_pcb *pcb;   // out for begin, in for close
  err_t err;
};

static err_t _server_begin_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_server_call *>(d);
  m->pcb = nullptr;
#if LWIP_IPV4 && LWIP_IPV6
  tcp_pcb *pcb = tcp_new_ip_type(m->addr->type);
#else
  tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
#endif
  if (!pcb) {
    m->err = ERR_MEM;
    return m->err;
  }
  ip_set_option(pcb, SOF_REUSEADDR);   // rebind with clients in TIME_WAIT
  m->err = tcp_bind(pcb, m->addr, m->port);
  tcp_pcb *lpcb = m->err == ERR_OK ? tcp_listen_with_backlog(pcb, m->backlog) : nullptr;
  if (!lpcb) {   // a failed listen keeps pcb
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    if (m->err == ERR_OK) m->err = ERR_MEM;
    return m->err;
  }
  tcp_arg(lpcb, m->server);
  tcp_accept(lpcb, &AsyncTCP_detail::tcp_accept);
  m->pcb = lpcb;
  return m->err;
}

static err_t _server_close_api(struct tcpip_api_call_data *d) {
  auto *m = reinterpret_cast<tcp_server_call *>(d);
  tcp_arg(m->pcb, NULL);
  tcp_accept(m->pcb, NULL);
  if (tcp_close(m->pcb) != ERR_OK) tcp_abort(m->pcb);
  m->err = ERR_OK;
  return m->err;
}

/*
 * AsyncClient, owner's task
 */

// tiT
void AsyncClient::_cache(tcp_pcb *pcb) {
  _snd.store(pcb ? tcp_sndbuf(pcb) : 0, std::memory_order_relaxed);
  _st.store(pcb ? (uint8_t)pcb->state : (uint8_t)CLOSED, std::memory_order_release);
}

// tiT
void AsyncClient::_own(tcp_pcb *pcb) {
  _pcb.store(pcb);
  _cache(pcb);
}

AsyncClient::AsyncClient(tcp_pcb *pcb) {
  _client_count++;
  if (pcb) {   // tiT, from accept: direct lwIP calls only
    _own(pcb);
    _rx_last_packet = millis();
    _bind_tcp_callbacks(pcb, this);
  }
}

AsyncClient::~AsyncClient() {
  if (_running == this) _running = nullptr;   // its callback stops here
  _released = true;   // no onDisconnect from a delete
  if (_pcb.load()) {
    _close();
  }
  {
    queue_mutex_guard guard;
    if (_ask) _ask->client = nullptr;   // answer goes nowhere
  }
  _remove_events_for_client(this, true);
  _client_count--;
}

bool AsyncClient::operator==(const AsyncClient &other) const {
  return _pcb.load() == other._pcb.load();
}

void AsyncClient::onConnect(AcConnectHandler cb, void *arg) {
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

void AsyncClient::onDisconnect(AcConnectHandler cb, void *arg) {
  _discard_cb = cb;
  _discard_cb_arg = arg;
}

void AsyncClient::onAck(AcAckHandler cb, void *arg) {
  _sent_cb = cb;
  _sent_cb_arg = arg;
}

void AsyncClient::onError(AcErrorHandler cb, void *arg) {
  _error_cb = cb;
  _error_cb_arg = arg;
}

void AsyncClient::onData(AcDataHandler cb, void *arg) {
  _recv_cb = cb;
  _recv_cb_arg = arg;
}

void AsyncClient::onPacket(AcPacketHandler cb, void *arg) {
  _pb_cb = cb;
  _pb_cb_arg = arg;
}

void AsyncClient::onTimeout(AcTimeoutHandler cb, void *arg) {
  _timeout_cb = cb;
  _timeout_cb_arg = arg;
}

void AsyncClient::onPoll(AcConnectHandler cb, void *arg) {
  _poll_cb = cb;
  _poll_cb_arg = arg;
}

bool AsyncClient::connect(ip_addr_t addr, uint16_t port) {
  _check_task();
  if (_pcb.load()) {
    async_tcp_log_d("already connected, state %d", state());
    return false;
  }
  _closed = false;
  _rx_ack_len = 0;
  return _connect_to(addr, port);
}

// DNS answer too
bool AsyncClient::_connect_to(ip_addr_t addr, uint16_t port) {
  if (!_ready()) {
    return false;
  }
  tcp_call m{};
  m.client = this;
  m.connect.addr = &addr;
  m.connect.port = port;
  return _tit(_connect_api, m) == ERR_OK;   // a fail leaves no pcb, no callback
}

#ifdef ARDUINO
bool AsyncClient::connect(const IPAddress &ip, uint16_t port) {
  ip_addr_t addr;
#if ESP_IDF_VERSION_MAJOR < 5
#if LWIP_IPV4 && LWIP_IPV6
  addr.u_addr.ip4.addr = ip;
  addr.type = IPADDR_TYPE_V4;
#else
  addr.addr = ip;
#endif
#else
  ip.to_ip_addr_t(&addr);
#endif
  return connect(addr, port);
}
#endif

#if LWIP_IPV6 && ESP_IDF_VERSION_MAJOR < 5
bool AsyncClient::connect(const IPv6Address &ip, uint16_t port) {
  auto ipaddr = static_cast<const uint32_t *>(ip);
  ip_addr_t addr = IPADDR6_INIT(ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
  return connect(addr, port);
}
#endif

bool AsyncClient::connect(const char *host, uint16_t port) {
  _check_task();
  if (_pcb.load() || !_ready()) {
    return false;
  }
  _closed = false;
  _rx_ack_len = 0;
  dns_ask *ask = new (std::nothrow) dns_ask{this, ++_dns_seq};
  if (!ask) {
    return false;
  }
  {
    // an older ask answers nobody
    queue_mutex_guard guard;
    if (_ask) _ask->client = nullptr;
    _ask = ask;
    _dns_id = ask->id;
  }
  _connect_port = port;
  ip_addr_t addr;
  tcp_dns_call m;
  m.host = host;
  m.addr = &addr;
  m.ask = ask;
  m.err = ERR_VAL;
  tcpip_api_call(_dns_api, &m.call);
  if (m.err != ERR_INPROGRESS) {
    // cached or failed, lwIP never calls back
    queue_mutex_guard guard;
    _ask = nullptr;
    delete ask;
  }
  if (m.err == ERR_OK) {
    return connect(addr, port);
  }
  if (m.err == ERR_INPROGRESS) {
    return true;
  }
  async_tcp_log_d("error: %d", m.err);
  return false;
}

void AsyncClient::close() {
  _check_task();
  _closed = true;
  if (_pcb.load()) {
    _recved(this, _rx_ack_len);
  }
  _close();
}

void AsyncClient::release() {
  _check_task();
  delete this;
}

int8_t AsyncClient::abort() {
  _check_task();
  tcp_call m{};
  m.client = this;
  return _tit(_abort_api, m);
}

size_t AsyncClient::space() const {
  return connected() ? _snd.load(std::memory_order_relaxed) : 0;
}

size_t AsyncClient::add(const char *data, size_t size, uint8_t apiflags) {
  if (!data || !size || !space()) {
    return 0;   // full costs no trip
  }
  _check_task();
  return _write(this, data, size, apiflags, false);
}

bool AsyncClient::send() {
  // before, the ack can beat us back
  uint32_t was = _tx_last_packet;
  _tx_last_packet = millis();
  tcp_call m{};
  m.client = this;
  if (_tit(_output_api, m) == ERR_OK) {
    return true;
  }
  _tx_last_packet = was;
  return false;
}

// queued is taken, lwIP sends it
size_t AsyncClient::write(const char *data, size_t size, uint8_t apiflags) {
  if (!data || !size || !space()) {
    return 0;
  }
  _check_task();
  uint32_t was = _tx_last_packet;
  _tx_last_packet = millis();
  size_t n = _write(this, data, size, apiflags, true);
  if (!n) _tx_last_packet = was;
  return n;
}

size_t AsyncClient::ack(size_t len) {
  if (len > _rx_ack_len) len = _rx_ack_len;
  _rx_ack_len -= len;
  if (len && _pcb.load()) {
    _recved(this, len);
  }
  return len;
}

void AsyncClient::ackPacket(struct pbuf *pb) {
  if (!pb) {
    return;
  }
  if (_pcb.load()) _recved(this, pb->len);
  pbuf_free(pb);
}

int8_t AsyncClient::_close() {
  tcp_call m{};
  m.client = this;
  int8_t err = _tit(_close_api, m);
  if (err == ERR_OK && _discard_cb && !_released) {
    _told = true;
    _discard_cb(_discard_cb_arg, this);
  }
  return err;
}

void AsyncClient::_connected() {
  if (!_pcb.load()) {
    return;   // error took it
  }
  _rx_last_packet = millis();
  _tx_last_packet = 0;
  _rx_last_ack = 0;
  if (_connect_cb) {
    _connect_cb(_connect_cb_arg, this);
  }
}

// close() in onError may have told
void AsyncClient::_error(int8_t err) {
  _told = false;
  if (_error_cb) {
    _error_cb(_error_cb_arg, this, err);
  }
  if (_running == this && _discard_cb && !_told) {
    _discard_cb(_discard_cb_arg, this);
  }
}

void AsyncClient::_sent(uint16_t len) {
  _rx_last_ack = _rx_last_packet = millis();
  if (_sent_cb) {
    _sent_cb(_sent_cb_arg, this, len, _rx_last_packet - _tx_last_packet);
  }
}

static void _free_pbufs(pbuf *pb) {
  while (pb) {
    pbuf *b = pb;
    pb = b->next;
    b->next = NULL;
    pbuf_free(b);
  }
}

void AsyncClient::_recv(pbuf *pb) {
  if (_closed) {
    _free_pbufs(pb);
    return;
  }
  while (pb) {
    _rx_last_packet = millis();
    // no ack before the data is taken
    _ack_pcb = true;
    pbuf *b = pb;
    pb = b->next;
    b->next = NULL;
    if (_pb_cb) {
      _pb_cb(_pb_cb_arg, this, b);
      b = NULL;
    } else {
      // credit before onData
      _rx_ack_len += b->len;
      if (_recv_cb) {
        _recv_cb(_recv_cb_arg, this, b->payload, b->len);
      }
    }
    // callback may have deleted or closed this
    if (_running != this || _closed) {
      if (b) pbuf_free(b);
      _free_pbufs(pb);
      return;
    }
    if (b) {
      if (_ack_pcb) {
        ack(b->len);
      }
      pbuf_free(b);
    }
  }
}

void AsyncClient::_poll() {
  if (!_pcb.load()) {
    return;
  }
  uint32_t now = millis();
  // ACK timeout
  if (_ack_timeout) {
    const uint32_t one_day = 86400000;
    bool last_tx_is_after_last_ack = (_rx_last_ack - _tx_last_packet + one_day) < one_day;
    if (last_tx_is_after_last_ack && (now - _tx_last_packet) >= _ack_timeout) {
      if (_timeout_cb) {
        _timeout_cb(_timeout_cb_arg, this, now - _tx_last_packet);
      }
      return;
    }
  }
  // RX timeout
  if (_rx_timeout && (now - _rx_last_packet) >= (_rx_timeout * 1000)) {
    _close();
    return;
  }
  if (_poll_cb) {
    _poll_cb(_poll_cb_arg, this);
  }
}

void AsyncClient::_dns_found(ip_addr_t *ipaddr) {
  if (_closed || _pcb.load()) {
    return;
  }
  if (ipaddr && _connect_to(*ipaddr, _connect_port)) {
    return;
  }
  // tell the owner, else it waits forever
  if (_error_cb) {
    _error_cb(_error_cb_arg, this, ipaddr ? ERR_CONN : -55);
  }
  if (_running == this && _discard_cb) {
    _discard_cb(_discard_cb_arg, this);
  }
}

bool AsyncClient::free() {
  return freeable();
}

void AsyncClient::setRxTimeout(uint32_t timeout) {
  _rx_timeout = timeout;
}

uint32_t AsyncClient::getRxTimeout() const {
  return _rx_timeout;
}

uint32_t AsyncClient::getAckTimeout() const {
  return _ack_timeout;
}

void AsyncClient::setAckTimeout(uint32_t timeout) {
  _ack_timeout = timeout;
}

void AsyncClient::setNoDelay(bool nodelay) const {
  tcp_call m{};
  m.client = const_cast<AsyncClient *>(this);
  m.nodelay = nodelay;
  _tit(_nodelay_api, m);
}

bool AsyncClient::getNoDelay() {
  pcb_info i;
  _info(this, &i);
  return i.nodelay;
}

void AsyncClient::setKeepAlive(uint32_t ms, uint8_t cnt) {
  tcp_call m{};
  m.client = this;
  m.keepalive.ms = ms;
  m.keepalive.cnt = cnt;
  _tit(_keepalive_api, m);
}

uint16_t AsyncClient::getMss() const {
  pcb_info i;
  _info(this, &i);
  return i.mss;
}

static uint32_t _ip4_u32(const ip_addr_t &a) {
#if LWIP_IPV4 && LWIP_IPV6
  return a.u_addr.ip4.addr;
#else
  return a.addr;
#endif
}

static ip4_addr_t _ip4(const ip_addr_t &a) {
#if LWIP_IPV4 && LWIP_IPV6
  if (a.type == IPADDR_TYPE_V4) return a.u_addr.ip4;
  ip4_addr_t z;
  ip4_addr_set_zero(&z);
  return z;
#else
  return a;
#endif
}

uint32_t AsyncClient::getRemoteAddress() const {
  pcb_info i;
  _info(this, &i);
  return _ip4_u32(i.remote_ip);
}

uint16_t AsyncClient::getRemotePort() const {
  pcb_info i;
  _info(this, &i);
  return i.remote_port;
}

uint32_t AsyncClient::getLocalAddress() const {
  pcb_info i;
  _info(this, &i);
  return _ip4_u32(i.local_ip);
}

uint16_t AsyncClient::getLocalPort() const {
  pcb_info i;
  _info(this, &i);
  return i.local_port;
}

ip4_addr_t AsyncClient::getRemoteAddress4() const {
  pcb_info i;
  _info(this, &i);
  return _ip4(i.remote_ip);
}

ip4_addr_t AsyncClient::getLocalAddress4() const {
  pcb_info i;
  _info(this, &i);
  return _ip4(i.local_ip);
}

#if LWIP_IPV6
static ip6_addr_t _ip6(const ip_addr_t &a) {
  if (a.type == IPADDR_TYPE_V6) return a.u_addr.ip6;
  ip6_addr_t z;
  ip6_addr_set_zero(&z);
  return z;
}

ip6_addr_t AsyncClient::getRemoteAddress6() const {
  pcb_info i;
  _info(this, &i);
  return _ip6(i.remote_ip);
}

ip6_addr_t AsyncClient::getLocalAddress6() const {
  pcb_info i;
  _info(this, &i);
  return _ip6(i.local_ip);
}

#ifdef ARDUINO
#if ESP_IDF_VERSION_MAJOR < 5
IPv6Address AsyncClient::remoteIP6() const {
  return IPv6Address(getRemoteAddress6().addr);
}

IPv6Address AsyncClient::localIP6() const {
  return IPv6Address(getLocalAddress6().addr);
}
#else
IPAddress AsyncClient::remoteIP6() const {
  pcb_info i;
  _info(this, &i);
  if (!i.ok) return IPAddress(IPType::IPv6);
  IPAddress ip;
  ip.from_ip_addr_t(&i.remote_ip);
  return ip;
}

IPAddress AsyncClient::localIP6() const {
  pcb_info i;
  _info(this, &i);
  if (!i.ok) return IPAddress(IPType::IPv6);
  IPAddress ip;
  ip.from_ip_addr_t(&i.local_ip);
  return ip;
}
#endif
#endif
#endif

#ifdef ARDUINO
IPAddress AsyncClient::remoteIP() const {
#if ESP_IDF_VERSION_MAJOR < 5
  return IPAddress(getRemoteAddress());
#else
  pcb_info i;
  _info(this, &i);
  if (!i.ok) return IPAddress();
  IPAddress ip;
  ip.from_ip_addr_t(&i.remote_ip);
  return ip;
#endif
}

IPAddress AsyncClient::localIP() const {
#if ESP_IDF_VERSION_MAJOR < 5
  return IPAddress(getLocalAddress());
#else
  pcb_info i;
  _info(this, &i);
  if (!i.ok) return IPAddress();
  IPAddress ip;
  ip.from_ip_addr_t(&i.local_ip);
  return ip;
#endif
}
#endif

uint8_t AsyncClient::state() const {
  return _st.load(std::memory_order_acquire);
}

bool AsyncClient::connected() const {
  return state() == ESTABLISHED;
}

bool AsyncClient::connecting() const {
  uint8_t s = state();
  return s > CLOSED && s < ESTABLISHED;
}

bool AsyncClient::disconnecting() const {
  uint8_t s = state();
  return s > ESTABLISHED && s < TIME_WAIT;
}

bool AsyncClient::disconnected() const {
  uint8_t s = state();
  return s == CLOSED || s == TIME_WAIT;
}

bool AsyncClient::freeable() const {
  uint8_t s = state();
  return s == CLOSED || s > ESTABLISHED;
}

bool AsyncClient::canSend() const {
  return space() > 0;
}

/*
 * AsyncServer, owner's task
 */

AsyncServer::AsyncServer(ip_addr_t addr, uint16_t port) : _port(port), _addr(addr) {}

#ifdef ARDUINO
AsyncServer::AsyncServer(IPAddress addr, uint16_t port) : _port(port) {
#if ESP_IDF_VERSION_MAJOR < 5
#if LWIP_IPV4 && LWIP_IPV6
  _addr.type = IPADDR_TYPE_V4;
  _addr.u_addr.ip4.addr = addr;
#else
  _addr.addr = addr;
#endif
#else
  addr.to_ip_addr_t(&_addr);
#endif
}

#if ESP_IDF_VERSION_MAJOR < 5 && __has_include(<IPv6Address.h>) && LWIP_IPV6
AsyncServer::AsyncServer(IPv6Address addr, uint16_t port) : _port(port) {
  auto ipaddr = static_cast<const uint32_t *>(addr);
  _addr = IPADDR6_INIT(ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
}
#endif
#endif

AsyncServer::AsyncServer(uint16_t port) : _port(port) {
#if LWIP_IPV4 && LWIP_IPV6
  _addr.type = IPADDR_TYPE_ANY;
  _addr.u_addr.ip4.addr = INADDR_ANY;
#else
  _addr.addr = INADDR_ANY;
#endif
}

AsyncServer::~AsyncServer() {
  end();
}

void AsyncServer::onClient(AcConnectHandler cb, void *arg) {
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

void AsyncServer::begin() {
  if (_pcb || !_ready()) {
    return;
  }
  tcp_server_call m{};
  m.server = this;
  m.addr = &_addr;
  m.port = _port;
  m.backlog = _maxClients ? _maxClients : 5;
  tcpip_api_call(_server_begin_api, &m.call);
  _pcb = m.pcb;
  if (!_pcb) {
    async_tcp_log_e("listen error: %d", m.err);
  }
}

// no accept after the close, then none queued
void AsyncServer::end() {
  if (_pcb) {
    tcp_server_call m{};
    m.pcb = _pcb;
    tcpip_api_call(_server_close_api, &m.call);
    _pcb = nullptr;
  }
  _remove_events_for_server(this);
}

void AsyncServer::setNoDelay(bool nodelay) {
  _noDelay = nodelay;
}

bool AsyncServer::getNoDelay() const {
  return _noDelay;
}

uint8_t AsyncServer::status() const {
  return _pcb ? LISTEN : CLOSED;
}
