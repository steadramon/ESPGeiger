// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"

/**
 * LibreTiny specific configurations
 */
#if defined(LIBRETINY)
#include <Arduino.h>
// LibreTiny does not support IDF - disable code that expects it to be available
#define ESP_IDF_VERSION_MAJOR (0)
// xTaskCreatePinnedToCore is not available, force single-core operation
#define CONFIG_FREERTOS_UNICORE 1
// ESP watchdog is not available
#undef CONFIG_ASYNC_TCP_USE_WDT
#define CONFIG_ASYNC_TCP_USE_WDT 0
#endif  // LIBRETINY

/**
 * Arduino specific configurations
 */
#if defined(ARDUINO) && !defined(LIBRETINY)
#include <Arduino.h>
#include <esp_idf_version.h>
#if (ESP_IDF_VERSION_MAJOR >= 5)
#include <NetworkInterface.h>
#endif  // ESP_IDF_VERSION_MAJOR
#endif  // ARDUINO

/**
 * ESP-IDF specific configurations
 */
#if !defined(LIBRETINY) && !defined(ARDUINO)
#include "esp_timer.h"
static unsigned long millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}
#endif  // !LIBRETINY && !ARDUINO

extern "C" {
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"   // tcp_active_pcbs / tcp_tw_pcbs
}

// Required for:
// https://github.com/espressif/arduino-esp32/blob/3.0.3/libraries/Network/src/NetworkInterface.cpp#L37-L47

#if CONFIG_ASYNC_TCP_USE_WDT
#include "esp_task_wdt.h"
#define ASYNC_TCP_MAX_TASK_SLEEP (pdMS_TO_TICKS(1000 * CONFIG_ESP_TASK_WDT_TIMEOUT_S) / 4)
#else
#define ASYNC_TCP_MAX_TASK_SLEEP portMAX_DELAY
#endif

// https://github.com/espressif/arduino-esp32/issues/10526
namespace {
#ifdef CONFIG_LWIP_TCPIP_CORE_LOCKING
struct tcp_core_guard {
  bool do_lock;
  inline tcp_core_guard() : do_lock(!sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) {
    if (do_lock) {
      LOCK_TCPIP_CORE();
    }
  }
  inline ~tcp_core_guard() {
    if (do_lock) {
      UNLOCK_TCPIP_CORE();
    }
  }
  tcp_core_guard(const tcp_core_guard &) = delete;
  tcp_core_guard(tcp_core_guard &&) = delete;
  tcp_core_guard &operator=(const tcp_core_guard &) = delete;
  tcp_core_guard &operator=(tcp_core_guard &&) = delete;
} __attribute__((unused));
#else   // CONFIG_LWIP_TCPIP_CORE_LOCKING
struct tcp_core_guard {
} __attribute__((unused));
#endif  // CONFIG_LWIP_TCPIP_CORE_LOCKING
}  // anonymous namespace

/*
  TCP poll interval is specified in terms of the TCP coarse timer interval, which is called twice a second
  https://github.com/espressif/esp-lwip/blob/2acf959a2bb559313cd2bf9306c24612ba3d0e19/src/core/tcp.c#L1895
*/
#define CONFIG_ASYNC_TCP_POLL_TIMER 1

// events per pass, one-core only
#ifndef ASYNC_TCP_BATCH
#define ASYNC_TCP_BATCH 16
#endif

/*
 * TCP/IP Event Task
 * */

typedef enum {
  LWIP_TCP_SENT,
  LWIP_TCP_RECV,
  LWIP_TCP_FIN,
  LWIP_TCP_ERROR,
  LWIP_TCP_POLL,
  LWIP_TCP_ACCEPT,
  LWIP_TCP_CONNECTED,
  LWIP_TCP_DNS,
  LWIP_TCP_CLOSE,     // from another task
  LWIP_TCP_RELEASE    // from another task, frees it
} lwip_tcp_event_t;

struct lwip_tcp_event_packet_t {
  lwip_tcp_event_packet_t *next;
  lwip_tcp_event_t event;
  AsyncClient *client;
  union {
    struct {
      tcp_pcb *pcb;
      int8_t err;
    } connected;
    struct {
      int8_t err;
    } error;
    struct {
      tcp_pcb *pcb;
      uint16_t len;
    } sent;
    struct {
      tcp_pcb *pcb;
      pbuf *pb;
      int8_t err;
    } recv;
    struct {
      tcp_pcb *pcb;
      int8_t err;
    } fin;
    struct {
      tcp_pcb *pcb;
    } poll;
    struct {
      AsyncServer *server;
    } accept;
    struct {
      ip_addr_t addr;
      uint32_t id;
      bool found;    // false = failed
    } dns;
    struct {
      uint8_t gen;   // a reconnect makes it stale
    } close;
  };

  inline lwip_tcp_event_packet_t(lwip_tcp_event_t _event, AsyncClient *_client) : next(nullptr), event(_event), client(_client){};
};

// lwIP's arg till it answers
struct dns_ask {
  AsyncClient *client;
  uint32_t id;
};
static std::atomic<uint32_t> _dns_seq{0};

// Detail class for interacting with AsyncClient internals, but without exposing the API
class AsyncTCP_detail {
public:
  // Helper functions
  static void __attribute__((visibility("internal"))) handle_async_event(lwip_tcp_event_packet_t *event);
  static bool __attribute__((visibility("internal"))) owner_event(lwip_tcp_event_packet_t *event);

  // LwIP TCP event callbacks that (will) require privileged access
  static int8_t __attribute__((visibility("internal"))) tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pb, int8_t err);
  static int8_t __attribute__((visibility("internal"))) tcp_sent(void *arg, struct tcp_pcb *pcb, uint16_t len);
  static void __attribute__((visibility("internal"))) tcp_error(void *arg, int8_t err);
  static int8_t __attribute__((visibility("internal"))) tcp_poll(void *arg, struct tcp_pcb *pcb);
  static int8_t __attribute__((visibility("internal"))) tcp_accept(void *arg, tcp_pcb *pcb, int8_t err);
};

// Guard class for the global queue
namespace {

static SemaphoreHandle_t _async_queue_mutex = nullptr;

class queue_mutex_guard {
  bool holds_mutex;

public:
  // mutex may not exist yet
  inline queue_mutex_guard() : holds_mutex(_async_queue_mutex ? xSemaphoreTake(_async_queue_mutex, portMAX_DELAY) : false){};
  inline ~queue_mutex_guard() {
    if (holds_mutex) {
      xSemaphoreGive(_async_queue_mutex);
    }
  };
  inline explicit operator bool() const {
    return holds_mutex;
  };
};
}  // anonymous namespace

static SimpleIntrusiveList<lwip_tcp_event_packet_t> _async_queue;
static TaskHandle_t _async_service_task_handle = NULL;

// dispatch vs ~AsyncClient, recursive for close() in callbacks
static StaticSemaphore_t _lifetime_mtx_storage;
static SemaphoreHandle_t _lifetime_mtx = nullptr;

static inline void _lifetime_mtx_init() {
  if (!_lifetime_mtx) {
    _lifetime_mtx = xSemaphoreCreateRecursiveMutexStatic(&_lifetime_mtx_storage);
  }
}

