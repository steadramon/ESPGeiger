/*
  EGTcp.h - TCP client and server, one owner task.

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
#ifndef EGTCP_H
#define EGTCP_H

/*
  The stack's callbacks only note what happened. The owner, one task (the
  Arduino loop), calls EGTcp::poll() and every callback below runs there.
  Every EGTcpClient/EGTcpServer call comes from that task too.

  A backend (EGTcpOps) does the network, lwIP raw. No lwIP or Arduino
  types in here.
*/

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#ifdef ARDUINO
#include <IPAddress.h>
#endif

class EGTcpClient;
class EGTcpServer;
struct EGTcpOps;
struct EGTcpAsk;
struct EGTcpInfo;

namespace EGTcp {
extern std::atomic<bool> pending;
void run();
// from the owner's loop, cheap when idle
inline void poll() {
  if (pending.load(std::memory_order_relaxed)) run();
}
using MillisFn = uint32_t (*)();
void setClock(MillisFn fn);   // = EGClock::set, nullptr = millis()
uint32_t now();               // = EGClock::now
}

struct EGTcpAddr {
  uint8_t v6 = 0;       // 0 = v4 in b[0..3]
  uint8_t b[16] = {};

  EGTcpAddr() {}
  EGTcpAddr(uint8_t a, uint8_t b1, uint8_t c, uint8_t d) {
    b[0] = a; b[1] = b1; b[2] = c; b[3] = d;
  }
  bool any() const;
  bool operator==(const EGTcpAddr& o) const;
#ifdef ARDUINO
  EGTcpAddr(const IPAddress& ip);
  IPAddress ip() const;   // v4
#endif
};

class EGTcpClient {
  public:
    enum State : uint8_t { IDLE, CONNECTING, OPEN, CLOSED };

    // Write flags
    static const uint8_t COPY = 0x01;   // else the data has to stay valid until acked
    static const uint8_t MORE = 0x02;   // no PSH, more to come

    // lwIP's numbers
    static const int8_t E_MEM = -1;
    static const int8_t E_TIMEOUT = -3;
    static const int8_t E_CONN = -11;
    static const int8_t E_ABRT = -13;   // stack gave up
    static const int8_t E_RST = -14;
    static const int8_t E_DNS = -55;

    EGTcpClient();
    ~EGTcpClient();   // closes, fine in its own callback
    EGTcpClient(const EGTcpClient&) = delete;
    EGTcpClient& operator=(const EGTcpClient&) = delete;

    // false = busy, or failed straight away and error() says why. Else onConnect or onClose follows
    bool connect(const char* host, uint16_t port);
    bool connect(const EGTcpAddr& ip, uint16_t port);
    void close();      // queued data still goes, then FIN; unread dropped
    void abort();      // RST
    void shutdown();   // FIN after queued data, keep reading

    size_t space() const;   // bytes write() takes now, 0 when not open
    // up to space(), sends now unless MORE
    size_t write(const void* data, size_t len, uint8_t flags = COPY);
    size_t write(const char* s);

    // Pull: with no onData the data waits here, the window opens as it's read
    size_t available() const;
    size_t read(void* out, size_t len);
    int read();   // -1 = nothing
    int peek();

    // Push: call in onData to keep the window shut, then ack() it later
    void ackLater() { _ack_later = true; }
    void ack(size_t len);

    using ConnectFn = void (*)(void* ctx);
    using DataFn = void (*)(void* ctx, const uint8_t* data, size_t len);
    using SentFn = void (*)(void* ctx, size_t acked);
    using CloseFn = void (*)(void* ctx, int8_t err);
    using FinFn = bool (*)(void* ctx);

    void onConnect(ConnectFn fn, void* ctx) { _on_connect = fn; _connect_ctx = ctx; }
    // data is gone once it returns, don't read() in here
    void onData(DataFn fn, void* ctx) { _on_data = fn; _data_ctx = ctx; }
    void onSent(SentFn fn, void* ctx) { _on_sent = fn; _sent_ctx = ctx; }
    // Called once if it ends without a close(). 0 = peer closed, else why
    void onClose(CloseFn fn, void* ctx) { _on_close = fn; _close_ctx = ctx; }
    // Peer sent FIN, done sending. true = stay open to answer, then close(); else onClose(0)
    void onFin(FinFn fn, void* ctx) { _on_fin = fn; _fin_ctx = ctx; }

    // ms, 0 = off; ends with E_TIMEOUT
    void setConnectTimeout(uint32_t ms) { _connect_ms = ms; rearm(); }
    void setIdleTimeout(uint32_t ms) { _idle_ms = ms; rearm(); }
    void setAckTimeout(uint32_t ms) { _ack_ms = ms; rearm(); }
    // once connected, else ignored; keepalive 0 = off, idle_ms is the probe gap too where lwIP allows
    void setNoDelay(bool on);
    void setKeepAlive(uint32_t idle_ms, uint8_t count);

