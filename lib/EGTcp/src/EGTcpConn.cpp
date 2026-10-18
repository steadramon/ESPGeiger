/*
  EGTcpConn.cpp - one TCP connection, driven from the main loop.

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
#include "EGTcpConn.h"

#include <new>
#include <stdlib.h>
#include <string.h>
#include <EGAsyncTCP.h>

EGTcpConn::~EGTcpConn() {
  close();
}

bool EGTcpConn::connect(const char* host, uint16_t port) {
  close();
  AsyncClient* c = new (std::nothrow) AsyncClient();
  if (!c) {
    fail(E_MEM);
    return false;
  }
  c->onConnect([](void* a, AsyncClient* cl) { ((EGTcpConn*)a)->on_connect(cl); }, this);
  c->onData([](void* a, AsyncClient* cl, void* d, size_t n) { ((EGTcpConn*)a)->on_data(cl, d, n); }, this);
  c->onError([](void* a, AsyncClient* cl, int8_t e) { ((EGTcpConn*)a)->on_error(cl, e); }, this);
  c->onDisconnect([](void* a, AsyncClient* cl) { ((EGTcpConn*)a)->on_disconnect(cl); }, this);
  {
    EGTcpHold h(_lock);
    _client = c;
    _state = CONNECTING;
    _err = 0;
  }
  if (c->connect(host, port)) return true;
  // no callback will say so
  {
    EGTcpHold h(_lock);
    _client = nullptr;
    _state = CLOSED;
    if (!_err) _err = E_CONN;
  }
  c->release();
  return false;
}

void EGTcpConn::close() {
  AsyncClient* c;
  {
    EGTcpHold h(_lock);
    c = _client;
    _client = nullptr;
    _state = IDLE;
    _err = 0;
  }
  drop_rx();
  // closes it too
  if (c) c->release();
}

EGTcpConn::State EGTcpConn::state() {
  EGTcpHold h(_lock);
  return _state;
}

int8_t EGTcpConn::error() {
  EGTcpHold h(_lock);
  return _err;
}

size_t EGTcpConn::space() {
  if (state() != OPEN) return 0;
  return _client->space();
}

size_t EGTcpConn::write(const void* data, size_t len) {
  if (state() != OPEN || !len) return 0;
  size_t room = _client->space();
  if (len > room) len = room;
  if (!len) return 0;
  size_t n = _client->add((const char*)data, len, ASYNC_WRITE_FLAG_COPY);
  if (n) _client->send();
  return n;
}

size_t EGTcpConn::available() {
  EGTcpHold h(_lock);
  return _rx;
}

int EGTcpConn::peek() {
  EGTcpHold h(_lock);
  if (!_head) return -1;
  return _head->bytes[_head->off];
}

size_t EGTcpConn::read(void* out, size_t len) {
  uint8_t* dst = (uint8_t*)out;
  size_t got = 0;
  Block* done = nullptr;
  {
    EGTcpHold h(_lock);
    while (got < len && _head) {
      Block* b = _head;
      size_t n = b->len - b->off;
      if (n > len - got) n = len - got;
      memcpy(dst + got, b->bytes + b->off, n);
      b->off += n;
      got += n;
      if (b->off == b->len) {
        _head = b->next;
        if (!_head) _tail = nullptr;
        b->next = done;
        done = b;
      }
    }
    _rx -= got;
  }
  while (done) {
    Block* n = done->next;
    free(done);
    done = n;
  }
  // only the loop swaps _client
  if (got && _client) _client->ack(got);
  return got;
}

void EGTcpConn::drop_rx() {
  Block* b;
  {
    EGTcpHold h(_lock);
    b = _head;
    _head = _tail = nullptr;
    _rx = 0;
  }
  while (b) {
    Block* n = b->next;
    free(b);
    b = n;
  }
}

void EGTcpConn::fail(int8_t err) {
  EGTcpHold h(_lock);
  if (!_err) _err = err;
  _state = CLOSED;
}

void EGTcpConn::on_connect(AsyncClient* c) {
  EGTcpHold h(_lock);
  if (c != _client || _state != CONNECTING) return;
  _state = OPEN;
}

void EGTcpConn::on_data(AsyncClient* c, const void* data, size_t len) {
  // window shut till the loop reads
  c->ackLater();
  Block* b = (Block*)malloc(sizeof(Block) + len);
  if (b) {
    b->next = nullptr;
    b->len = (uint16_t)len;
    b->off = 0;
    memcpy(b->bytes, data, len);
  }
  bool kept = false;
  {
    EGTcpHold h(_lock);
    if (c == _client && _state == OPEN) {
      if (b) {
        if (_tail) _tail->next = b;
        else _head = b;
        _tail = b;
        _rx += len;
        kept = true;
      } else {
        // can't keep it, can't refuse it
        if (!_err) _err = E_MEM;
        _state = CLOSED;
      }
    }
  }
  if (!kept) free(b);
}

void EGTcpConn::on_error(AsyncClient* c, int8_t err) {
  EGTcpHold h(_lock);
  if (c != _client) return;
  if (!_err) _err = err;
  _state = CLOSED;
}

void EGTcpConn::on_disconnect(AsyncClient* c) {
  EGTcpHold h(_lock);
  if (c != _client) return;
  _state = CLOSED;
}
