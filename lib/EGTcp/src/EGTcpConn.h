/*
  EGTcpConn.h - one TCP connection, driven from the main loop.

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
#ifndef EGTCPCONN_H
#define EGTCPCONN_H

#include <stddef.h>
#include <stdint.h>
#include "EGTcpLock.h"

class AsyncClient;

// callbacks only queue, the loop does the rest
class EGTcpConn {
  public:
    enum State : uint8_t { IDLE, CONNECTING, OPEN, CLOSED };

    EGTcpConn() {}
    ~EGTcpConn();
    EGTcpConn(const EGTcpConn&) = delete;
    EGTcpConn& operator=(const EGTcpConn&) = delete;

    bool connect(const char* host, uint16_t port);
    void close();                     // unread bytes dropped

    State state();
    int8_t error();                   // first one, 0 = none

    size_t space();                   // write() takes up to this
    size_t write(const void* data, size_t len);   // copies, sends now
    size_t available();
    size_t read(void* out, size_t len);           // window reopens as read
    int peek();                       // -1 = nothing

    // lwIP's numbers
    static const int8_t E_MEM = -1;
    static const int8_t E_CONN = -11;     // never started
    static const int8_t E_ABRT = -13;     // lwIP gave up

  private:
    struct Block {
      Block* next;
      uint16_t len;
      uint16_t off;
      uint8_t bytes[];
    };

    void drop_rx();
    void fail(int8_t err);

    // AsyncTCP task
    void on_connect(AsyncClient* c);
    void on_data(AsyncClient* c, const void* data, size_t len);
    void on_error(AsyncClient* c, int8_t err);
    void on_disconnect(AsyncClient* c);

    EGTcpLock _lock;
    AsyncClient* _client = nullptr;
    State _state = IDLE;
    int8_t _err = 0;
    Block* _head = nullptr;
    Block* _tail = nullptr;
    size_t _rx = 0;
};

#endif
