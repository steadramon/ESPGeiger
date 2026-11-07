/*
  EGTcp.cpp - the owner's side: ready list, callbacks, client and server.

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
#include <EGClock.h>

#include <string.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

// clients per poll(), the loop keeps going
#ifndef EGTCP_BATCH
#define EGTCP_BATCH 16
#endif

// window updates under this wait for more
#define EGTCP_CREDIT_MIN 512

std::atomic<bool> EGTcp::pending{false};

void EGTcp::setClock(MillisFn fn) { EGClock::set(fn); }

uint32_t EGTcp::now() { return EGClock::now(); }

uint32_t EGTcpCore::now() { return EGClock::now(); }

#if !defined(ESP8266)
EGTcpLock& EGTcpCore::lock() {
  static EGTcpLock l;
  return l;
}
#endif

/*
 * Ready list: the stack adds, the owner takes
 */

static EGTcpClient* s_head = nullptr;
static EGTcpClient* s_tail = nullptr;
static EGTcpClient* s_running = nullptr;   // whose callback, ~EGTcpClient clears it

void EGTcpCore::post(EGTcpClient* c, uint16_t ev) {
  c->_ev |= ev;
  if (!c->_queued) {
    c->_queued = true;
    c->_next = nullptr;
    if (s_tail) s_tail->_next = c;
    else s_head = c;
    s_tail = c;
  }
  // the lock orders it
  EGTcp::pending.store(true, std::memory_order_relaxed);
}

struct EGTcpRun {
  // lock held
  static void unqueue(EGTcpClient* c) {
    c->_ev = 0;
    c->_ev_acked = 0;
    if (!c->_queued) return;
    EGTcpClient* prev = nullptr;
    for (EGTcpClient* p = s_head; p; prev = p, p = p->_next) {
      if (p != c) continue;
      if (prev) prev->_next = c->_next;
      else s_head = c->_next;
      if (s_tail == c) s_tail = prev;
      break;
    }
    c->_queued = false;
    c->_next = nullptr;
  }

  // the stack adds to acked, so it goes with the bits
  static EGTcpClient* take(uint16_t& ev, uint32_t& acked) {
    EGTcpHold h;
    EGTcpClient* c = s_head;
    if (!c) return nullptr;
    s_head = c->_next;
    if (!s_head) s_tail = nullptr;
    c->_queued = false;
    ev = c->_ev;
    c->_ev = 0;
    if (ev & EV_SENT) {
      acked = c->_ev_acked;
      c->_ev_acked = 0;
    }
    return c;
  }

  // accepts for s, never handed over, lock held
  static EGTcpClient* unqueue_accepts(EGTcpServer* s) {
    EGTcpClient* gone = nullptr;
    EGTcpClient* p = s_head;
    while (p) {
      EGTcpClient* n = p->_next;
      if (p->_srv == s) {
        unqueue(p);
        p->_accept_next = gone;
        gone = p;
      }
      p = n;
    }
    return gone;
  }

  /*
   * Dispatch, owner's task
   */

  // false: never seen, or deleted in onAccept
  static bool hand_over(EGTcpClient* c, uint16_t ev) {
    EGTcpServer* s = c->_srv;
    c->_srv = nullptr;
    if ((ev & EV_ERROR) || !s) {
      delete c;
      return false;
    }
    c->opened();   // handed over open, onAccept can write
    if (!s->_on_accept) {
      EGTcpClient** p = &s->_pull;
      while (*p) p = &(*p)->_accept_next;
      *p = c;
      return true;
    }
    s_running = c;
    s->_on_accept(c);
    return s_running == c;
  }

  // not deleted, closed or redialled since
  static bool live(EGTcpClient* c, uint8_t life) {
    return s_running == c && c->_life == life;
  }

  // poll, then how it ended
  static void tail(EGTcpClient* c, uint16_t ev, uint8_t life) {
    if ((ev & EV_POLL) && live(c, life)) c->timers();
    if ((ev & EV_FIN) && live(c, life)) c->ended(0);
    if ((ev & EV_ERROR) && live(c, life)) c->ended(c->_ev_err);
  }

