/*
  EGUdp.h - UDP socket, one owner task.

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
#ifndef EGUDP_H
#define EGUDP_H

/*
  EGTcp's shape: the stack's callback copies a datagram into a free slot,
  the owner's poll() hands it to onPacket. Every call from that one task.
  Slots are taken in begin() and given back in end().
*/

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <functional>
#include "EGTcp.h"   // EGTcpAddr

#ifndef EGUDP_MAX
#define EGUDP_MAX 512   // biggest datagram kept, bigger ones dropped
#endif
#ifndef EGUDP_SLOTS
#define EGUDP_SLOTS 4
#endif

class EGUdp {
  public:
    using PacketFn = std::function<void(const uint8_t* data, size_t len, const EGTcpAddr& from, uint16_t port)>;

    // slots 0 = send only
    explicit EGUdp(uint16_t max = EGUDP_MAX, uint8_t slots = EGUDP_SLOTS);
    ~EGUdp();
    EGUdp(const EGUdp&) = delete;
    EGUdp& operator=(const EGUdp&) = delete;

    bool begin(uint16_t port);   // 0 = any free one
    void end();                  // leaves the group, queued ones dropped

    // one group, any iface = all
    bool join(const EGTcpAddr& group, const EGTcpAddr& iface = EGTcpAddr());
    // after begin(), the 8266 core's beginPacketMulticast()
    void setMulticast(const EGTcpAddr& iface, uint8_t ttl = 1);

    // end() is fine in here, delete is not; data gone once it returns
    void onPacket(PacketFn cb) { _on_packet = cb; }
    // from the owner's loop, cheap when idle
    void poll() {
      if (_count.load(std::memory_order_acquire)) run();
    }

    bool sendTo(const EGTcpAddr& to, uint16_t port, const void* data, size_t len);

    bool listening() const { return _h != nullptr; }
    uint16_t localPort() const { return _port; }
    uint8_t waiting() const { return _count.load(std::memory_order_relaxed); }
    uint32_t tooBig() const { return _big.load(std::memory_order_relaxed); }
    uint32_t full() const { return _full.load(std::memory_order_relaxed); }

  private:
    friend struct EGUdpLwip;

    struct Slot {
      EGTcpAddr from;
      uint16_t port;
      uint16_t len;
    };

    void run();
    void leave();

    const uint16_t _size;   // not _max, Arduino.h has that macro
    const uint8_t _slots;

    void* _h = nullptr;   // backend's pcb
    uint16_t _port = 0;
    EGTcpAddr _group;
    EGTcpAddr _iface;
    bool _joined = false;

    // taken in begin(), the stack fills _tail, the owner reads _head
    Slot* _slot = nullptr;
    uint8_t* _buf = nullptr;   // _slots * _size
    void freeRing();
    uint8_t _head = 0;
    uint8_t _tail = 0;
    std::atomic<uint8_t> _count{0};
    std::atomic<uint32_t> _big{0};
    std::atomic<uint32_t> _full{0};

    // owner only
    uint8_t _life = 0;   // end() bumps
    bool _in_run = false;
    PacketFn _on_packet;
};

#endif