class lifetime_guard {
public:
  inline lifetime_guard() {
    if (_lifetime_mtx) xSemaphoreTakeRecursive(_lifetime_mtx, portMAX_DELAY);
  }
  inline ~lifetime_guard() {
    if (_lifetime_mtx) xSemaphoreGiveRecursive(_lifetime_mtx);
  }
};

#define ASYNC_SERVER_REG_MAX 8
static AsyncServer *_live_servers[ASYNC_SERVER_REG_MAX] = {};
static uint8_t _live_server_count = 0;

static void _register_server(AsyncServer *s) {
  queue_mutex_guard guard;
  if (_live_server_count < ASYNC_SERVER_REG_MAX) {
    _live_servers[_live_server_count++] = s;
  }
}

static void _unregister_server(AsyncServer *s) {
  queue_mutex_guard guard;
  for (uint8_t i = 0; i < _live_server_count; i++) {
    if (_live_servers[i] == s) {
      _live_servers[i] = _live_servers[--_live_server_count];
      _live_servers[_live_server_count] = nullptr;
      return;
    }
  }
}

static bool _server_alive(const AsyncServer *s) {
  queue_mutex_guard guard;
  for (uint8_t i = 0; i < _live_server_count; i++) {
    if (_live_servers[i] == s) return true;
  }
  return false;
}

// must hold lifetime lock
#define ASYNC_CLIENT_REG_MAX 32
static_assert(ASYNC_CLIENT_REG_MAX >= 2 * MEMP_NUM_TCP_PCB,
              "client registry too small for MEMP_NUM_TCP_PCB: UAF guard would fail open");
static AsyncClient *_live_clients[ASYNC_CLIENT_REG_MAX] = {};
static uint8_t _live_client_count = 0;
static uint8_t _untracked_clients = 0;  // overflow, fails open

static bool _client_alive_unlocked(const AsyncClient *c) {
  for (uint8_t i = 0; i < _live_client_count; i++) {
    if (_live_clients[i] == c) return true;
  }
  return _untracked_clients > 0;
}

static bool _client_alive(const AsyncClient *c) {
  queue_mutex_guard guard;
  return _client_alive_unlocked(c);
}

static void _register_client(AsyncClient *c) {
  queue_mutex_guard guard;
  if (_live_client_count < ASYNC_CLIENT_REG_MAX) {
    _live_clients[_live_client_count++] = c;
  } else {
    ++_untracked_clients;
    async_tcp_log_e("client registry full; UAF guard degraded");
  }
}

static void _unregister_client(AsyncClient *c) {
  queue_mutex_guard guard;
  for (uint8_t i = 0; i < _live_client_count; i++) {
    if (_live_clients[i] == c) {
      _live_clients[i] = _live_clients[--_live_client_count];
      _live_clients[_live_client_count] = nullptr;
      return;
    }
  }
  if (_untracked_clients) {
    --_untracked_clients;
  }
}

static uint32_t _xor_shift_state = 31;  // any nonzero seed will do
static uint32_t _xor_shift_next() {
  uint32_t x = _xor_shift_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return _xor_shift_state = x;
}

static void _free_event(lwip_tcp_event_packet_t *evpkt) {
  if ((evpkt->event == LWIP_TCP_RECV) && (evpkt->recv.pb != nullptr)) {
    pbuf_free(evpkt->recv.pb);
  }
  delete evpkt;
}

static inline void _send_async_event(lwip_tcp_event_packet_t *e) {
  if (e == nullptr) {
    return;
  }
  _async_queue.push_back(e);
  xTaskNotifyGive(_async_service_task_handle);
}

static inline void _prepend_async_event(lwip_tcp_event_packet_t *e) {
  if (e == nullptr) {
    return;
  }
  _async_queue.push_front(e);
  xTaskNotifyGive(_async_service_task_handle);
}

static inline bool _on_async_task() {
  return xTaskGetCurrentTaskHandle() == _async_service_task_handle;
}

// no task yet = no events to race
static inline bool _off_task() {
  return _async_service_task_handle && !_on_async_task();
}

// close or release from another task
static void _queue_for_task(lwip_tcp_event_packet_t *e) {
  queue_mutex_guard guard;
  _send_async_event(e);
}

static inline lwip_tcp_event_packet_t *_get_async_event() {
  queue_mutex_guard guard;
  while (1) {
    lwip_tcp_event_packet_t *e = _async_queue.pop_front();

    if ((!e) || (e->event != LWIP_TCP_POLL)) {
      return e;
    }

    /*
      Let's try to coalesce two (or more) consecutive poll events into one
      this usually happens with poor implemented user-callbacks that are runs too long and makes poll events to stack in the queue
      if consecutive user callback for a same connection runs longer that poll time then it will fill the queue with events until it deadlocks.
      This is a workaround to mitigate such poor designs and won't let other events/connections to starve the task time.
      It won't be effective if user would run multiple simultaneous long running callbacks due to message interleaving.
      todo: implement fair dequeuing or (better) simply punish user for a bad designed callbacks by resetting hog connections
    */
    for (lwip_tcp_event_packet_t *next_pkt = _async_queue.begin(); next_pkt && (next_pkt->client == e->client) && (next_pkt->event == LWIP_TCP_POLL);
         next_pkt = _async_queue.begin()) {
      // if the next event that will come is a poll event for the same connection, we can discard it and continue
      _free_event(_async_queue.pop_front());
      async_tcp_log_d("coalescing polls, network congestion or async callbacks might be too slow!");
    }

    /*
      now we have to decide if to proceed with poll callback handler or discard it?
      poor designed apps using asynctcp without proper dataflow control could flood the queue with interleaved pool/ack events.
      I.e. on each poll app would try to generate more data to send, which in turn results in additional ack event triggering chain effect
      for long connections. Or poll callback could take long time starving other connections. Anyway our goal is to keep the queue length
      grows under control (if possible) and poll events are the safest to discard.
      Let's discard poll events processing using linear-increasing probability curve when queue size grows over 3/4
      Poll events are periodic and connection could get another chance next time
    */
    if (_async_queue.size() > (_xor_shift_next() % CONFIG_ASYNC_TCP_QUEUE_SIZE / 4 + CONFIG_ASYNC_TCP_QUEUE_SIZE * 3 / 4)) {
      _free_event(e);
      async_tcp_log_d("discarding poll due to queue congestion");
      continue;
    }

    return e;
  }
}

static size_t _remove_events_for_client(AsyncClient *client) {
  lwip_tcp_event_packet_t *removed_event_chain;
  {
    queue_mutex_guard guard;
    // a queued release still has to free it
    removed_event_chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &pkt) {
      return pkt.client == client && pkt.event != LWIP_TCP_RELEASE;
    });
  }

  size_t count = 0;
  while (removed_event_chain) {
    ++count;
    auto t = removed_event_chain;
    removed_event_chain = t->next;
    _free_event(t);
  }
  return count;
};

