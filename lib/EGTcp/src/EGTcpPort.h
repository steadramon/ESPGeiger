/*
  EGTcpPort.h - what the core and a backend share. Not for apps.

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
#ifndef EGTCPPORT_H
#define EGTCPPORT_H

#include "EGTcp.h"
#include "EGTcpLock.h"

// EGTcpClient::_ev, run in this order
enum : uint16_t {
  EV_ACCEPT    = 1 << 0,
  EV_DNS       = 1 << 1,
  EV_CONNECTED = 1 << 2,
  EV_RX        = 1 << 3,
  EV_SENT      = 1 << 4,
  EV_POLL      = 1 << 5,
  EV_FIN       = 1 << 6,
  EV_ERROR     = 1 << 7,
};

static const int8_t EGTCP_LATER = -5;   // lwIP's ERR_INPROGRESS

// The stack's arg until it answers, c is cleared under the lock
struct EGTcpAsk {
  EGTcpClient* c;
  uint32_t id;
};

struct EGTcpInfo {
  EGTcpAddr local;
  EGTcpAddr remote;
  uint16_t local_port;
  uint16_t remote_port;
  uint16_t mss;
};

// the owner calls these, the backend runs them on the stack's side
struct EGTcpOps {
  int8_t (*connect)(EGTcpClient* c, const EGTcpAddr& a, uint16_t port);
  int8_t (*resolve)(EGTcpClient* c, const char* host, EGTcpAddr* out);   // 0, EGTCP_LATER or fail
  size_t (*write)(EGTcpClient* c, const void* data, size_t len, uint8_t flags);
  void (*recved)(EGTcpClient* c, size_t len);
  void (*close)(EGTcpClient* c);      // never fails, the stack's after
  void (*abort)(EGTcpClient* c);
  void (*shutdown)(EGTcpClient* c);
  void (*info)(const EGTcpClient* c, EGTcpInfo* i);
  void (*nodelay)(EGTcpClient* c, bool on);
  void (*keepalive)(EGTcpClient* c, uint32_t ms, uint8_t count);
  // lock held
  const uint8_t* (*rx_peek)(EGTcpClient* c, size_t* len);
  void (*rx_drop)(EGTcpClient* c, size_t len);
  void (*rx_clear)(EGTcpClient* c);
  bool (*listen)(EGTcpServer* s, const EGTcpAddr& a, uint16_t port, uint8_t backlog);
  void (*unlisten)(EGTcpServer* s);
};

extern const EGTcpOps* const egtcp_ops;   // the build's backend

struct EGTcpCore {
#if !defined(ESP8266)
  static EGTcpLock& lock();
#endif
  static void post(EGTcpClient* c, uint16_t ev);   // lock held
  static uint32_t now();
  // backend's, per chip
#if defined(ESP32)
  static void owner();   // poll()'s task
  static void check();   // warns once if called off that task
#else
  static void owner() {}   // one task
  static void check() {}
#endif
};

// Lock held, the 8266 has no fetch_add
template <class T>
__attribute__((always_inline)) inline void locked_add(std::atomic<T>& a, T n, std::memory_order o) {
  a.store(a.load(std::memory_order_relaxed) + n, o);
}

template <class T>
__attribute__((always_inline)) inline void locked_sub(std::atomic<T>& a, T n, std::memory_order o) {
  a.store(a.load(std::memory_order_relaxed) - n, o);
}

// Holds the lock while in scope
class EGTcpHold {
  public:
#if defined(ESP8266)
    EGTcpHold() {}
#else
    EGTcpHold() : _l(EGTcpCore::lock()) { _l.lock(); }
    ~EGTcpHold() { _l.unlock(); }
  private:
    EGTcpLock& _l;
#endif
};

#endif