  // in bit order, each once
  static void dispatch(EGTcpClient* c, uint16_t ev, uint32_t acked) {
    if ((ev & EV_ACCEPT) && !hand_over(c, ev)) return;
    s_running = c;
    uint8_t life = c->_life;
    if (ev & EV_DNS) c->resolved();
    if ((ev & EV_CONNECTED) && live(c, life)) c->opened();
    if ((ev & EV_RX) && live(c, life)) c->delivered();
    if ((ev & EV_SENT) && live(c, life)) c->acked(acked);
    tail(c, ev, life);
    s_running = nullptr;
  }
};

void EGTcp::run() {
  static bool s_in_run = false;
  if (s_in_run) return;   // a callback polled
  s_in_run = true;
  EGTcpCore::owner();
  // before the drain, a post after sets it again
  pending.store(false, std::memory_order_relaxed);
  for (uint8_t n = 0; n < EGTCP_BATCH; n++) {
    uint16_t ev;
    uint32_t acked = 0;
    EGTcpClient* c = EGTcpRun::take(ev, acked);
    if (!c) {
      s_in_run = false;
      return;
    }
    EGTcpRun::dispatch(c, ev, acked);
  }
  pending.store(true, std::memory_order_relaxed);   // more next pass
  s_in_run = false;
}

/*
 * EGTcpAddr
 */

bool EGTcpAddr::any() const {
  for (uint8_t i = 0; i < 16; i++) {
    if (b[i]) return false;
  }
  return true;
}

bool EGTcpAddr::operator==(const EGTcpAddr& o) const {
  return v6 == o.v6 && memcmp(b, o.b, 16) == 0;
}

#ifdef ARDUINO
EGTcpAddr::EGTcpAddr(const IPAddress& ip) {
  b[0] = ip[0]; b[1] = ip[1]; b[2] = ip[2]; b[3] = ip[3];
}

IPAddress EGTcpAddr::ip() const {
  if (v6) return IPAddress();
  return IPAddress(b[0], b[1], b[2], b[3]);
}
#endif

/*
 * EGTcpClient
 */

EGTcpClient::EGTcpClient() : _ops(egtcp_ops) {}

EGTcpClient::~EGTcpClient() {
  if (s_running == this) s_running = nullptr;   // its callback stops here
  forget(false);
}

// no stack link, no events, no data
void EGTcpClient::forget(bool rst) {
  if (_h.load()) {
    if (rst) _ops->abort(this);
    else _ops->close(this);
  }
  {
    EGTcpHold h;
    EGTcpRun::unqueue(this);
    if (_ask) _ask->c = nullptr;   // answer goes nowhere
    _ask = nullptr;
    _ops->rx_clear(this);
  }
  _owed = _credit = _unacked = 0;
  _ack_later = false;
  _life++;
}

bool EGTcpClient::dial(const EGTcpAddr& a, uint16_t port) {
  int8_t e = _ops->connect(this, a, port);
  if (e == 0) return true;
  _state = CLOSED;
  _err = e;
  rearm();
  return false;
}

void EGTcpClient::start() {
  EGTcpCore::check();
  forget(false);
  _err = 0;
  _start_ms = EGTcpCore::now();
  _state = CONNECTING;
  rearm();
}

bool EGTcpClient::connect(const EGTcpAddr& a, uint16_t port) {
  if (_state == CONNECTING || _state == OPEN) return false;
  start();
  return dial(a, port);
}

bool EGTcpClient::connect(const char* host, uint16_t port) {
  if (!host || _state == CONNECTING || _state == OPEN) return false;
  start();
  _port = port;
  _dns_id++;
  EGTcpAddr a;
  int8_t e = _ops->resolve(this, host, &a);
  if (e == 0) return dial(a, port);   // cached
  if (e == EGTCP_LATER) return true;
  _state = CLOSED;
  _err = e == E_MEM ? E_MEM : E_DNS;
  rearm();
  return false;
}

