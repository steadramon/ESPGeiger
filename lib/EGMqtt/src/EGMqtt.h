/*
  EGMqtt.h - MQTT 3.1.1 client, run from the main loop.

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
#ifndef EGMQTT_H
#define EGMQTT_H

#include <stddef.h>
#include <stdint.h>
#include <EGTcpConn.h>
#include "EGMqttWire.h"

class __FlashStringHelper;   // F("...")

#ifndef EGMQTT_OUT_CAP
  #define EGMQTT_OUT_CAP 1536   // packets waiting to go or to be acked
#endif
#ifndef EGMQTT_IN_CAP
  #define EGMQTT_IN_CAP 512     // biggest packet taken in, bigger are dropped
#endif
#ifndef EGMQTT_QOS2_IN
  #define EGMQTT_QOS2_IN 20     // QoS 2 ids held till PUBREL
#endif
#ifndef EGMQTT_STR_CAP
  #define EGMQTT_STR_CAP 128    // longest host, id, user, password, will topic, will
#endif

// all from loop(), buffers kept till stop(); QoS 1 and 2 kept till acked
class EGMqtt {
  public:
    enum State : uint8_t { DISCONNECTED, CONNECTING, CONNECTED };

    // why it last went down, 1-5 are the broker's CONNACK refusals
    enum Error : int8_t {
      OK = 0, BAD_PROTOCOL = 1, ID_REJECTED = 2, SERVER_UNAVAILABLE = 3,
      BAD_CREDENTIALS = 4, NOT_AUTHORIZED = 5,
      TCP_FAILED = -1, CONNECTION_LOST = -2, TIMEOUT = -3, PROTOCOL = -4,
      NO_MEMORY = -5,
    };

    struct Message {
      const char* topic;       // NUL ended
      const uint8_t* payload;  // NUL ended too, not counted in len
      size_t len;
      uint8_t qos;
      bool retain;
      bool dup;
    };

    enum AckOf : uint8_t { ACK_PUBLISH, ACK_SUBSCRIBE, ACK_UNSUBSCRIBE };
    struct Ack {
      uint16_t id;       // what publish() / subscribe() / unsubscribe() gave
      AckOf of;
      uint8_t granted;   // QoS 0-2, 0x80 = refused or lost
    };

    // all from loop(); they may call anything here, connect() too
    typedef void (*OnConnect)(void* arg, bool session_present);
    typedef void (*OnDisconnect)(void* arg, int8_t error);   // not after disconnect()
    typedef void (*OnMessage)(void* arg, const Message& m);
    typedef void (*OnAck)(void* arg, const Ack& a);

    EGMqtt() { sized(); }
    ~EGMqtt();
    EGMqtt(const EGMqtt&) = delete;
    EGMqtt& operator=(const EGMqtt&) = delete;

    // copied, used from the next connect; false = too long or no room
    bool setServer(const char* host, uint16_t port = 1883);
    bool setClientId(const char* id);
    bool setCredentials(const char* user, const char* pass = nullptr);   // no pass without a user
    bool setWill(const char* topic, const char* msg, uint8_t qos = 0, bool retain = false);
    bool setWill(const char* topic, const __FlashStringHelper* msg, uint8_t qos = 0, bool retain = false);
    void clearWill();
    void setKeepAlive(uint16_t secs) { _keepalive = secs; }   // 0 = off
    void setCleanSession(bool clean) { _clean = clean; }
    void setTimeout(uint16_t secs) { _timeoutS = secs; }      // connect and ping replies
    void onConnect(OnConnect fn, void* arg = nullptr) { _onUp = fn; _onUpArg = arg; }
    void onDisconnect(OnDisconnect fn, void* arg = nullptr) { _onDown = fn; _onDownArg = arg; }
    void onMessage(OnMessage fn, void* arg = nullptr) { _onMsg = fn; _onMsgArg = arg; }
    void onAck(OnAck fn, void* arg = nullptr) { _onAck = fn; _onAckArg = arg; }

    bool connect();
    void disconnect();   // DISCONNECT, then closed; the will isn't sent
    void stop();         // drops the queue and settings, frees all
    void loop();

    State state() const { return _state; }
    bool connected() const { return _state == CONNECTED; }
    int8_t lastError() const { return _err; }
    bool sessionPresent() const { return _session; }

    // 0 = refused (bad topic, full, QoS 0 while down), QoS 0 gives 1
    uint16_t publish(const char* topic, const uint8_t* payload, size_t len,
                     uint8_t qos = 0, bool retain = false);
    uint16_t publish(const char* topic, const char* payload, uint8_t qos = 0, bool retain = false);
    // payload from flash, copied into the outbox
    uint16_t publish(const char* topic, const __FlashStringHelper* payload,
                     uint8_t qos = 0, bool retain = false);

    // payload written in place, nothing else on this client till end
    uint8_t* publish_begin(const char* topic, uint8_t qos, bool retain, size_t* cap);
    uint16_t publish_end(size_t len);   // as publish(), 0 = refused
    uint16_t subscribe(const char* filter, uint8_t qos = 0);   // connected only
    uint16_t unsubscribe(const char* filter);

    size_t queued() const;          // QoS 1 and 2 not acked yet
    size_t room() const;            // a publish this big still fits
    uint32_t dropped() const { return _dropped; }   // over EGMQTT_IN_CAP

    static const size_t OUT_CAP = EGMQTT_OUT_CAP;
    static const size_t IN_CAP = EGMQTT_IN_CAP;

  private:
    enum Phase : uint8_t { IDLE, DIAL, HELLO, UP };   // HELLO = CONNECT sent, no CONNACK yet
    // from K_PUB1 on they're kept till acked
    enum Kind : uint8_t { K_CONNECT, K_ONCE, K_PING, K_PUB1, K_PUB2, K_REL, K_SUB, K_UNSUB };
    enum Sent : uint8_t { S_NEW, S_SENT, S_DONE, S_LOST };   // S_LOST = told from events()
    enum Rx : uint8_t { R_HEAD, R_BODY, R_SKIP };

    // an outbox entry, the packet follows it
    struct Rec {
      uint32_t at;   // when written, or since its write stalled; 0 = neither
      uint16_t len;
      uint16_t id;
      uint8_t kind;
      uint8_t sent;
    };

    struct Mem {
      alignas(4) uint8_t out[EGMQTT_OUT_CAP];
      uint8_t in[EGMQTT_IN_CAP + 1];   // + the payload's NUL
    };

    // settings, one block
    enum Field : uint8_t { F_HOST, F_ID, F_USER, F_PASS, F_WILL_TOPIC, F_WILL_MSG, F_COUNT };

    bool mem();
    const char* str(Field f) const { return _strs ? _strs + _at[f] : ""; }
    bool set_str(Field f, const char* v, Field g = F_COUNT, const char* v2 = nullptr);
    EGMqttWire::Connect conf() const;
    static size_t needs(const EGMqttWire::Connect& c);
    bool fits(const EGMqttWire::Connect& c) const;

    // outbox
    Rec* rec(size_t off) const;
    size_t next(size_t off) const;
    uint8_t* tail(size_t* cap, bool keep_reserve);
    void commit(Kind k, uint16_t id, size_t len);
    void queue(EGMqttWire::Type t, uint16_t id);
    Rec* find(Kind k, uint16_t id);
    Rec* find(Kind k);
    uint16_t next_id();
    void flush();
    void compact();
    void rewind();
    void settle();
    bool acks_fit() const;
    bool stuck(uint32_t now);

    // the loop's steps
    void dial(uint32_t now);
    void timers(uint32_t now);
    void receive();
    bool head();
    void body();
    void skip();
    void pick(uint32_t at, uint8_t b);
    void handle();
    void on_connack();
    void on_publish();
    void on_ack();
    void deliver(const EGMqttWire::Publish& p);
    bool q2_known(uint16_t id);
    void q2_drop(uint16_t id);
    void done(Kind k, uint16_t id, AckOf of, uint8_t granted);
    void note(uint16_t id, AckOf of, uint8_t granted);
    void end(int8_t err);
    void events();
    void lost();
    void sized();

    EGTcpConn _tcp;
    Mem* _m = nullptr;
    char* _strs = nullptr;
    uint16_t _at[F_COUNT] = {};

    uint8_t* _pend = nullptr;    // cleared when the outbox moves
    size_t _pendCap = 0;
    const char* _pendTopic = nullptr;
    uint8_t _pendQos = 0;
    bool _pendRetain = false;
    uint16_t _port = 1883;
    bool _will = false;
    uint8_t _willQos = 0;
    bool _willRetain = false;
    bool _hasUser = false;
    bool _hasPass = false;
    uint16_t _keepalive = 60;
    bool _clean = true;
    uint16_t _timeoutS = 10;
    OnConnect _onUp = nullptr;
    void* _onUpArg = nullptr;
    OnDisconnect _onDown = nullptr;
    void* _onDownArg = nullptr;
    OnMessage _onMsg = nullptr;
    void* _onMsgArg = nullptr;
    OnAck _onAck = nullptr;
    void* _onAckArg = nullptr;

    State _state = DISCONNECTED;
    Phase _phase = IDLE;
    int8_t _err = OK;
    bool _session = false;
    bool _upNew = false;
    bool _downNew = false;
    bool _lostNew = false;
    uint32_t _phaseMs = 0;
    uint32_t _txMs = 0;        // last packet out
    uint32_t _rxMs = 0;        // last packet in
    bool _pingOut = false;     // queued, no PINGRESP yet
    bool _pingSent = false;    // written, the deadline runs
    uint32_t _pingMs = 0;

    size_t _outLen = 0;     // used bytes of out
    size_t _txOff = 0;      // into the record being written
    size_t _reserve = 0;    // kept for CONNECT and acks
    uint16_t _lastId = 0;

    Rx _rx = R_HEAD;
    uint8_t _hdr[5];
    uint8_t _hdrLen = 0;
    uint8_t _first = 0;
    uint32_t _rem = 0;
    uint32_t _got = 0;
    uint16_t _bigTopic = 0;    // picked out of a too big PUBLISH
    uint16_t _bigId = 0;
    uint32_t _dropped = 0;

    uint16_t _q2[EGMQTT_QOS2_IN] = {};
};

#endif
