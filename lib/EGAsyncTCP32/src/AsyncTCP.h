// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

#ifndef ASYNCTCP_H_
#define ASYNCTCP_H_

#include "AsyncTCPVersion.h"
#define ASYNCTCP_FORK_ESP32Async

#ifdef ARDUINO
#include "IPAddress.h"
#if __has_include(<IPv6Address.h>)
#include "IPv6Address.h"
#endif
#endif

#include "lwip/ip6_addr.h"
#include "lwip/ip_addr.h"
#include <atomic>
#include <functional>

#include "sdkconfig.h"
extern "C" {
#include "freertos/semphr.h"
#include "lwip/pbuf.h"
}

#ifndef CONFIG_ASYNC_TCP_QUEUE_SIZE
#define CONFIG_ASYNC_TCP_QUEUE_SIZE 64
#endif

#ifndef CONFIG_ASYNC_TCP_MAX_ACK_TIME
#define CONFIG_ASYNC_TCP_MAX_ACK_TIME 5000
#endif

class AsyncClient;
class AsyncServer;
class AsyncTCP_detail;
struct tcp_pcb;
struct dns_ask;

#define ASYNC_WRITE_FLAG_COPY 0x01  // copy into lwIP, else data must outlive the send
#define ASYNC_WRITE_FLAG_MORE 0x02  // no PSH, more to come

typedef std::function<void(void *, AsyncClient *)> AcConnectHandler;
typedef std::function<void(void *, AsyncClient *, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void *, AsyncClient *, int8_t error)> AcErrorHandler;
typedef std::function<void(void *, AsyncClient *, void *data, size_t len)> AcDataHandler;
typedef std::function<void(void *, AsyncClient *, struct pbuf *pb)> AcPacketHandler;
typedef std::function<void(void *, AsyncClient *, uint32_t time)> AcTimeoutHandler;

/*
  Two threads. lwIP's tcpip thread (tiT) runs lwIP and only queues events.
  The owner, one task (the Arduino loop), calls AsyncTCP::poll() every pass:
  the callbacks run there, and every AsyncClient/AsyncServer call comes from
  there. Same shape as the 8266.

  Only tiT writes _pcb or touches a tcp_pcb. The owner tests _pcb for null,
  reads the copy tiT keeps (_st, _snd), or makes one tcpip_api_call.
*/
namespace AsyncTCP {
extern std::atomic<bool> pending;
void run();
// every loop pass, cheap when idle
inline void poll() {
  if (pending.load(std::memory_order_relaxed)) run();
}
}  // namespace AsyncTCP

typedef enum {
  LWIP_TCP_SENT,
  LWIP_TCP_RECV,
  LWIP_TCP_FIN,
  LWIP_TCP_ERROR,
  LWIP_TCP_POLL,
  LWIP_TCP_ACCEPT,
  LWIP_TCP_CONNECTED,
  LWIP_TCP_DNS,
  LWIP_TCP_RELEASE    // an accept the server never saw
} lwip_tcp_event_t;

// no pcb in here, a dropped pcb purges its events
struct lwip_tcp_event_packet_t {
  lwip_tcp_event_packet_t *next;
  lwip_tcp_event_t event;
  AsyncClient *client;
  union {
    struct {
      int8_t err;
    } error;
    struct {
      uint16_t len;
    } sent;
    struct {
      pbuf *pb;
    } recv;
    struct {
      AsyncServer *server;
    } accept;
    struct {
      ip_addr_t addr;
      uint32_t id;
      bool found;    // false = failed
    } dns;
  };

  inline lwip_tcp_event_packet_t(lwip_tcp_event_t _event, AsyncClient *_client) : next(nullptr), event(_event), client(_client){};
};

class AsyncClient {
public:
  AsyncClient(tcp_pcb *pcb = 0);
  ~AsyncClient();

  AsyncClient(const AsyncClient &) = delete;
  AsyncClient &operator=(const AsyncClient &) = delete;
  AsyncClient(AsyncClient &&) = delete;
  AsyncClient &operator=(AsyncClient &&) = delete;

  bool operator==(const AsyncClient &other) const;
  bool operator!=(const AsyncClient &other) const {
    return !(*this == other);
  }

  bool connect(ip_addr_t addr, uint16_t port);
#ifdef ARDUINO
  bool connect(const IPAddress &ip, uint16_t port);
#if __has_include(<IPv6Address.h>)
  bool connect(const IPv6Address &ip, uint16_t port);
#endif
#endif
  bool connect(const char *host, uint16_t port);

  [[deprecated("Use AsyncClient::close() instead")]]
  void close(bool now) {
    close();
  }
  [[deprecated("Use AsyncClient::close() instead")]]
  void stop() {
    close();
  };

  void close();
  // close and delete, no callbacks after; safe in its own callback
  void release();

  int8_t abort();
  bool free();

  // ack is not pending
  bool canSend() const;
  // TCP buffer space available
  size_t space() const;

  // queue, send later; returns bytes taken
  size_t add(const char *data, size_t size, uint8_t apiflags = ASYNC_WRITE_FLAG_COPY);
  // send what add() queued
  bool send();
  // add + send in one trip; queued counts as taken
  size_t write(const char *data, size_t size, uint8_t apiflags = ASYNC_WRITE_FLAG_COPY);
  size_t write(const char *data) {
    return data == NULL ? 0 : write(data, strlen(data));
  };

  // tiT's copy, one event behind at most
  uint8_t state() const;
  bool connecting() const;
  bool connected() const;
  bool disconnecting() const;
  bool disconnected() const;
  // disconnected or disconnecting
  bool freeable() const;

