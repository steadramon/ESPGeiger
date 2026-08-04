/*
  test_egmqttwire - MQTT 3.1.1 packets, built and parsed.

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

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <EGMqttWire.h>

using namespace EGMqttWire;

void setUp(void) {}
void tearDown(void) {}

static uint8_t buf[600];

#define BYTES(...) do { \
    static const uint8_t want[] = { __VA_ARGS__ }; \
    TEST_ASSERT_EQUAL_UINT(sizeof(want), n); \
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, sizeof(want)); \
  } while (0)

// ---- lengths

static void lengths(void) {
  struct { uint32_t v; uint8_t b[4]; int n; } cases[] = {
    { 0,         { 0x00 }, 1 },
    { 127,       { 0x7F }, 1 },
    { 128,       { 0x80, 0x01 }, 2 },
    { 16383,     { 0xFF, 0x7F }, 2 },
    { 16384,     { 0x80, 0x80, 0x01 }, 3 },
    { 2097151,   { 0xFF, 0xFF, 0x7F }, 3 },
    { 2097152,   { 0x80, 0x80, 0x80, 0x01 }, 4 },
    { LEN_MAX,   { 0xFF, 0xFF, 0xFF, 0x7F }, 4 },
  };
  for (auto& c : cases) {
    uint8_t in[5] = { 0x30 };
    memcpy(in + 1, c.b, c.n);
    uint8_t first;
    uint32_t rem;
    TEST_ASSERT_EQUAL_INT(1 + c.n, header(in, 1 + c.n, &first, &rem));
    TEST_ASSERT_EQUAL_UINT32(c.v, rem);
    TEST_ASSERT_EQUAL_HEX8(0x30, first);
    TEST_ASSERT_EQUAL_INT(0, header(in, c.n, &first, &rem));   // one short
  }
}

static void bad_lengths(void) {
  uint8_t first;
  uint32_t rem;
  const uint8_t five[] = { 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
  TEST_ASSERT_EQUAL_INT(-1, header(five, sizeof(five), &first, &rem));
  const uint8_t loose[] = { 0x30, 0x80, 0x00 };   // 0 in two bytes
  TEST_ASSERT_EQUAL_INT(-1, header(loose, sizeof(loose), &first, &rem));
  TEST_ASSERT_EQUAL_INT(0, header(five, 1, &first, &rem));
}

// ---- text

static void utf8(void) {
  const uint8_t ok[] = { 'a', 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80 };
  TEST_ASSERT_TRUE(utf8_ok(ok, sizeof(ok)));
  const uint8_t nul[] = { 'a', 0 };
  const uint8_t overlong[] = { 0xC0, 0x80 };
  const uint8_t surrogate[] = { 0xED, 0xA0, 0x80 };
  const uint8_t too_high[] = { 0xF4, 0x90, 0x80, 0x80 };
  const uint8_t cut[] = { 0xE2, 0x82 };
  const uint8_t lone[] = { 0x80 };
  TEST_ASSERT_FALSE(utf8_ok(nul, sizeof(nul)));
  TEST_ASSERT_FALSE(utf8_ok(overlong, sizeof(overlong)));
  TEST_ASSERT_FALSE(utf8_ok(surrogate, sizeof(surrogate)));
  TEST_ASSERT_FALSE(utf8_ok(too_high, sizeof(too_high)));
  TEST_ASSERT_FALSE(utf8_ok(cut, sizeof(cut)));
  TEST_ASSERT_FALSE(utf8_ok(lone, sizeof(lone)));
}

static void topics(void) {
  TEST_ASSERT_TRUE(topic_ok("a/b"));
  TEST_ASSERT_TRUE(topic_ok("/"));
  TEST_ASSERT_FALSE(topic_ok(""));
  TEST_ASSERT_FALSE(topic_ok("a/+"));
  TEST_ASSERT_FALSE(topic_ok("a/#"));
  TEST_ASSERT_FALSE(topic_ok(nullptr));
}

static void filters(void) {
  const char* good[] = { "#", "+", "a/#", "+/+", "a/+/b", "/+", "+/", "$SYS/#" };
  const char* bad[] = { "", "a#", "a/#/b", "a+", "+a", "a/b+", "##" };
  for (const char* f : good) TEST_ASSERT_TRUE_MESSAGE(filter_ok(f), f);
  for (const char* f : bad) TEST_ASSERT_FALSE_MESSAGE(filter_ok(f), f);
}

static void matching(void) {
  struct { const char* f; const char* t; bool m; } cases[] = {
    { "a/b",   "a/b",     true },
    { "a/b",   "a/c",     false },
    { "a/+",   "a/b",     true },
    { "a/+",   "a/b/c",   false },
    { "a/+",   "a/",      true },
    { "+/b",   "/b",      true },
    { "a/#",   "a/b/c",   true },
    { "a/#",   "a",       true },
    { "a/#",   "ab",      false },
    { "#",     "x/y",     true },
    { "#",     "$SYS/x",  false },
    { "+/x",   "$SYS/x",  false },
    { "$SYS/#", "$SYS/x", true },
    { "a",     "ab",      false },
    { "ab",    "a",       false },
    { "a/b",   "a",       false },
  };
  for (auto& c : cases) {
    char msg[40];
    snprintf(msg, sizeof(msg), "%s ~ %s", c.f, c.t);
    TEST_ASSERT_EQUAL_MESSAGE(c.m, matches(c.f, c.t), msg);
  }
}

// ---- building

static void connect_min(void) {
  Connect c;
  c.client_id = "dev1";
  size_t n = connect(buf, sizeof(buf), c);
  BYTES(0x10, 16, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 60, 0, 4, 'd', 'e', 'v', '1');
  TEST_ASSERT_EQUAL_UINT(n, connect_size(c));
}

static void connect_full(void) {
  Connect c;
  c.client_id = "id";
  c.user = "u";
  c.pass = "pw";
  c.will_topic = "w";
  c.will_msg = (const uint8_t*)"x";
  c.will_len = 1;
  c.will_qos = 1;
  c.will_retain = true;
  c.keepalive = 0x0102;
  c.clean = false;
  size_t n = connect(buf, sizeof(buf), c);
  // user, password, retain, QoS 1, will, not clean
  BYTES(0x10, 27, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xEC, 1, 2, 0, 2, 'i', 'd',
        0, 1, 'w', 0, 1, 'x', 0, 1, 'u', 0, 2, 'p', 'w');
  TEST_ASSERT_EQUAL_UINT(n, connect_size(c));
}

static void connect_refused(void) {
  Connect c;
  c.client_id = "id";
  c.pass = "pw";   // no user
  TEST_ASSERT_EQUAL_UINT(0, connect(buf, sizeof(buf), c));
  TEST_ASSERT_EQUAL_UINT(0, connect_size(c));
  c.pass = nullptr;
  c.will_topic = "w/#";
  TEST_ASSERT_EQUAL_UINT(0, connect(buf, sizeof(buf), c));
  c.will_topic = nullptr;
  c.client_id = "";
  c.clean = false;   // a broker can't keep a session for no one
  TEST_ASSERT_EQUAL_UINT(0, connect(buf, sizeof(buf), c));
  c.client_id = "id";
  TEST_ASSERT_EQUAL_UINT(0, connect(buf, 10, c));   // won't fit
}

static Publish msg(const char* topic, const char* payload, uint8_t qos = 0, uint16_t id = 0) {
  Publish m;
  m.topic = topic;
  m.topic_len = (uint16_t)strlen(topic);
  m.payload = (const uint8_t*)payload;
  m.len = strlen(payload);
  m.qos = qos;
  m.id = id;
  return m;
}

static void publish_qos0(void) {
  size_t n = publish(buf, sizeof(buf), msg("a/b", "hi"));
  BYTES(0x30, 7, 0, 3, 'a', '/', 'b', 'h', 'i');
}

static void publish_qos1(void) {
  Publish m = msg("a/b", "hi", 1, 10);
  m.retain = true;
  size_t n = publish(buf, sizeof(buf), m);
  BYTES(0x33, 9, 0, 3, 'a', '/', 'b', 0, 10, 'h', 'i');
  m.dup = true;
  n = publish(buf, sizeof(buf), m);
  BYTES(0x3B, 9, 0, 3, 'a', '/', 'b', 0, 10, 'h', 'i');
}

static void publish_refused(void) {
  TEST_ASSERT_EQUAL_UINT(0, publish(buf, sizeof(buf), msg("a/+", "x")));
  TEST_ASSERT_EQUAL_UINT(0, publish(buf, sizeof(buf), msg("a", "x", 1, 0)));   // QoS 1, no id
  TEST_ASSERT_EQUAL_UINT(0, publish(buf, sizeof(buf), msg("a", "x", 3, 1)));
  TEST_ASSERT_EQUAL_UINT(0, publish(buf, sizeof(buf), msg("", "x")));
  TEST_ASSERT_EQUAL_UINT(0, publish(buf, 5, msg("a", "x")));   // needs 6
  TEST_ASSERT_EQUAL_UINT(6, publish(buf, 6, msg("a", "x")));
  Publish d = msg("a", "x");
  d.dup = true;   // no DUP at QoS 0
  size_t n = publish(buf, sizeof(buf), d);
  BYTES(0x30, 4, 0, 1, 'a', 'x');
}

static void publish_big(void) {
  static char big[301];
  memset(big, 'z', 300);
  size_t n = publish(buf, sizeof(buf), msg("t", big));
  TEST_ASSERT_EQUAL_UINT(1 + 2 + 3 + 300, n);
  TEST_ASSERT_EQUAL_HEX8(0x30, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAF, buf[1]);   // 303: low 7 bits and more, then 2
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);
}

static void subscribe_one(void) {
  size_t n = subscribe(buf, sizeof(buf), 1, "a/+", 1);
  BYTES(0x82, 8, 0, 1, 0, 3, 'a', '/', '+', 1);
  TEST_ASSERT_EQUAL_UINT(0, subscribe(buf, sizeof(buf), 0, "a", 0));
  TEST_ASSERT_EQUAL_UINT(0, subscribe(buf, sizeof(buf), 1, "a#", 0));
  TEST_ASSERT_EQUAL_UINT(0, subscribe(buf, sizeof(buf), 1, "a", 3));
}

static void unsubscribe_one(void) {
  size_t n = unsubscribe(buf, sizeof(buf), 2, "a/+");
  BYTES(0xA2, 7, 0, 2, 0, 3, 'a', '/', '+');
}

static void acks(void) {
  size_t n = ack(buf, sizeof(buf), PUBACK, 5);
  BYTES(0x40, 2, 0, 5);
  n = ack(buf, sizeof(buf), PUBREL, 0x1234);
  BYTES(0x62, 2, 0x12, 0x34);
  TEST_ASSERT_EQUAL_UINT(0, ack(buf, sizeof(buf), PUBACK, 0));
  TEST_ASSERT_EQUAL_UINT(0, ack(buf, sizeof(buf), SUBACK, 1));
  n = bare(buf, sizeof(buf), PINGREQ);
  BYTES(0xC0, 0);
  n = bare(buf, sizeof(buf), DISCONNECT);
  BYTES(0xE0, 0);
}

// ---- parsing

static void connacks(void) {
  bool s;
  uint8_t code;
  const uint8_t ok[] = { 0, 0 };
  TEST_ASSERT_TRUE(parse_connack(0x20, ok, 2, &s, &code));
  TEST_ASSERT_FALSE(s);
  TEST_ASSERT_EQUAL_UINT8(0, code);
  const uint8_t kept[] = { 1, 0 };
  TEST_ASSERT_TRUE(parse_connack(0x20, kept, 2, &s, &code));
  TEST_ASSERT_TRUE(s);
  const uint8_t no[] = { 0, 5 };
  TEST_ASSERT_TRUE(parse_connack(0x20, no, 2, &s, &code));
  TEST_ASSERT_EQUAL_UINT8(5, code);
  const uint8_t odd[] = { 1, 5 };      // refused but a session
  const uint8_t reserved[] = { 2, 0 };
  TEST_ASSERT_FALSE(parse_connack(0x20, odd, 2, &s, &code));
  TEST_ASSERT_FALSE(parse_connack(0x20, reserved, 2, &s, &code));
  TEST_ASSERT_FALSE(parse_connack(0x21, ok, 2, &s, &code));
  TEST_ASSERT_FALSE(parse_connack(0x20, ok, 1, &s, &code));
}

static void publishes(void) {
  Publish p;
  const uint8_t q1[] = { 0, 3, 'a', '/', 'b', 0, 7, 'h', 'i' };
  TEST_ASSERT_TRUE(parse_publish(0x3B, q1, sizeof(q1), &p));   // dup, QoS 1, retain
  TEST_ASSERT_EQUAL_UINT16(3, p.topic_len);
  TEST_ASSERT_EQUAL_MEMORY("a/b", p.topic, 3);
  TEST_ASSERT_EQUAL_UINT16(7, p.id);
  TEST_ASSERT_EQUAL_UINT(2, p.len);
  TEST_ASSERT_EQUAL_MEMORY("hi", p.payload, 2);
  TEST_ASSERT_TRUE(p.retain);
  TEST_ASSERT_TRUE(p.dup);
  TEST_ASSERT_EQUAL_UINT8(1, p.qos);

  const uint8_t q0[] = { 0, 1, 't' };   // empty payload
  TEST_ASSERT_TRUE(parse_publish(0x30, q0, sizeof(q0), &p));
  TEST_ASSERT_EQUAL_UINT(0, p.len);
  TEST_ASSERT_EQUAL_UINT16(0, p.id);
}

static void bad_publishes(void) {
  Publish p;
  const uint8_t ok[] = { 0, 1, 't', 0, 1 };
  TEST_ASSERT_FALSE(parse_publish(0x36, ok, sizeof(ok), &p));   // QoS 3
  TEST_ASSERT_FALSE(parse_publish(0x38, ok, sizeof(ok), &p));   // DUP at QoS 0
  TEST_ASSERT_FALSE(parse_publish(0x32, ok, 4, &p));            // id cut short
  const uint8_t over[] = { 0, 9, 't' };                         // topic runs past the end
  TEST_ASSERT_FALSE(parse_publish(0x30, over, sizeof(over), &p));
  const uint8_t empty[] = { 0, 0, 'x' };
  TEST_ASSERT_FALSE(parse_publish(0x30, empty, sizeof(empty), &p));
  const uint8_t wild[] = { 0, 1, '#' };
  TEST_ASSERT_FALSE(parse_publish(0x30, wild, sizeof(wild), &p));
  const uint8_t no_id[] = { 0, 1, 't', 0, 0 };
  TEST_ASSERT_FALSE(parse_publish(0x32, no_id, sizeof(no_id), &p));
  const uint8_t nul[] = { 0, 2, 'a', 0 };
  TEST_ASSERT_FALSE(parse_publish(0x30, nul, sizeof(nul), &p));
  TEST_ASSERT_FALSE(parse_publish(0x30, ok, 1, &p));
}

static void heads(void) {
  TEST_ASSERT_TRUE(publish_head(0x30, 1, 0, 600));
  TEST_ASSERT_TRUE(publish_head(0x3A, 1, 7, 600));   // dup, QoS 1
  TEST_ASSERT_FALSE(publish_head(0x20, 1, 0, 600));  // not a PUBLISH
  TEST_ASSERT_FALSE(publish_head(0x36, 1, 7, 600));  // QoS 3
  TEST_ASSERT_FALSE(publish_head(0x38, 1, 0, 600));  // DUP at QoS 0
  TEST_ASSERT_FALSE(publish_head(0x30, 0, 0, 600));  // no topic
  TEST_ASSERT_FALSE(publish_head(0x32, 1, 0, 600));  // no id
  TEST_ASSERT_FALSE(publish_head(0x32, 597, 7, 600)); // id past the end
  TEST_ASSERT_TRUE(publish_head(0x32, 596, 7, 600));
}

static void ids(void) {
  uint16_t id;
  const uint8_t b[] = { 0x12, 0x34 };
  TEST_ASSERT_TRUE(parse_id(0x40, b, 2, &id));
  TEST_ASSERT_EQUAL_HEX16(0x1234, id);
  TEST_ASSERT_TRUE(parse_id(0x62, b, 2, &id));    // PUBREL has its flag
  TEST_ASSERT_FALSE(parse_id(0x60, b, 2, &id));
  TEST_ASSERT_FALSE(parse_id(0x42, b, 2, &id));
  TEST_ASSERT_TRUE(parse_id(0xB0, b, 2, &id));    // UNSUBACK
  TEST_ASSERT_FALSE(parse_id(0x40, b, 3, &id));
  const uint8_t zero[] = { 0, 0 };
  TEST_ASSERT_FALSE(parse_id(0x40, zero, 2, &id));
  TEST_ASSERT_FALSE(parse_id(0x30, b, 2, &id));
}

static void subacks(void) {
  uint16_t id;
  uint8_t g;
  const uint8_t ok[] = { 0, 9, 1 };
  TEST_ASSERT_TRUE(parse_suback(0x90, ok, 3, &id, &g));
  TEST_ASSERT_EQUAL_UINT16(9, id);
  TEST_ASSERT_EQUAL_UINT8(1, g);
  const uint8_t no[] = { 0, 9, 0x80 };
  TEST_ASSERT_TRUE(parse_suback(0x90, no, 3, &id, &g));
  TEST_ASSERT_EQUAL_HEX8(0x80, g);
  const uint8_t qos3[] = { 0, 9, 3 };
  TEST_ASSERT_FALSE(parse_suback(0x90, qos3, 3, &id, &g));
  TEST_ASSERT_FALSE(parse_suback(0x90, ok, 2, &id, &g));
  TEST_ASSERT_TRUE(parse_bare(0xD0, 0));
  TEST_ASSERT_FALSE(parse_bare(0xD0, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(lengths);
  RUN_TEST(bad_lengths);
  RUN_TEST(utf8);
  RUN_TEST(topics);
  RUN_TEST(filters);
  RUN_TEST(matching);
  RUN_TEST(connect_min);
  RUN_TEST(connect_full);
  RUN_TEST(connect_refused);
  RUN_TEST(publish_qos0);
  RUN_TEST(publish_qos1);
  RUN_TEST(publish_refused);
  RUN_TEST(publish_big);
  RUN_TEST(subscribe_one);
  RUN_TEST(unsubscribe_one);
  RUN_TEST(acks);
  RUN_TEST(connacks);
  RUN_TEST(publishes);
  RUN_TEST(bad_publishes);
  RUN_TEST(heads);
  RUN_TEST(ids);
  RUN_TEST(subacks);
  return UNITY_END();
}