void EGTcpClient::close() {
  // else lwIP sends RST over unread data
  if (_h.load()) credit(_owed + _rx_len.load(), true);
  forget(false);
  _state = IDLE;
  _err = 0;
  rearm();
}

void EGTcpClient::abort() {
  forget(true);
  _state = IDLE;
  _err = 0;
  rearm();
}

void EGTcpClient::shutdown() {
  if (_state == OPEN) _ops->shutdown(this);
}

size_t EGTcpClient::space() const {
  return connected() ? _snd.load(std::memory_order_relaxed) : 0;
}

size_t EGTcpClient::write(const void* data, size_t len, uint8_t flags) {
  if (!data || !len || !space()) return 0;   // full costs no trip
  EGTcpCore::check();
  size_t n = _ops->write(this, data, len, flags);
  if (n) {
    _unacked += n;
    _tx_ms = EGTcpCore::now();
    rearm();
  }
  return n;
}

size_t EGTcpClient::write(const char* s) {
  return s ? write(s, strlen(s)) : 0;
}

size_t EGTcpClient::available() const {
  return _rx_len.load(std::memory_order_acquire);
}

size_t EGTcpClient::read(void* out, size_t len) {
  uint8_t* dst = (uint8_t*)out;
  size_t got = 0;
  while (got < len) {
    EGTcpHold h;
    size_t n = 0;
    const uint8_t* d = _ops->rx_peek(this, &n);
    if (!d) break;
    if (n > len - got) n = len - got;
    memcpy(dst + got, d, n);
    _ops->rx_drop(this, n);
    got += n;
  }
  credit(got, available() == 0);
  return got;
}

int EGTcpClient::read() {
  uint8_t b;
  return read(&b, 1) ? b : -1;
}

int EGTcpClient::peek() {
  EGTcpHold h;
  size_t n = 0;
  const uint8_t* d = _ops->rx_peek(this, &n);
  return d ? *d : -1;
}

void EGTcpClient::ack(size_t len) {
  if (len > _owed) len = _owed;
  _owed -= len;
  credit(len, true);
}

// batched, lwIP only updates the window past a threshold anyway
void EGTcpClient::credit(size_t n, bool flush) {
  _credit += n;
  if (!_h.load()) {
    _credit = 0;
    return;
  }
  if (!_credit || (!flush && _credit < EGTCP_CREDIT_MIN)) return;
  _ops->recved(this, _credit);
  _credit = 0;
}

void EGTcpClient::setNoDelay(bool on) {
  if (_h.load()) _ops->nodelay(this, on);
}

void EGTcpClient::setKeepAlive(uint32_t idle_ms, uint8_t count) {
  if (_h.load()) _ops->keepalive(this, idle_ms, count);
}

EGTcpInfo EGTcpClient::info() const {
  EGTcpInfo i{};
  _ops->info(this, &i);
  return i;
}

EGTcpAddr EGTcpClient::remoteAddr() const { return info().remote; }
uint16_t EGTcpClient::remotePort() const  { return info().remote_port; }
EGTcpAddr EGTcpClient::localAddr() const  { return info().local; }
uint16_t EGTcpClient::localPort() const   { return info().local_port; }
uint16_t EGTcpClient::mss() const         { return info().mss; }

// what happened, owner's task

// the answer stays put till the next ask, the owner's
void EGTcpClient::resolved() {
  if (_ev_dns != _dns_id || _state != CONNECTING || _h.load()) return;
  if (!_ev_found) {
    ended(E_DNS);
    return;
  }
  int8_t e = _ops->connect(this, _ev_addr, _port);
  if (e) ended(e);
}

void EGTcpClient::opened() {
  if (_state != CONNECTING || !_h.load()) return;   // an error took it
  _state = OPEN;
  _rx_ms = _tx_ms = EGTcpCore::now();
  rearm();
  if (_on_connect) _on_connect();
}