  // these ask tiT
  uint16_t getMss() const;
  void setNoDelay(bool nodelay) const;
  bool getNoDelay();
  void setKeepAlive(uint32_t ms, uint8_t cnt);

  uint32_t getRxTimeout() const;
  // no RX data timeout, seconds
  void setRxTimeout(uint32_t timeout);
  uint32_t getAckTimeout() const;
  // no ACK timeout for the last send, ms
  void setAckTimeout(uint32_t timeout);

  uint32_t getRemoteAddress() const;
  uint16_t getRemotePort() const;
  uint16_t remotePort() const {
    return getRemotePort();
  }
  uint32_t getLocalAddress() const;
  uint16_t getLocalPort() const;
  uint16_t localPort() const {
    return getLocalPort();
  }
  ip4_addr_t getRemoteAddress4() const;
  ip4_addr_t getLocalAddress4() const;
#if LWIP_IPV6
  ip6_addr_t getRemoteAddress6() const;
  ip6_addr_t getLocalAddress6() const;
#ifdef ARDUINO
#if __has_include(<IPv6Address.h>)
  IPv6Address remoteIP6() const;
  IPv6Address localIP6() const;
#else
  IPAddress remoteIP6() const;
  IPAddress localIP6() const;
#endif
#endif
#endif
#ifdef ARDUINO
  IPAddress remoteIP() const;
  IPAddress localIP() const;
#endif

  void onConnect(AcConnectHandler cb, void *arg = 0);
  void onDisconnect(AcConnectHandler cb, void *arg = 0);
  void onAck(AcAckHandler cb, void *arg = 0);
  // failed connect or error
  void onError(AcErrorHandler cb, void *arg = 0);
  // data, if onPacket is not set
  void onData(AcDataHandler cb, void *arg = 0);
  // you own the pbuf: ackPacket() it or free it
  void onPacket(AcPacketHandler cb, void *arg = 0);
  void onTimeout(AcTimeoutHandler cb, void *arg = 0);
  // every 500 ms when connected
  void onPoll(AcConnectHandler cb, void *arg = 0);

  void ackPacket(struct pbuf *pb);
  size_t ack(size_t len);
  // from onData: ack() it yourself later
  void ackLater() {
    _ack_pcb = false;
  }

  // lwIP thread only
  tcp_pcb *pcb() {
    return _pcb.load();
  }
  void _own(tcp_pcb *pcb);     // lwIP thread: take or drop it
  void _cache(tcp_pcb *pcb);   // lwIP thread: refresh _st, _snd

protected:
  friend class AsyncTCP_detail;
  friend class AsyncServer;

  std::atomic<tcp_pcb *> _pcb{nullptr};
  std::atomic<uint8_t> _st{0};    // tcp_state, 0 = CLOSED
  std::atomic<uint16_t> _snd{0};  // tcp_sndbuf

  AcConnectHandler _connect_cb;
  void *_connect_cb_arg = nullptr;
  AcConnectHandler _discard_cb;
  void *_discard_cb_arg = nullptr;
  AcAckHandler _sent_cb;
  void *_sent_cb_arg = nullptr;
  AcErrorHandler _error_cb;
  void *_error_cb_arg = nullptr;
  AcDataHandler _recv_cb;
  void *_recv_cb_arg = nullptr;
  AcPacketHandler _pb_cb;
  void *_pb_cb_arg = nullptr;
  AcTimeoutHandler _timeout_cb;
  void *_timeout_cb_arg = nullptr;
  AcConnectHandler _poll_cb;
  void *_poll_cb_arg = nullptr;

  bool _ack_pcb = true;
  bool _closed = false;     // by the owner
  bool _told = false;       // onDisconnect ran
  bool _released = false;   // no more callbacks
  lwip_tcp_event_packet_t _release_ev{LWIP_TCP_RELEASE, this};   // never allocs
  dns_ask *_ask = nullptr;  // lwIP's till it answers, queue lock
  uint32_t _dns_id = 0;     // older answers dropped
  uint32_t _tx_last_packet = 0;
  uint32_t _rx_ack_len = 0;
  uint32_t _rx_last_packet = 0;
  uint32_t _rx_timeout = 0;
  uint32_t _rx_last_ack = 0;
  uint32_t _ack_timeout = CONFIG_ASYNC_TCP_MAX_ACK_TIME;
  uint16_t _connect_port = 0;

  int8_t _close();
  void _connected();
  void _error(int8_t err);
  void _poll();
  void _sent(uint16_t len);
  void _recv(pbuf *pb);
  void _dns_found(ip_addr_t *ipaddr);
  bool _connect_to(ip_addr_t addr, uint16_t port);
};

class AsyncServer {
public:
  AsyncServer(ip_addr_t addr, uint16_t port);
#ifdef ARDUINO
  AsyncServer(IPAddress addr, uint16_t port);
#if __has_include(<IPv6Address.h>)
  AsyncServer(IPv6Address addr, uint16_t port);
#endif
#endif
  AsyncServer(uint16_t port);
  ~AsyncServer();
  void onClient(AcConnectHandler cb, void *arg);
  void begin();
  void end();
  void setNoDelay(bool nodelay);
  bool getNoDelay() const;
  uint8_t status() const;
  // before begin(), lwIP drops SYNs past n
  void setMaxClients(uint8_t n) {
    _maxClients = n;
  }

protected:
  friend class AsyncTCP_detail;

  uint16_t _port;
  ip_addr_t _addr;
  bool _noDelay = false;
  uint8_t _maxClients = 0;   // 0 = lwIP's 5 half-open
  tcp_pcb *_pcb = nullptr;   // listening; tiT reads it only in calls
  AcConnectHandler _connect_cb;
  void *_connect_cb_arg = nullptr;
};

#endif /* ASYNCTCP_H_ */