// never handed to user code
static size_t _remove_events_for_server(AsyncServer *server) {
  lwip_tcp_event_packet_t *removed_event_chain;
  {
    queue_mutex_guard guard;
    removed_event_chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &pkt) {
      return pkt.event == LWIP_TCP_ACCEPT && pkt.accept.server == server;
    });
  }

  size_t count = 0;
  while (removed_event_chain) {
    ++count;
    auto t = removed_event_chain;
    removed_event_chain = t->next;
    delete t->client;
    _free_event(t);
  }
  return count;
}

// true = handled
bool AsyncTCP_detail::owner_event(lwip_tcp_event_packet_t *e) {
  if (e->event == LWIP_TCP_RELEASE) {
    delete e->client;
    return true;
  }
  if (e->client->_released) {
    return true;   // no more callbacks
  }
  if (e->event == LWIP_TCP_CLOSE) {
    if (e->close.gen == e->client->_gen) {
      e->client->close();
    }
    return true;
  }
  return false;
}

// nullptr = the lookup failed
static ip_addr_t *dns_answer(lwip_tcp_event_packet_t *e) {
  return e->dns.found ? &e->dns.addr : nullptr;
}

void AsyncTCP_detail::handle_async_event(lwip_tcp_event_packet_t *e) {
  lifetime_guard guard;
  if (e->client == NULL || !_client_alive(e->client)) {
    // client gone
  } else if (e->event == LWIP_TCP_ACCEPT && !_server_alive(e->accept.server)) {
    // never handed to user code
    delete e->client;
  } else if (owner_event(e)) {
    // done
  } else if (e->event == LWIP_TCP_RECV) {
    e->client->_recv(e->recv.pcb, e->recv.pb, e->recv.err);
    e->recv.pb = nullptr;  // given to client
  } else if (e->event == LWIP_TCP_FIN) {
    e->client->_fin(e->fin.pcb, e->fin.err);
  } else if (e->event == LWIP_TCP_SENT) {
    e->client->_sent(e->sent.pcb, e->sent.len);
  } else if (e->event == LWIP_TCP_POLL) {
    e->client->_poll(e->poll.pcb);
  } else if (e->event == LWIP_TCP_ERROR) {
    e->client->_error(e->error.err);
  } else if (e->event == LWIP_TCP_CONNECTED) {
    e->client->_connected(e->connected.pcb, e->connected.err);
  } else if (e->event == LWIP_TCP_ACCEPT) {
    e->accept.server->_accepted(e->client);
  } else if (e->event == LWIP_TCP_DNS) {
    if (e->dns.id == e->client->_dns_id) {
      e->client->_dns_found(dns_answer(e));
    }
  }
  _free_event(e);
}

static void _async_service_task(void *pvParameters) {
#if CONFIG_ASYNC_TCP_USE_WDT
  if (esp_task_wdt_add(NULL) != ESP_OK) {
    async_tcp_log_w("Failed to add async task to WDT");
  }
#endif
  for (;;) {
#if CONFIG_FREERTOS_UNICORE
    uint8_t ran = 0;
#endif
    for (;;) {
      {
        // pop + dispatch under one lock
        lifetime_guard guard;
        auto packet = _get_async_event();
        if (!packet) {
          break;
        }
        AsyncTCP_detail::handle_async_event(packet);
      }
#if CONFIG_ASYNC_TCP_USE_WDT
      esp_task_wdt_reset();
#endif
#if CONFIG_FREERTOS_UNICORE
      // one core, else loop starves
      if (++ran >= ASYNC_TCP_BATCH) {
        ran = 0;
        vTaskDelay(1);
      }
#endif
    }
    // queue is empty
    ulTaskNotifyTake(pdTRUE, ASYNC_TCP_MAX_TASK_SLEEP);
#if CONFIG_ASYNC_TCP_USE_WDT
    esp_task_wdt_reset();
#endif
  }
}

static bool _start_async_task() {
  if (!_async_queue_mutex) {
    _async_queue_mutex = xSemaphoreCreateMutex();
    if (!_async_queue_mutex) {
      return false;
    }
  }
  _lifetime_mtx_init();

  if (!_async_service_task_handle) {
    // -1 = any core
#if CONFIG_ASYNC_TCP_RUNNING_CORE >= 0 && !defined(CONFIG_FREERTOS_UNICORE)
    xTaskCreatePinnedToCore(_async_service_task, "async_tcp", CONFIG_ASYNC_TCP_STACK_SIZE, NULL,
                            CONFIG_ASYNC_TCP_PRIORITY, &_async_service_task_handle, CONFIG_ASYNC_TCP_RUNNING_CORE);
#else
    xTaskCreate(_async_service_task, "async_tcp", CONFIG_ASYNC_TCP_STACK_SIZE, NULL,
                CONFIG_ASYNC_TCP_PRIORITY, &_async_service_task_handle);
#endif
    if (!_async_service_task_handle) {
      return false;
    }
  }
  return true;
}

/*
 * LwIP Callbacks
 * */

static void _bind_tcp_callbacks(tcp_pcb *pcb, AsyncClient *client) {
  tcp_arg(pcb, client);
  tcp_recv(pcb, &AsyncTCP_detail::tcp_recv);
  tcp_sent(pcb, &AsyncTCP_detail::tcp_sent);
  tcp_err(pcb, &AsyncTCP_detail::tcp_error);
  tcp_poll(pcb, &AsyncTCP_detail::tcp_poll, CONFIG_ASYNC_TCP_POLL_TIMER);
}

static void _reset_tcp_callbacks(tcp_pcb *pcb, AsyncClient *client) {
  tcp_arg(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
  if (client) {
    _remove_events_for_client(client);
  }
}

static int8_t _tcp_connected(void *arg, tcp_pcb *pcb, int8_t err) {
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_CONNECTED, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return ERR_MEM;
  }
  e->connected.pcb = pcb;
  e->connected.err = err;
  queue_mutex_guard guard;
  _send_async_event(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_poll(void *arg, struct tcp_pcb *pcb) {
  // throttle polling events queueing when event queue is getting filled up, let it handle _onack's
  {
    queue_mutex_guard guard;
    // async_tcp_log_d("qs:%u", _async_queue.size());
    if (_async_queue.size() > (_xor_shift_next() % CONFIG_ASYNC_TCP_QUEUE_SIZE / 2 + CONFIG_ASYNC_TCP_QUEUE_SIZE / 4)) {
      async_tcp_log_d("throttling");
      return ERR_OK;
    }
  }

  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_POLL, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return ERR_MEM;
  }
  e->poll.pcb = pcb;

  queue_mutex_guard guard;
  _send_async_event(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pb, int8_t err) {
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_RECV, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return ERR_MEM;
  }
  if (pb) {
    e->recv.pcb = pcb;
    e->recv.pb = pb;
    e->recv.err = err;
  } else {
    e->event = LWIP_TCP_FIN;
    e->fin.pcb = pcb;
    e->fin.err = err;
  }

  queue_mutex_guard guard;
  _send_async_event(e);
  return ERR_OK;
}

int8_t AsyncTCP_detail::tcp_sent(void *arg, struct tcp_pcb *pcb, uint16_t len) {
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_SENT, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return ERR_MEM;
  }
  e->sent.pcb = pcb;
  e->sent.len = len;

  queue_mutex_guard guard;
  _send_async_event(e);
  return ERR_OK;
}

