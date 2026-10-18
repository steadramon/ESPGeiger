/*
  EGMqttWire.cpp - MQTT 3.1.1 packets, built and parsed. No I/O.

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
#include "EGMqttWire.h"

#include <string.h>

namespace EGMqttWire {

static const uint16_t STR_MAX = 0xFFFF;

// Lengths and headers

static size_t len_bytes(uint32_t n) {
  return n < 128 ? 1 : n < 16384 ? 2 : n < 2097152 ? 3 : 4;
}

// 7 bits a byte, low first, top bit = more follow
static void put_len(uint8_t* out, uint32_t n) {
  do {
    uint8_t b = n & 0x7F;
    n >>= 7;
    if (n) b |= 0x80;
    *out++ = b;
  } while (n);
}

int header(const uint8_t* in, size_t avail, uint8_t* first, uint32_t* rem) {
  uint32_t v = 0;
  for (size_t i = 0; i < 4; i++) {
    if (1 + i >= avail) return 0;
    uint8_t b = in[1 + i];
    if (i && b == 0) return -1;   // a longer form than needed
    v |= (uint32_t)(b & 0x7F) << (7 * i);
    if (!(b & 0x80)) {
      *first = in[0];
      *rem = v;
      return (int)(2 + i);
    }
  }
  return -1;   // a 5th length byte
}

// the fixed header, 0 = the whole packet won't fit
static size_t start(uint8_t* out, size_t cap, uint8_t first, uint32_t rem) {
  if (rem > LEN_MAX) return 0;
  size_t h = 1 + len_bytes(rem);
  if (rem > cap || h > cap - rem) return 0;
  out[0] = first;
  put_len(out + 1, rem);
  return h;
}

static uint8_t* put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
  return p + 2;
}

static uint8_t* put_str(uint8_t* p, const void* s, size_t n) {
  p = put16(p, (uint16_t)n);
  if (n) memcpy(p, s, n);
  return p + n;
}

// Text

bool utf8_ok(const uint8_t* s, size_t n) {
  size_t i = 0;
  while (i < n) {
    uint8_t c = s[i];
    if (c == 0) return false;
    if (c < 0x80) {
      i++;
      continue;
    }
    size_t more;
    uint32_t cp, least;
    if ((c & 0xE0) == 0xC0)      { more = 1; cp = c & 0x1F; least = 0x80; }
    else if ((c & 0xF0) == 0xE0) { more = 2; cp = c & 0x0F; least = 0x800; }
    else if ((c & 0xF8) == 0xF0) { more = 3; cp = c & 0x07; least = 0x10000; }
    else return false;
    if (more >= n - i) return false;
    for (size_t j = 1; j <= more; j++) {
      uint8_t b = s[i + j];
      if ((b & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += 1 + more;
  }
  return true;
}

static bool text_ok(const char* s, size_t* n) {
  if (!s) return false;
  *n = strlen(s);
  return *n > 0 && *n <= STR_MAX && utf8_ok((const uint8_t*)s, *n);
}

static bool topic_ok(const char* s, size_t n) {
  return n && n <= STR_MAX && utf8_ok((const uint8_t*)s, n) &&
         !memchr(s, '+', n) && !memchr(s, '#', n);
}

bool topic_ok(const char* s) {
  return s && topic_ok(s, strlen(s));
}

// each level is text, a whole +, or a # that ends it
bool filter_ok(const char* s) {
  size_t n;
  if (!text_ok(s, &n)) return false;
  for (size_t i = 0; i < n; i++) {
    bool level_start = i == 0 || s[i - 1] == '/';
    if (s[i] == '#' && (!level_start || i != n - 1)) return false;
    if (s[i] == '+' && (!level_start || (i + 1 < n && s[i + 1] != '/'))) return false;
  }
  return true;
}

// one level each, both left on the '/' or the end; false = they differ
static bool same_level(const char** f, const char** t) {
  if (**f == '+') {
    (*f)++;
    while (**t && **t != '/') (*t)++;
    return true;
  }
  while (**f && **f != '/' && **f == **t) {
    (*f)++;
    (*t)++;
  }
  return (!**f || **f == '/') && (!**t || **t == '/');
}

bool matches(const char* f, const char* t) {
  if (*t == '$' && (*f == '+' || *f == '#')) return false;   // $SYS isn't in a wildcard
  for (;;) {
    if (*f == '#') return true;
    if (!same_level(&f, &t)) return false;
    if (!*f && !*t) return true;
    if (*f != '/' || *t != '/') return *f == '/' && f[1] == '#' && !f[2];   // "a/#" takes "a"
    f++;
    t++;
  }
}

// Building

// a string's 2 + its length onto rem, false = too long or not text
static bool field(const char* s, bool text, uint32_t* rem) {
  size_t n = strlen(s);
  if (n > STR_MAX || (text && !utf8_ok((const uint8_t*)s, n))) return false;
  *rem += 2 + (uint32_t)n;
  return true;
}

static bool will_ok(const Connect& c) {
  return topic_ok(c.will_topic) && c.will_len <= STR_MAX && c.will_qos <= 2 &&
         (c.will_msg || !c.will_len);
}

// the CONNECT's remaining length, 0 = settings no broker would take
static uint32_t connect_rem(const Connect& c) {
  const char* id = c.client_id ? c.client_id : "";
  uint32_t rem = 10;
  if (!field(id, true, &rem) || (!id[0] && !c.clean)) return 0;
  if (c.will_topic) {
    if (!will_ok(c)) return 0;
    rem += 2 + (uint32_t)strlen(c.will_topic) + 2 + (uint32_t)c.will_len;
  }
  if (c.user && !field(c.user, true, &rem)) return 0;
  if (c.pass && (!c.user || !field(c.pass, false, &rem))) return 0;   // no password without a user
  return rem;
}

size_t connect_size(const Connect& c) {
  uint32_t rem = connect_rem(c);
  return rem ? 1 + len_bytes(rem) + rem : 0;
}

// name and level, flags, keepalive, then id, will, user, password as set
size_t connect(uint8_t* out, size_t cap, const Connect& c) {
  uint32_t rem = connect_rem(c);
  size_t h = rem ? start(out, cap, CONNECT << 4, rem) : 0;
  if (!h) return 0;
  bool will = c.will_topic != nullptr;
  bool user = c.user != nullptr;
  bool pass = c.pass != nullptr;

  uint8_t flags = c.clean ? 0x02 : 0;
  if (will) flags |= 0x04 | (uint8_t)(c.will_qos << 3) | (c.will_retain ? 0x20 : 0);
  if (pass) flags |= 0x40;
  if (user) flags |= 0x80;
  static const uint8_t NAME_LEVEL[] = { 0, 4, 'M', 'Q', 'T', 'T', 4 };
  uint8_t* p = out + h;
  memcpy(p, NAME_LEVEL, sizeof(NAME_LEVEL));
  p += sizeof(NAME_LEVEL);
  *p++ = flags;
  p = put16(p, c.keepalive);
  p = put_str(p, c.client_id, c.client_id ? strlen(c.client_id) : 0);
  if (will) {
    p = put_str(p, c.will_topic, strlen(c.will_topic));
    p = put_str(p, c.will_msg, c.will_len);
  }
  if (user) p = put_str(p, c.user, strlen(c.user));
  if (pass) put_str(p, c.pass, strlen(c.pass));
  return h + rem;
}

static bool publish_ok(const Publish& m) {
  return m.topic && topic_ok(m.topic, m.topic_len) && m.qos <= 2 && (m.id || !m.qos) &&
         (m.payload || !m.len) && m.len <= LEN_MAX;
}

size_t publish(uint8_t* out, size_t cap, const Publish& m) {
  if (!publish_ok(m)) return 0;
  uint32_t rem = (uint32_t)(2 + m.topic_len + (m.qos ? 2 : 0) + m.len);   // under 2^32, start() caps it
  uint8_t first = (uint8_t)((PUBLISH << 4) | (m.qos << 1) | (m.retain ? 1 : 0));
  if (m.dup && m.qos) first |= DUP;
  size_t h = start(out, cap, first, rem);
  if (!h) return 0;
  uint8_t* p = put_str(out + h, m.topic, m.topic_len);
  if (m.qos) p = put16(p, m.id);
  if (m.len) memmove(p, m.payload, m.len);   // may already sit in out, a little higher
  return h + rem;
}

size_t subscribe(uint8_t* out, size_t cap, uint16_t id, const char* filter, uint8_t qos) {
  if (!id || qos > 2 || !filter_ok(filter)) return 0;
  size_t fn = strlen(filter);
  size_t h = start(out, cap, (SUBSCRIBE << 4) | 0x02, (uint32_t)(2 + 2 + fn + 1));
  if (!h) return 0;
  uint8_t* p = put16(out + h, id);
  p = put_str(p, filter, fn);
  *p = qos;
  return h + 2 + 2 + fn + 1;
}

size_t unsubscribe(uint8_t* out, size_t cap, uint16_t id, const char* filter) {
  if (!id || !filter_ok(filter)) return 0;
  size_t fn = strlen(filter);
  size_t h = start(out, cap, (UNSUBSCRIBE << 4) | 0x02, (uint32_t)(2 + 2 + fn));
  if (!h) return 0;
  put_str(put16(out + h, id), filter, fn);
  return h + 2 + 2 + fn;
}

size_t ack(uint8_t* out, size_t cap, Type t, uint16_t id) {
  if (t < PUBACK || t > PUBCOMP || !id) return 0;
  size_t h = start(out, cap, (uint8_t)((t << 4) | (t == PUBREL ? 0x02 : 0)), 2);
  if (!h) return 0;
  put16(out + h, id);
  return h + 2;
}

size_t bare(uint8_t* out, size_t cap, Type t) {
  if (t != PINGREQ && t != DISCONNECT) return 0;
  return start(out, cap, (uint8_t)(t << 4), 0);
}

// Parsing

bool publish_head(uint8_t first, uint16_t topic_len, uint16_t id, size_t n) {
  uint8_t qos = (first >> 1) & 3;
  if (type_of(first) != PUBLISH || qos == 3 || (!qos && (first & DUP))) return false;
  size_t head = 2 + (size_t)topic_len + (qos ? 2 : 0);
  return topic_len && head <= n && (id || !qos);
}

bool parse_publish(uint8_t first, const uint8_t* body, size_t n, Publish* p) {
  if (n < 2) return false;
  uint8_t qos = (first >> 1) & 3;
  uint16_t tn = get16(body);
  size_t head = 2 + (size_t)tn + (qos ? 2 : 0);
  uint16_t id = qos && head <= n ? get16(body + 2 + tn) : 0;
  if (!publish_head(first, tn, id, n)) return false;
  const uint8_t* t = body + 2;
  if (!utf8_ok(t, tn) || memchr(t, '+', tn) || memchr(t, '#', tn)) return false;
  p->topic = (const char*)t;
  p->topic_len = tn;
  p->id = id;
  p->payload = body + head;
  p->len = n - head;
  p->qos = qos;
  p->retain = first & 1;
  p->dup = (first & DUP) != 0;
  return true;
}

bool parse_connack(uint8_t first, const uint8_t* body, size_t n, bool* session, uint8_t* code) {
  if (first != (CONNACK << 4) || n != 2 || (body[0] & 0xFE)) return false;
  *session = body[0] & 1;
  *code = body[1];
  return !(*session && *code);   // a refusal has no session
}

bool parse_id(uint8_t first, const uint8_t* body, size_t n, uint16_t* id) {
  Type t = type_of(first);
  bool known = t == PUBACK || t == PUBREC || t == PUBREL || t == PUBCOMP || t == UNSUBACK;
  uint8_t flags = t == PUBREL ? 0x02 : 0;
  if (!known || (first & 0x0F) != flags || n != 2) return false;
  *id = get16(body);
  return *id != 0;
}

bool parse_suback(uint8_t first, const uint8_t* body, size_t n, uint16_t* id, uint8_t* granted) {
  if (first != (SUBACK << 4) || n != 3) return false;   // one filter a subscribe
  *id = get16(body);
  *granted = body[2];
  return *id && (*granted <= 2 || *granted == 0x80);
}

bool parse_bare(uint8_t first, size_t n) {
  return first == (PINGRESP << 4) && n == 0;
}

}