    State state() const { return _state; }
    int8_t error() const { return _err; }   // first one, 0 = none
    // false as soon as the stack drops it, before poll() says why
    bool connected() const { return _state == OPEN && _h.load() != nullptr; }

    EGTcpAddr remoteAddr() const;
    uint16_t remotePort() const;
    EGTcpAddr localAddr() const;
    uint16_t localPort() const;
    uint16_t mss() const;
#ifdef ARDUINO
    IPAddress remoteIP() const { return remoteAddr().ip(); }
    IPAddress localIP() const { return localAddr().ip(); }
#endif

  private:
    friend struct EGTcpCore;
    friend struct EGTcpRun;
    friend struct EGTcpLwip;
    friend class EGTcpServer;

    EGTcpInfo info() const;   // the backend's addresses and mss
    void opened();
    void delivered();
    void acked(uint32_t n);
    bool due(uint32_t now) const;
    void rearm();   // after anything due() reads changes
    void timers();
    void ended(int8_t err);
    void finished();
    void resolved();
    void credit(size_t n, bool flush);
    void forget(bool rst);
    bool dial(const EGTcpAddr& a, uint16_t port);
    void start();

    const EGTcpOps* _ops;

    // stack side writes, under the lock
    std::atomic<void*> _h{nullptr};         // backend's handle, its thread only
    std::atomic<uint16_t> _snd{0};          // send room, the stack's copy
    std::atomic<uint32_t> _rx_len{0};       // bytes waiting
    void* _rx = nullptr;                    // backend's chain
    uint16_t _rx_off = 0;
    EGTcpClient* _next = nullptr;           // ready list
    uint16_t _ev = 0;                       // what happened, bits
    bool _queued = false;
    int8_t _ev_err = 0;
    uint32_t _ev_acked = 0;
    EGTcpAddr _ev_addr;
    uint32_t _ev_dns = 0;
    bool _ev_found = false;
    EGTcpServer* _srv = nullptr;            // accepted, not handed over
    EGTcpAsk* _ask = nullptr;               // pending DNS ask, the stack owns it until it answers
    // owner writes, the stack's poll compares; 0 = nothing timed
    std::atomic<uint32_t> _due_at{0};

    // owner only
    State _state = IDLE;
    int8_t _err = 0;
    bool _ack_later = false;
    uint8_t _life = 0;       // forget() bumps it so old events are ignored
    uint16_t _port = 0;
    uint32_t _dns_id = 0;
    uint32_t _owed = 0;      // taken, not acked yet
    uint32_t _credit = 0;    // acked, not told the stack yet
    uint32_t _unacked = 0;   // written, peer has not acked
    uint32_t _start_ms = 0;
    uint32_t _rx_ms = 0;
    uint32_t _tx_ms = 0;
    uint32_t _connect_ms = 0;
    uint32_t _idle_ms = 0;
    uint32_t _ack_ms = 0;
    EGTcpClient* _accept_next = nullptr;    // server's pull list

    ConnectFn _on_connect = nullptr;
    void* _connect_ctx = nullptr;
    DataFn _on_data = nullptr;
    void* _data_ctx = nullptr;
    SentFn _on_sent = nullptr;
    void* _sent_ctx = nullptr;
    CloseFn _on_close = nullptr;
    void* _close_ctx = nullptr;
    FinFn _on_fin = nullptr;
    void* _fin_ctx = nullptr;
};

class EGTcpServer {
  public:
    EGTcpServer() {}
    ~EGTcpServer();
    EGTcpServer(const EGTcpServer&) = delete;
    EGTcpServer& operator=(const EGTcpServer&) = delete;

    // any addr = all; false = listening already or the stack said no
    __attribute__((warn_unused_result))
    bool begin(uint16_t port, const EGTcpAddr& addr = EGTcpAddr());
    void end();   // open clients stay yours

    // push: the client is yours. Without it, accept() pulls them
    using AcceptFn = void (*)(void* ctx, EGTcpClient* c);
    void onAccept(AcceptFn fn, void* ctx) { _on_accept = fn; _accept_ctx = ctx; }
    EGTcpClient* accept();   // nullptr = none, else yours to delete

    // before begin(); the stack drops SYNs past n open
    void setMaxClients(uint8_t n) { _max = n; }
    void setNoDelay(bool on) { _nodelay = on; }
    bool listening() const { return _h.load() != nullptr; }
    uint16_t port() const { return _port; }

  private:
    friend struct EGTcpRun;
    friend struct EGTcpLwip;

    std::atomic<void*> _h{nullptr};
    uint16_t _port = 0;
    uint8_t _max = 0;   // 0 = stack's default
    bool _nodelay = false;
    EGTcpClient* _pull = nullptr;
    AcceptFn _on_accept = nullptr;
    void* _accept_ctx = nullptr;
};

#endif