void AsyncTCP_detail::tcp_error(void *arg, int8_t err) {
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  if (client) {
    // lwIP task, client may be freed
    queue_mutex_guard guard;
    if (!_client_alive_unlocked(client)) {
      return;
    }
    if (client->_pcb) {
      // The pcb has already been freed by LwIP; do not attempt to clear the callbacks!
      client->_pcb_alive.store(false, std::memory_order_release);
      client->_pcb = nullptr;
    }
  }
  if (client) {
    _remove_events_for_client(client);
  }

  // enqueue event to be processed in the async task for the user callback
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_ERROR, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return;
  }
  e->error.err = err;

  queue_mutex_guard guard;
  _send_async_event(e);
}

static void _tcp_dns_found(const char *, ip_addr_t *ipaddr, void *arg) {
  auto ask = reinterpret_cast<dns_ask *>(arg);
  AsyncClient *client = ask->client;
  uint32_t id = ask->id;
  delete ask;

  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_DNS, client};
  if (!e) {
    async_tcp_log_e("Failed to allocate event packet");
    return;
  }
  e->dns.id = id;

  e->dns.found = ipaddr != nullptr;
  if (ipaddr) {
    memcpy(&e->dns.addr, ipaddr, sizeof(ip_addr_t));
  } else {
    memset(&e->dns.addr, 0, sizeof(e->dns.addr));
  }

  queue_mutex_guard guard;
  _send_async_event(e);
}

/*
 * TCP/IP API Calls
 * */

#include "lwip/priv/tcpip_priv.h"

typedef struct {
  struct tcpip_api_call_data call;
  tcp_pcb **pcb;
  int8_t err;
  union {
    AsyncClient *close;
    struct {
      const char *buf;
      size_t size;
      uint8_t apiflags;
    } write;
    size_t received;
    struct {
      ip_addr_t *addr;
      uint16_t port;
      tcp_connected_fn cb;
    } connect;
    struct {
      ip_addr_t *addr;
      uint16_t port;
    } bind;
    uint8_t backlog;
  };
} tcp_api_call_t;

// lwIP thread or core lock held
static inline bool _pcb_in_active_lists(const struct tcp_pcb *pcb) {
  for (const struct tcp_pcb *p = tcp_active_pcbs; p; p = p->next) {
    if (p == pcb) return true;
  }
  for (const struct tcp_pcb *p = tcp_tw_pcbs; p; p = p->next) {
    if (p == pcb) return true;
  }
  return false;
}

// these trip lwIP write asserts
static inline bool _pcb_state_writable(const struct tcp_pcb *pcb) {
  switch (pcb->state) {
    case LISTEN: case CLOSED: case TIME_WAIT:
    case FIN_WAIT_2: case CLOSING: case LAST_ACK:
      return false;
    default:
      return true;
  }
}

// lwIP thread
static inline bool _pcb_is_active(const struct tcp_pcb *pcb) {
  if (pcb == NULL) return false;
  if (!_pcb_in_active_lists(pcb)) return false;
  return _pcb_state_writable(pcb);
}

// user thread, takes core lock
tcp_pcb *AsyncClient::_validate_pcb() const {
  tcp_pcb *p = _live_pcb();
  if (!p) return nullptr;
  LOCK_TCPIP_CORE();
  bool ok = _pcb_in_active_lists(p) && _pcb_state_writable(p);
  UNLOCK_TCPIP_CORE();
  return ok ? p : nullptr;
}

static err_t _tcp_output_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = ERR_CONN;
  if (_pcb_is_active(*msg->pcb)) {
    msg->err = tcp_output(*msg->pcb);
  }
  return msg->err;
}