void EGTcpClient::delivered() {
  _rx_ms = EGTcpCore::now();
  rearm();
  if (!_on_data) return;   // pull, it waits
  for (;;) {
    size_t n = 0;
    const uint8_t* d;
    {
      EGTcpHold h;
      d = _ops->rx_peek(this, &n);
    }
    if (!d) return;
    // only the owner drops, d holds
    _ack_later = false;
    _on_data(d, n);
    if (s_running != this || _state == IDLE) return;   // deleted or closed
    {
      EGTcpHold h;
      _ops->rx_drop(this, n);
    }
    if (_ack_later) _owed += n;
    else credit(n, available() == 0);
  }
}

void EGTcpClient::acked(uint32_t n) {
  _unacked = n > _unacked ? 0 : _unacked - n;
  _rx_ms = EGTcpCore::now();
  rearm();
  if (_on_sent) _on_sent(n);
}

static bool late(uint32_t now, uint32_t since, uint32_t limit) {
  return limit && now - since >= limit;
}

// idle runs from the later of rx and tx, signed diff so millis wrap is fine
bool EGTcpClient::due(uint32_t now) const {
  uint32_t heard = (int32_t)(_rx_ms - _tx_ms) > 0 ? _rx_ms : _tx_ms;
  return (_state == CONNECTING && late(now, _start_ms, _connect_ms)) ||
         (_state == OPEN && late(now, heard, _idle_ms)) ||
         (_unacked && late(now, _tx_ms, _ack_ms));
}

// the soonest due() could say yes, for the stack's poll
void EGTcpClient::rearm() {
  uint32_t at = 0;
  bool any = false;
  auto sooner = [&](uint32_t since, uint32_t limit) {
    if (!limit) return;
    uint32_t t = since + limit;
    if (!any || (int32_t)(t - at) < 0) at = t;
    any = true;
  };
  if (_state == CONNECTING) sooner(_start_ms, _connect_ms);
  if (_state == OPEN) sooner((int32_t)(_rx_ms - _tx_ms) > 0 ? _rx_ms : _tx_ms, _idle_ms);
  if (_unacked) sooner(_tx_ms, _ack_ms);
  // 0 = nothing timed, so a real 0 goes as 1
  _due_at.store(any ? (at ? at : 1) : 0, std::memory_order_relaxed);
}

void EGTcpClient::timers() {
  if (!due(EGTcpCore::now())) return;
  if (_h.load()) _ops->abort(this);
  ended(E_TIMEOUT);
}

// once: the peer, the stack or a timer ended it
void EGTcpClient::ended(int8_t err) {
  if (_state != CONNECTING && _state != OPEN) return;
  if (_h.load()) {
    credit(_owed + available(), true);   // FIN, not RST; pull keeps the data
    _ops->close(this);
  }
  _state = CLOSED;
  if (!_err) _err = err;
  rearm();
  if (_on_close) _on_close(err);
}

/*
 * EGTcpServer
 */

EGTcpServer::~EGTcpServer() {
  end();
}

bool EGTcpServer::begin(uint16_t port, const EGTcpAddr& addr) {
  if (_h.load()) return false;
  EGTcpCore::check();
  _port = port;
  return egtcp_ops->listen(this, addr, port, _max);
}

EGTcpClient* EGTcpServer::accept() {
  EGTcpClient* c = _pull;
  if (c) {
    _pull = c->_accept_next;
    c->_accept_next = nullptr;
  }
  return c;
}

// accepts not handed over go
void EGTcpServer::end() {
  if (_h.load()) egtcp_ops->unlisten(this);
  EGTcpClient* gone;
  {
    EGTcpHold h;
    gone = EGTcpRun::unqueue_accepts(this);
  }
  while (gone) {
    EGTcpClient* n = gone->_accept_next;
    delete gone;
    gone = n;
  }
  while (EGTcpClient* c = accept()) delete c;
}
