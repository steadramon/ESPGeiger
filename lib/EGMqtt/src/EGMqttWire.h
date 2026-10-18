/*
  EGMqttWire.h - MQTT 3.1.1 packets, built and parsed. No I/O.

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
#ifndef EGMQTTWIRE_H
#define EGMQTTWIRE_H

#include <stddef.h>
#include <stdint.h>

// builders return the packet size, 0 = bad or no fit; parsers take the body
namespace EGMqttWire {

enum Type : uint8_t {
  CONNECT = 1, CONNACK, PUBLISH, PUBACK, PUBREC, PUBREL, PUBCOMP,
  SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, PINGREQ, PINGRESP, DISCONNECT,
};

static const uint32_t LEN_MAX = 268435455;   // 4 length bytes of 7 bits
static const uint8_t DUP = 0x08;             // in the first byte

inline Type type_of(uint8_t first) { return (Type)(first >> 4); }
inline uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }   // big endian

// fixed header: 2-5 bytes used, 0 = not all here yet, -1 = malformed
int header(const uint8_t* in, size_t avail, uint8_t* first, uint32_t* rem);

// UTF-8 as MQTT wants it: no NUL, no surrogates, shortest form
bool utf8_ok(const uint8_t* s, size_t n);
bool topic_ok(const char* s);    // to publish to: no + or #
bool filter_ok(const char* s);   // to subscribe: + a whole level, # the last
bool matches(const char* filter, const char* topic);   // for routing messages

struct Connect {
  const char* client_id = "";
  const char* user = nullptr;
  const char* pass = nullptr;
  const char* will_topic = nullptr;   // nullptr = no will
  const uint8_t* will_msg = nullptr;
  size_t will_len = 0;
  uint8_t will_qos = 0;
  bool will_retain = false;
  uint16_t keepalive = 60;
  bool clean = true;
};

struct Publish {
  const char* topic = nullptr;   // needn't end in a NUL, topic_len long
  uint16_t topic_len = 0;
  const uint8_t* payload = nullptr;
  size_t len = 0;
  uint16_t id = 0;               // 0 at QoS 0
  uint8_t qos = 0;
  bool retain = false;
  bool dup = false;
};

size_t connect(uint8_t* out, size_t cap, const Connect& c);
size_t connect_size(const Connect& c);   // 0 = bad settings
size_t publish(uint8_t* out, size_t cap, const Publish& p);
size_t subscribe(uint8_t* out, size_t cap, uint16_t id, const char* filter, uint8_t qos);
size_t unsubscribe(uint8_t* out, size_t cap, uint16_t id, const char* filter);
size_t ack(uint8_t* out, size_t cap, Type t, uint16_t id);   // PUBACK PUBREC PUBREL PUBCOMP
size_t bare(uint8_t* out, size_t cap, Type t);               // PINGREQ DISCONNECT

bool parse_publish(uint8_t first, const uint8_t* body, size_t n, Publish* p);
bool publish_head(uint8_t first, uint16_t topic_len, uint16_t id, size_t n);   // all a skipped one has
bool parse_connack(uint8_t first, const uint8_t* body, size_t n, bool* session, uint8_t* code);
bool parse_id(uint8_t first, const uint8_t* body, size_t n, uint16_t* id);   // the acks
bool parse_suback(uint8_t first, const uint8_t* body, size_t n, uint16_t* id, uint8_t* granted);
bool parse_bare(uint8_t first, size_t n);   // PINGRESP

}

#endif