static esp_err_t _tcp_output(tcp_pcb **pcb) {
  if (!pcb || !*pcb) {
    return ERR_CONN;
  }
  tcp_api_call_t msg;
  msg.pcb = pcb;
  tcpip_api_call(_tcp_output_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_write_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = ERR_CONN;
  if (_pcb_is_active(*msg->pcb)) {
    msg->err = tcp_write(*msg->pcb, msg->write.buf, msg->write.size, msg->write.apiflags);
  }
  return msg->err;
}

static esp_err_t _tcp_write(tcp_pcb **pcb, const char *data, size_t size, uint8_t apiflags) {
  if (!pcb || !*pcb) {
    return ERR_CONN;
  }
  tcp_api_call_t msg;
  msg.pcb = pcb;
  msg.write.buf = data;
  msg.write.size = size;
  msg.write.apiflags = apiflags;
  tcpip_api_call(_tcp_write_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_recved_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = ERR_CONN;
  struct tcp_pcb *pcb = *msg->pcb;
  if (!_pcb_is_active(pcb)) {
    return msg->err;
  }
  // Clamp len to remaining rcv_wnd headroom; lwIP asserts on overflow.
  // rcv_wnd can pass max, subtract would wrap
  size_t len = msg->received;
  if (pcb->rcv_wnd >= TCP_WND_MAX(pcb)) {
    len = 0;
  } else {
    tcpwnd_size_t headroom = (tcpwnd_size_t)(TCP_WND_MAX(pcb) - pcb->rcv_wnd);
    if (len > (size_t)headroom) {
      len = (size_t)headroom;
    }
  }
  msg->err = 0;
  if (len > 0) {
    tcp_recved(pcb, len);
  }
  return msg->err;
}

static esp_err_t _tcp_recved(tcp_pcb **pcb, size_t len) {
  if (!pcb || !*pcb) {
    return ERR_CONN;
  }
  tcp_api_call_t msg;
  msg.pcb = pcb;
  msg.received = len;
  tcpip_api_call(_tcp_recved_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_close_api(struct tcpip_api_call_data *api_call_msg) {
  // Unlike the other calls, this is not a direct wrapper of the LwIP function;
  // we perform the AsyncClient teardown interlocked safely with the LwIP task.

  // As a postcondition, the queue must not have any events referencing
  // the AsyncClient in api_call_msg->close.  This is because it is possible for
  // an error event to have been queued, clearing the pcb*, but after the async
  // thread has committed to closing/destructing the AsyncClient object.

  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = ERR_CONN;
  if (*msg->pcb) {
    tcp_pcb *pcb = *msg->pcb;
    msg->close->_mark_pcb_dead();
    _reset_tcp_callbacks(pcb, msg->close);
    tcp_backlog_accepted(pcb);   // frees the backlog slot
    if (tcp_close(pcb) != ERR_OK) {
      // We do not permit failure here: abandon the pcb anyways.
      tcp_abort(pcb);
    }
    msg->err = ERR_OK;
    *msg->pcb = nullptr;  // PCB is now the property of LwIP
  } else {
    // Ensure there is not an error event queued for this client
    if (_remove_events_for_client(msg->close)) {
      msg->err = ERR_OK;  // dispose needs to be run
    }
  }
  return msg->err;
}

static esp_err_t _tcp_close(tcp_pcb **pcb, AsyncClient *client) {
  tcp_api_call_t msg;
  msg.pcb = pcb;
  msg.close = client;
  tcpip_api_call(_tcp_close_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_abort_api(struct tcpip_api_call_data *api_call_msg) {
  // Like close(), we must ensure that the queue is cleared
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  if (*msg->pcb) {
    msg->close->_mark_pcb_dead();
    tcp_abort(*msg->pcb);
    *msg->pcb = nullptr;  // PCB is now the property of LwIP
    msg->err = ERR_ABRT;
  } else {
    msg->err = ERR_CONN;
  }
  return msg->err;
}

static esp_err_t _tcp_abort(tcp_pcb **pcb, AsyncClient *client) {
  if (!pcb || !*pcb) {
    return ERR_CONN;
  }
  tcp_api_call_t msg;
  msg.pcb = pcb;
  msg.close = client;
  tcpip_api_call(_tcp_abort_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_connect_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = tcp_connect(*msg->pcb, msg->connect.addr, msg->connect.port, msg->connect.cb);
  return msg->err;
}

static esp_err_t _tcp_connect(tcp_pcb *pcb, ip_addr_t *addr, uint16_t port, tcp_connected_fn cb) {
  if (!pcb) {
    return ESP_FAIL;
  }
  tcp_api_call_t msg;
  msg.pcb = &pcb;  // cannot be invalidated by LwIP at this point
  msg.connect.addr = addr;
  msg.connect.port = port;
  msg.connect.cb = cb;
  tcpip_api_call(_tcp_connect_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_bind_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  tcp_pcb *pcb = *msg->pcb;
  msg->err = tcp_bind(pcb, msg->bind.addr, msg->bind.port);
  if (msg->err != ERR_OK) {
    // Close the pcb on behalf of the server without an extra round-trip through the LwIP lock
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
    }
    *msg->pcb = nullptr;  // PCB is now owned by LwIP
  }
  return msg->err;
}

static esp_err_t _tcp_bind(tcp_pcb **pcb, ip_addr_t *addr, uint16_t port) {
  if (!pcb || !*pcb) {
    return ESP_FAIL;
  }
  tcp_api_call_t msg;
  msg.pcb = pcb;
  msg.bind.addr = addr;
  msg.bind.port = port;
  tcpip_api_call(_tcp_bind_api, (struct tcpip_api_call_data *)&msg);
  return msg.err;
}

static err_t _tcp_listen_api(struct tcpip_api_call_data *api_call_msg) {
  tcp_api_call_t *msg = (tcp_api_call_t *)api_call_msg;
  msg->err = 0;
  *msg->pcb = tcp_listen_with_backlog(*msg->pcb, msg->backlog);
  return msg->err;
}

static tcp_pcb *_tcp_listen_with_backlog(tcp_pcb *pcb, uint8_t backlog) {
  if (!pcb) {
    return NULL;
  }
  tcp_api_call_t msg;
  msg.pcb = &pcb;
  msg.backlog = backlog ? backlog : 0xFF;
  tcpip_api_call(_tcp_listen_api, (struct tcpip_api_call_data *)&msg);
  return pcb;
}

// pcb and callbacks in one call
struct tcp_new_client_api_call_t {
  struct tcpip_api_call_data call;
  u8_t addr_type;
  AsyncClient *client;
  tcp_pcb *pcb;
};

static err_t _tcp_new_client_api(struct tcpip_api_call_data *api_call_msg) {
  auto *msg = reinterpret_cast<tcp_new_client_api_call_t *>(api_call_msg);
  msg->pcb = tcp_new_ip_type(msg->addr_type);
  if (msg->pcb) {
    _bind_tcp_callbacks(msg->pcb, msg->client);
  }
  return ERR_OK;
}

static tcp_pcb *_tcp_new_client(u8_t addr_type, AsyncClient *client) {
  tcp_new_client_api_call_t msg;
  msg.addr_type = addr_type;
  msg.client = client;
  msg.pcb = nullptr;
  tcpip_api_call(_tcp_new_client_api, reinterpret_cast<struct tcpip_api_call_data *>(&msg));
  return msg.pcb;
}

// sync with lwIP task before the purge
struct tcp_server_close_api_call_t {
  struct tcpip_api_call_data call;
  tcp_pcb **pcb;
};

static err_t _tcp_server_close_api(struct tcpip_api_call_data *api_call_msg) {
  auto *msg = reinterpret_cast<tcp_server_close_api_call_t *>(api_call_msg);
  if (*msg->pcb) {
    tcp_arg(*msg->pcb, NULL);
    tcp_accept(*msg->pcb, NULL);
    if (tcp_close(*msg->pcb) != ERR_OK) {
      tcp_abort(*msg->pcb);
    }
    *msg->pcb = NULL;
  }
  return ERR_OK;
}

static void _tcp_server_close(tcp_pcb **pcb) {
  tcp_server_close_api_call_t msg;
  msg.pcb = pcb;
  tcpip_api_call(_tcp_server_close_api, reinterpret_cast<struct tcpip_api_call_data *>(&msg));
}

// arg + accept together on lwIP thread
struct tcp_server_accept_api_call_t {
  struct tcpip_api_call_data call;
  tcp_pcb *pcb;
  AsyncServer *server;
};

static err_t _tcp_server_accept_api(struct tcpip_api_call_data *api_call_msg) {
  auto *msg = reinterpret_cast<tcp_server_accept_api_call_t *>(api_call_msg);
  tcp_arg(msg->pcb, msg->server);
  tcp_accept(msg->pcb, &AsyncTCP_detail::tcp_accept);
  return ERR_OK;
}

static void _tcp_server_accept(tcp_pcb *pcb, AsyncServer *server) {
  tcp_server_accept_api_call_t msg;
  msg.pcb = pcb;
  msg.server = server;
  tcpip_api_call(_tcp_server_accept_api, reinterpret_cast<struct tcpip_api_call_data *>(&msg));
}

// lwIP thread owns the DNS cache
struct tcp_dns_api_call_t {
  struct tcpip_api_call_data call;
  const char *host;
  ip_addr_t *addr;
  dns_ask *ask;
  err_t err;
};

static err_t _tcp_dns_gethostbyname_api(struct tcpip_api_call_data *api_call_msg) {
  auto *msg = reinterpret_cast<tcp_dns_api_call_t *>(api_call_msg);
  msg->err = dns_gethostbyname(msg->host, msg->addr,
                               (dns_found_callback)&_tcp_dns_found, msg->ask);
  return ERR_OK;
}

// ERR_INPROGRESS: _tcp_dns_found frees ask
static err_t _tcp_dns_gethostbyname(const char *host, ip_addr_t *addr, dns_ask *ask) {
  tcp_dns_api_call_t msg;
  msg.host = host;
  msg.addr = addr;
  msg.ask = ask;
  msg.err = ERR_VAL;
  tcpip_api_call(_tcp_dns_gethostbyname_api, reinterpret_cast<struct tcpip_api_call_data *>(&msg));
  return msg.err;
}

/*
  Async TCP Client
 */

AsyncClient::AsyncClient(tcp_pcb *pcb)
  : _connect_cb(0), _connect_cb_arg(0), _discard_cb(0), _discard_cb_arg(0), _sent_cb(0), _sent_cb_arg(0), _error_cb(0), _error_cb_arg(0), _recv_cb(0),
    _recv_cb_arg(0), _pb_cb(0), _pb_cb_arg(0), _timeout_cb(0), _timeout_cb_arg(0), _poll_cb(0), _poll_cb_arg(0), _ack_pcb(true), _tx_last_packet(0),
    _rx_timeout(0), _rx_last_ack(0), _ack_timeout(CONFIG_ASYNC_TCP_MAX_ACK_TIME), _connect_port(0) {
  _pcb = pcb;
  _release_ev = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_RELEASE, this};
  _register_client(this);
  if (_pcb) {
    _pcb_alive.store(true, std::memory_order_release);
    _rx_last_packet = millis();
    _bind_tcp_callbacks(_pcb, this);
  }
}

AsyncClient::~AsyncClient() {
  lifetime_guard guard;  // waits for dispatch
  _unregister_client(this);
  if (_pcb) {
    _close();
  }
  _remove_events_for_client(this);
  delete _release_ev;   // unless release() took it
}

/*
 * Operators
 * */

bool AsyncClient::operator==(const AsyncClient &other) const {
  return _pcb == other._pcb;
}

/*
 * Callback Setters
 * */

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

/*
 * Main Public Methods
 * */

bool AsyncClient::connect(ip_addr_t addr, uint16_t port) {
  if (_pcb) {
    async_tcp_log_d("already connected, state %d", _pcb->state);
    return false;
  }
  _closed = false;
  _close_wanted = false;
  _rx_ack_len = 0;
  _gen++;
  return _connect_to(addr, port);
}

// DNS answer too, no new _gen
bool AsyncClient::_connect_to(ip_addr_t addr, uint16_t port) {
  if (!_start_async_task()) {
    async_tcp_log_e("failed to start task");
    return false;
  }

#if LWIP_IPV4 && LWIP_IPV6
  tcp_pcb *pcb = _tcp_new_client(addr.type, this);
#else
  tcp_pcb *pcb = _tcp_new_client(IPADDR_TYPE_V4, this);
#endif
  if (!pcb) {
    async_tcp_log_e("pcb == NULL");
    return false;
  }

  esp_err_t err = _tcp_connect(pcb, &addr, port, (tcp_connected_fn)&_tcp_connected);
  return err == ESP_OK;
}

#ifdef ARDUINO
bool AsyncClient::connect(const IPAddress &ip, uint16_t port) {
  ip_addr_t addr;
#if ESP_IDF_VERSION_MAJOR < 5
#if LWIP_IPV4 && LWIP_IPV6
  // if both IPv4 and IPv6 are enabled, ip_addr_t has a union field and the address type
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
  ip_addr_t addr;
  // same reset as connect(addr)
  if (_pcb) {
    return false;
  }
  _closed = false;
  _close_wanted = false;
  _rx_ack_len = 0;
  _gen++;

  if (!_start_async_task()) {
    async_tcp_log_e("failed to start task");
    return false;
  }

  // id drops a stale answer
  _dns_id = ++_dns_seq;
  dns_ask *ask = new (std::nothrow) dns_ask{this, _dns_id};
  if (!ask) {
    return false;
  }
  err_t err = _tcp_dns_gethostbyname(host, &addr, ask);
  if (err != ERR_INPROGRESS) {
    delete ask;
  }

  if (err == ERR_OK) {
#if ESP_IDF_VERSION_MAJOR < 5
#if LWIP_IPV6
    if (addr.type == IPADDR_TYPE_V6) {
      return connect(IPv6Address(addr.u_addr.ip6.addr), port);
    }
    return connect(IPAddress(addr.u_addr.ip4.addr), port);
#else
    return connect(IPAddress(addr.addr), port);
#endif
#else
    return connect(addr, port);
#endif
  } else if (err == ERR_INPROGRESS) {
    _connect_port = port;
    return true;
  }
  async_tcp_log_d("error: %d", err);
  return false;
}

// off task only queues
void AsyncClient::close() {
  if (_off_task()) {
    lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_CLOSE, this};
    if (!e) {
      _close_wanted = true;
      return;
    }
    e->close.gen = _gen;
    _queue_for_task(e);
    return;
  }
  lifetime_guard guard;
  _closed = true;
  if (_pcb) {
    _tcp_recved(&_pcb, _rx_ack_len.load());
  }
  _close();
}

// any task, never waits
void AsyncClient::release() {
  _released = true;
  if (!_off_task()) {
    delete this;
    return;
  }
  lwip_tcp_event_packet_t *e = _release_ev;
  _release_ev = nullptr;
  if (!e) {
    // leak beats use after free
    async_tcp_log_e("release: no event, client leaked");
    return;
  }
  _queue_for_task(e);
}

int8_t AsyncClient::abort() {
  if (_off_task()) {
    close();   // only the task aborts
    return ERR_OK;
  }
  return _tcp_abort(&_pcb, this);
  // _pcb is now NULL
}

size_t AsyncClient::space() const {
  tcp_pcb *p = _live_pcb();
  if (p && p->state == ESTABLISHED) {
    return tcp_sndbuf(p);
  }
  return 0;
}

size_t AsyncClient::add(const char *data, size_t size, uint8_t apiflags) {
  if (!_pcb || size == 0 || data == NULL) {
    return 0;
  }
  size_t room = space();
  if (!room) {
    return 0;
  }
  size_t will_send = (room < size) ? room : size;
  int8_t err = ERR_OK;
  err = _tcp_write(&_pcb, data, will_send, apiflags);
  if (err != ERR_OK) {
    return 0;
  }
  return will_send;
}

bool AsyncClient::send() {
  auto backup = _tx_last_packet;
  _tx_last_packet = millis();
  if (_tcp_output(&_pcb) == ERR_OK) {
    return true;
  }
  _tx_last_packet = backup;
  return false;
}

size_t AsyncClient::ack(size_t len) {
  uint32_t have = _rx_ack_len.load();
  do {
    if (len > have) {
      len = have;
    }
  } while (!_rx_ack_len.compare_exchange_weak(have, have - len));
  if (len) {
    _tcp_recved(&_pcb, len);
  }
  return len;
}

void AsyncClient::ackPacket(struct pbuf *pb) {
  if (!pb) {
    return;
  }
  _tcp_recved(&_pcb, pb->len);
  pbuf_free(pb);
}

/*
 * Main Private Methods
 * */

int8_t AsyncClient::_close() {
  int8_t err = _tcp_close(&_pcb, this);
  // _pcb is now NULL
  if ((err == ERR_OK) && _discard_cb && !_released) {
    // _pcb was closed here
    _told = true;
    _discard_cb(_discard_cb_arg, this);
  }
  return err;
}

/*
 * Private Callbacks
 * */

int8_t AsyncClient::_connected(tcp_pcb *pcb, int8_t err) {
  // no stale check, breaks reconnects
  _pcb = reinterpret_cast<tcp_pcb *>(pcb);
  if (_pcb) {
    _pcb_alive.store(true, std::memory_order_release);
    _rx_last_packet = millis();
  }
  _tx_last_packet = 0;
  _rx_last_ack = 0;
  if (_connect_cb) {
    _connect_cb(_connect_cb_arg, this);
  }
  return ERR_OK;
}

// close() in onError may have told
void AsyncClient::_error(int8_t err) {
  _told = false;
  if (_error_cb) {
    _error_cb(_error_cb_arg, this, err);
  }
  if (_client_alive(this) && _discard_cb && !_told) {
    _discard_cb(_discard_cb_arg, this);
  }
}

// In Async Thread
int8_t AsyncClient::_fin(tcp_pcb *pcb, int8_t err) {
  close();
  return ERR_OK;
}

int8_t AsyncClient::_sent(tcp_pcb *pcb, uint16_t len) {
  _rx_last_ack = _rx_last_packet = millis();
  if (_sent_cb) {
    _sent_cb(_sent_cb_arg, this, len, (_rx_last_packet - _tx_last_packet));
  }
  return ERR_OK;
}

int8_t AsyncClient::_recv(tcp_pcb *pcb, pbuf *pb, int8_t err) {
  if (_closed) {
    while (pb != NULL) {
      pbuf *b = pb;
      pb = b->next;
      b->next = NULL;
      pbuf_free(b);
    }
    return ERR_OK;
  }
  while (pb != NULL) {
    _rx_last_packet = millis();
    // we should not ack before we assimilate the data
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
    if (!_client_alive(this) || _closed) {
      if (b) pbuf_free(b);
      while (pb != NULL) {
        b = pb;
        pb = b->next;
        b->next = NULL;
        pbuf_free(b);
      }
      return ERR_OK;
    }
    if (b) {
      if (_ack_pcb) {
        ack(b->len);
      }
      pbuf_free(b);
    }
  }
  return ERR_OK;
}

int8_t AsyncClient::_poll(tcp_pcb *pcb) {
  if (!_pcb) {
    // async_tcp_log_d("pcb is NULL");
    return ERR_OK;
  }
  if (pcb != _pcb) {
    async_tcp_log_d("0x%08" PRIx32 " != 0x%08" PRIx32, (uint32_t)pcb, (uint32_t)_pcb);
    return ERR_OK;
  }
  if (_close_wanted.exchange(false)) {
    close();
    return ERR_OK;
  }

  uint32_t now = millis();

  // ACK Timeout
  if (_ack_timeout) {
    const uint32_t one_day = 86400000;
    bool last_tx_is_after_last_ack = (_rx_last_ack - _tx_last_packet + one_day) < one_day;
    if (last_tx_is_after_last_ack && (now - _tx_last_packet) >= _ack_timeout) {
      async_tcp_log_d("ack timeout %d", pcb->state);
      if (_timeout_cb) {
        _timeout_cb(_timeout_cb_arg, this, (now - _tx_last_packet));
      }
      return ERR_OK;
    }
  }
  // RX Timeout
  if (_rx_timeout && (now - _rx_last_packet) >= (_rx_timeout * 1000)) {
    async_tcp_log_d("rx timeout %d", pcb->state);
    _close();
    return ERR_OK;
  }
  // Everything is fine
  if (_poll_cb) {
    _poll_cb(_poll_cb_arg, this);
  }
  return ERR_OK;
}

void AsyncClient::_dns_found(ip_addr_t *ipaddr) {
  if (_closed || _pcb) {
    return;
  }
  if (ipaddr && _connect_to(*ipaddr, _connect_port)) {
    return;
  }
  // tell the owner, else it waits forever
  if (_error_cb) {
    _error_cb(_error_cb_arg, this, ipaddr ? ERR_CONN : -55);
  }
  if (_client_alive(this) && _discard_cb) {
    _discard_cb(_discard_cb_arg, this);
  }
}

/*
 * Public Helper Methods
 * */

bool AsyncClient::free() {
  tcp_pcb *p = _live_pcb();
  return !p || p->state == CLOSED || p->state > ESTABLISHED;
}

size_t AsyncClient::write(const char *data, size_t size, uint8_t apiflags) {
  size_t will_send = add(data, size, apiflags);
  if (!will_send || !send()) {
    return 0;
  }
  return will_send;
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
  tcp_pcb *p = _validate_pcb();
  if (!p) return;
  if (nodelay) {
    tcp_nagle_disable(p);
  } else {
    tcp_nagle_enable(p);
  }
}

bool AsyncClient::getNoDelay() {
  tcp_pcb *p = _validate_pcb();
  return p && tcp_nagle_disabled(p);
}

void AsyncClient::setKeepAlive(uint32_t ms, uint8_t cnt) {
  tcp_pcb *p = _validate_pcb();
  if (!p) return;
  if (ms != 0) {
    p->so_options |= SOF_KEEPALIVE;
    p->keep_idle = ms;
    p->keep_intvl = ms;
    p->keep_cnt = cnt;
  } else {
    p->so_options &= ~SOF_KEEPALIVE;
  }
}

uint16_t AsyncClient::getMss() const {
  tcp_pcb *p = _validate_pcb();
  return p ? tcp_mss(p) : 0;
}

uint32_t AsyncClient::getRemoteAddress() const {
  tcp_pcb *p = _validate_pcb();
  if (!p) return 0;
#if LWIP_IPV4 && LWIP_IPV6
  return p->remote_ip.u_addr.ip4.addr;
#else
  return p->remote_ip.addr;
#endif
}

#if LWIP_IPV6
ip6_addr_t AsyncClient::getRemoteAddress6() const {
  tcp_pcb *p = _validate_pcb();
  if (p && p->remote_ip.type == IPADDR_TYPE_V6) {
    return p->remote_ip.u_addr.ip6;
  }
  ip6_addr_t nulladdr;
  ip6_addr_set_zero(&nulladdr);
  return nulladdr;
}

ip6_addr_t AsyncClient::getLocalAddress6() const {
  tcp_pcb *p = _validate_pcb();
  if (p && p->local_ip.type == IPADDR_TYPE_V6) {
    return p->local_ip.u_addr.ip6;
  }
  ip6_addr_t nulladdr;
  ip6_addr_set_zero(&nulladdr);
  return nulladdr;
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
  tcp_pcb *p = _validate_pcb();
  if (!p) return IPAddress(IPType::IPv6);
  IPAddress ip;
  ip.from_ip_addr_t(&(p->remote_ip));
  return ip;
}

IPAddress AsyncClient::localIP6() const {
  tcp_pcb *p = _validate_pcb();
  if (!p) return IPAddress(IPType::IPv6);
  IPAddress ip;
  ip.from_ip_addr_t(&(p->local_ip));
  return ip;
}
#endif
#endif
#endif

uint16_t AsyncClient::getRemotePort() const {
  tcp_pcb *p = _validate_pcb();
  return p ? p->remote_port : 0;
}

uint32_t AsyncClient::getLocalAddress() const {
  tcp_pcb *p = _validate_pcb();
  if (!p) return 0;
#if LWIP_IPV4 && LWIP_IPV6
  return p->local_ip.u_addr.ip4.addr;
#else
  return p->local_ip.addr;
#endif
}

uint16_t AsyncClient::getLocalPort() const {
  tcp_pcb *p = _validate_pcb();
  return p ? p->local_port : 0;
}

ip4_addr_t AsyncClient::getRemoteAddress4() const {
  tcp_pcb *p = _validate_pcb();
  if (p) {
#if LWIP_IPV4 && LWIP_IPV6
    if (p->remote_ip.type == IPADDR_TYPE_V4) return p->remote_ip.u_addr.ip4;
#else
    return p->remote_ip;
#endif
  }
  ip4_addr_t nulladdr;
  ip4_addr_set_zero(&nulladdr);
  return nulladdr;
}

ip4_addr_t AsyncClient::getLocalAddress4() const {
  tcp_pcb *p = _validate_pcb();
  if (p) {
#if LWIP_IPV4 && LWIP_IPV6
    if (p->local_ip.type == IPADDR_TYPE_V4) return p->local_ip.u_addr.ip4;
#else
    return p->local_ip;
#endif
  }
  ip4_addr_t nulladdr;
  ip4_addr_set_zero(&nulladdr);
  return nulladdr;
}

#ifdef ARDUINO
IPAddress AsyncClient::remoteIP() const {
#if ESP_IDF_VERSION_MAJOR < 5
  return IPAddress(getRemoteAddress());
#else
  tcp_pcb *p = _validate_pcb();
  if (!p) return IPAddress();
  IPAddress ip;
  ip.from_ip_addr_t(&(p->remote_ip));
  return ip;
#endif
}

IPAddress AsyncClient::localIP() const {
#if ESP_IDF_VERSION_MAJOR < 5
  return IPAddress(getLocalAddress());
#else
  tcp_pcb *p = _validate_pcb();
  if (!p) return IPAddress();
  IPAddress ip;
  ip.from_ip_addr_t(&(p->local_ip));
  return ip;
#endif
}
#endif

uint8_t AsyncClient::state() const {
  tcp_pcb *p = _live_pcb();
  return p ? p->state : 0;
}

bool AsyncClient::connected() const {
  tcp_pcb *p = _live_pcb();
  return p && p->state == ESTABLISHED;
}

bool AsyncClient::connecting() const {
  tcp_pcb *p = _live_pcb();
  return p && p->state > CLOSED && p->state < ESTABLISHED;
}

bool AsyncClient::disconnecting() const {
  tcp_pcb *p = _live_pcb();
  return p && p->state > ESTABLISHED && p->state < TIME_WAIT;
}

bool AsyncClient::disconnected() const {
  tcp_pcb *p = _live_pcb();
  return !p || p->state == CLOSED || p->state == TIME_WAIT;
}

bool AsyncClient::freeable() const {
  tcp_pcb *p = _live_pcb();
  return !p || p->state == CLOSED || p->state > ESTABLISHED;
}

bool AsyncClient::canSend() const {
  return space() > 0;
}

/*
  Async TCP Server
 */

AsyncServer::AsyncServer(ip_addr_t addr, uint16_t port)
  : _port(port), _addr(addr), _noDelay(false), _pcb(nullptr), _connect_cb(nullptr), _connect_cb_arg(nullptr) {}

#ifdef ARDUINO
AsyncServer::AsyncServer(IPAddress addr, uint16_t port) : _port(port), _noDelay(false), _pcb(0), _connect_cb(0), _connect_cb_arg(0) {
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
AsyncServer::AsyncServer(IPv6Address addr, uint16_t port) : _port(port), _noDelay(false), _pcb(0), _connect_cb(0), _connect_cb_arg(0) {
#if LWIP_IPV4 && LWIP_IPV6
  _addr.type = IPADDR_TYPE_V6;
#endif
  auto ipaddr = static_cast<const uint32_t *>(addr);
  _addr = IPADDR6_INIT(ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
}
#endif
#endif

AsyncServer::AsyncServer(uint16_t port) : _port(port), _noDelay(false), _pcb(0), _connect_cb(0), _connect_cb_arg(0) {
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
  if (_pcb) {
    return;
  }

  if (!_start_async_task()) {
    async_tcp_log_e("failed to start task");
    return;
  }
  int8_t err;
  {
    tcp_core_guard tcg;
#if LWIP_IPV4 && LWIP_IPV6
    _pcb = tcp_new_ip_type(_addr.type);
#else
    _pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
#endif
  }
  if (!_pcb) {
    async_tcp_log_e("_pcb == NULL");
    return;
  }

  // rebind with clients in TIME_WAIT
  ip_set_option(_pcb, SOF_REUSEADDR);

  err = _tcp_bind(&_pcb, &_addr, _port);

  if (err != ERR_OK) {
    // pcb was closed by _tcp_bind
    async_tcp_log_e("bind error: %d", err);
    return;
  }

  uint8_t backlog = _maxClients ? _maxClients : 5;
  _pcb = _tcp_listen_with_backlog(_pcb, backlog);
  if (!_pcb) {
    async_tcp_log_e("listen_pcb == NULL");
    return;
  }
  _register_server(this);
  _tcp_server_accept(_pcb, this);
}

void AsyncServer::end() {
  _unregister_server(this);
  _tcp_server_close(&_pcb);
  _remove_events_for_server(this);
}

// runs on LwIP thread
int8_t AsyncTCP_detail::tcp_accept(void *arg, tcp_pcb *pcb, int8_t err) {
  if (!pcb) {
    async_tcp_log_e("_accept failed: pcb is NULL");
    return ERR_ABRT;
  }
  auto server = reinterpret_cast<AsyncServer *>(arg);
  if (!_server_alive(server)) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  if (server->_connect_cb) {
    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
    if (c && c->pcb()) {
      // core lock held, not setNoDelay()
      if (server->_noDelay) {
        tcp_nagle_disable(pcb);
      } else {
        tcp_nagle_enable(pcb);
      }

      lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_ACCEPT, c};
      if (e) {
        e->accept.server = server;
        // holds a backlog slot till freed
        if (server->_maxClients) tcp_backlog_delayed(pcb);

        queue_mutex_guard guard;
        _prepend_async_event(e);
        return ERR_OK;  // success
      }

      // Couldn't allocate accept event
      // We can't let the client object call in to close, as we're on the LWIP thread; it could deadlock trying to RPC to itself
      c->_mark_pcb_dead();
      c->_pcb = nullptr;
      tcp_abort(pcb);
      async_tcp_log_e("_accept failed: couldn't accept client");
      return ERR_ABRT;
    }
    if (c) {
      // Couldn't complete setup
      // pcb has already been aborted
      delete c;
      pcb = nullptr;
      async_tcp_log_e("_accept failed: couldn't complete setup");
      return ERR_ABRT;
    }
    async_tcp_log_e("_accept failed: couldn't allocate client");
  } else {
    async_tcp_log_e("_accept failed: no onConnect callback");
  }
  tcp_abort(pcb);
  return ERR_OK;
}

int8_t AsyncServer::_accepted(AsyncClient *client) {
  if (_connect_cb) {
    _connect_cb(_connect_cb_arg, client);
  }
  return ERR_OK;
}

void AsyncServer::setNoDelay(bool nodelay) {
  _noDelay = nodelay;
}

bool AsyncServer::getNoDelay() const {
  return _noDelay;
}

uint8_t AsyncServer::status() const {
  if (!_pcb) {
    return 0;
  }
  return _pcb->state;
}
