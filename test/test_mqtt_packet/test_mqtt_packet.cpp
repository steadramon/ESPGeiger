/*
  test_mqtt_packet - the MQTT PUBLISH packet parser.

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

// Parses bytes straight off the network from an untrusted broker, so the
// hostile cases matter most: topic length disagreeing with the packet, a short
// remaining-length, a payload split at an awkward offset.
//
// Run under native_asan; only the sanitizer catches an out-of-bounds read.

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../lib/async-mqtt-client/src/AsyncMqttClient/Packets/PublishPacket.hpp"

using AsyncMqttClientInternals::PublishPacket;
using AsyncMqttClientInternals::ParsingInformation;
using AsyncMqttClientInternals::BufferState;

// --- harness ----------------------------------------------------------------

// Mirrors AsyncMqttClient: topicBuffer is new char[maxTopicLength + 1], and
// the guard byte past the end catches a one-off overflow.
struct Harness {
  ParsingInformation pi{};
  std::vector<char>  topic_storage;
  uint16_t           max_topic;

  // what the callbacks saw
  int         data_calls    = 0;
  int         complete_calls= 0;
  std::string last_topic;
  size_t      last_len      = 0;
  size_t      last_index    = 0;
  size_t      last_total    = 0;
  std::string payload_seen;

  static constexpr char GUARD = 0x7E;

  explicit Harness(uint16_t maxTopicLength = 128) : max_topic(maxTopicLength) {
    topic_storage.assign((size_t)maxTopicLength + 2, GUARD);  // +1 real, +1 guard
    pi.bufferState     = BufferState::VARIABLE_HEADER;
    pi.maxTopicLength  = maxTopicLength;
    pi.topicBuffer     = topic_storage.data();
    pi.packetType      = 3;
    pi.packetFlags     = 0;
    pi.remainingLength = 0;
  }

  bool guard_intact() const { return topic_storage[max_topic + 1] == GUARD; }

  PublishPacket* make() {
    return new PublishPacket(&pi,
      [this](char* topic, char* payload, uint8_t, bool, bool,
             size_t len, size_t index, size_t total, uint16_t) {
        data_calls++;
        last_topic = topic ? topic : "";
        last_len = len; last_index = index; last_total = total;
        if (payload && len) payload_seen.append(payload, len);
      },
      [this](uint16_t, uint8_t) { complete_calls++; });
  }
};

// Build a PUBLISH variable header: 2-byte topic length, topic, [packet id].
static std::vector<char> var_header(const std::string& topic, bool with_id,
                                    uint16_t id = 0x1234) {
  std::vector<char> v;
  v.push_back((char)(topic.size() >> 8));
  v.push_back((char)(topic.size() & 0xFF));
  v.insert(v.end(), topic.begin(), topic.end());
  if (with_id) { v.push_back((char)(id >> 8)); v.push_back((char)(id & 0xFF)); }
  return v;
}

// Feed the variable header one byte at a time, exactly as the client does.
static void feed_header(PublishPacket* p, std::vector<char>& buf, Harness& h) {
  size_t pos = 0;
  while (pos < buf.size() && h.pi.bufferState == BufferState::VARIABLE_HEADER) {
    p->parseVariableHeader(buf.data(), buf.size(), &pos);
  }
}

void setUp(void)    {}
void tearDown(void) {}

// --- well-formed ------------------------------------------------------------

static void test_qos0_no_payload(void) {
  Harness h;
  std::string topic = "espgeiger/cmd";
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);

  TEST_ASSERT_EQUAL_STRING(topic.c_str(), h.last_topic.c_str());
  TEST_ASSERT_EQUAL_INT(1, h.data_calls);
  TEST_ASSERT_EQUAL_INT(1, h.complete_calls);
  TEST_ASSERT_EQUAL(BufferState::NONE, h.pi.bufferState);
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

static void test_qos0_with_payload(void) {
  Harness h;
  std::string topic = "homeassistant/status", body = "online";
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size() + body.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);
  TEST_ASSERT_EQUAL(BufferState::PAYLOAD, h.pi.bufferState);

  std::vector<char> pay(body.begin(), body.end());
  size_t pos = 0;
  p->parsePayload(pay.data(), pay.size(), &pos);

  TEST_ASSERT_EQUAL_STRING(topic.c_str(), h.last_topic.c_str());
  TEST_ASSERT_EQUAL_STRING(body.c_str(), h.payload_seen.c_str());
  TEST_ASSERT_EQUAL_size_t(body.size(), h.last_total);
  TEST_ASSERT_EQUAL_INT(1, h.complete_calls);
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

// QoS 1 and 2 carry a packet id between topic and payload.
static void test_qos1_has_packet_id(void) {
  Harness h;
  h.pi.packetFlags = 0x02;                    // QOS1
  std::string topic = "t", body = "hello";
  auto vh = var_header(topic, true);
  h.pi.remainingLength = vh.size() + body.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);
  TEST_ASSERT_EQUAL(BufferState::PAYLOAD, h.pi.bufferState);

  std::vector<char> pay(body.begin(), body.end());
  size_t pos = 0;
  p->parsePayload(pay.data(), pay.size(), &pos);
  TEST_ASSERT_EQUAL_STRING(body.c_str(), h.payload_seen.c_str());
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

// A payload arriving across several TCP segments must reassemble, with index
// and total tracking correctly. This is the normal case for a big retained
// message and the one most likely to be got wrong.
static void test_payload_split_across_segments(void) {
  Harness h;
  std::string topic = "t";
  std::string body  = "0123456789abcdefghij";
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size() + body.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);

  size_t off = 0;
  while (off < body.size()) {
    size_t chunk = (body.size() - off) > 7 ? 7 : (body.size() - off);
    std::vector<char> seg(body.begin() + off, body.begin() + off + chunk);
    size_t pos = 0;
    p->parsePayload(seg.data(), seg.size(), &pos);
    TEST_ASSERT_EQUAL_size_t(chunk, pos);      // consumed exactly the segment
    off += chunk;
  }

  TEST_ASSERT_EQUAL_STRING(body.c_str(), h.payload_seen.c_str());
  TEST_ASSERT_EQUAL_INT(1, h.complete_calls);
  TEST_ASSERT_EQUAL(BufferState::NONE, h.pi.bufferState);
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

// --- topic length boundaries ------------------------------------------------

// LATENT HAZARD, not a live bug. parseVariableHeader does
//   _topicLength = currentByte | _topicLengthMsb << 8;
// with `char currentByte`, and the packet id does the same with
// `char _packetIdMsb`. Both are only correct because every ESP toolchain
// (xtensa-lx106, xtensa-esp32, riscv32-esp) defaults to UNSIGNED char.
//
// Build with -fsigned-char, or port to a target whose char is signed, and the
// low byte sign-extends: every topic whose length has bit 7 set (128..255,
// 384..511, ...) computes a nonsense length and is silently dropped, and every
// packet id >= 0x8000 is mangled, which breaks QoS 1 and 2 acknowledgement.
//
// test.ini passes -fno-signed-char so this suite matches the device. This test
// asserts the assumption directly rather than relying on it silently.
static void test_parser_depends_on_unsigned_char(void) {
  char low = (char)200;
  uint16_t len = (uint16_t)(low | (char)0 << 8);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(200, len,
    "char is signed here; the topic-length and packet-id parses are wrong");

  char msb = (char)0x80;
  uint16_t id = (uint16_t)((char)0x34 | msb << 8);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x8034, id,
    "packet ids >= 0x8000 mangle under signed char");
}

// Exactly maxTopicLength is the last accepted size. The NUL lands on the final
// byte of the maxTopicLength+1 allocation, so the guard past it must survive.
// 128 is also the first length whose low byte has bit 7 set, so this is the
// case that fails outright on a signed-char build.
static void test_topic_exactly_max_length(void) {
  Harness h(128);
  std::string topic(128, 'x');
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);

  TEST_ASSERT_EQUAL_size_t(128, h.last_topic.size());
  TEST_ASSERT_EQUAL_INT(1, h.data_calls);
  TEST_ASSERT_TRUE_MESSAGE(h.guard_intact(), "wrote past topicBuffer");
  delete p;
}

// One over the limit must be dropped silently, with no callback and no write.
static void test_topic_over_max_is_ignored(void) {
  Harness h(128);
  std::string topic(129, 'x');
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, h.data_calls, "oversized topic reached the callback");
  TEST_ASSERT_TRUE_MESSAGE(h.guard_intact(), "oversized topic wrote past topicBuffer");
  delete p;
}

// --- hostile input ----------------------------------------------------------

// A broker that declares a remainingLength smaller than the variable header it
// then sends. The payload length is computed as
// `remainingLength - (_bytePosition + 1)` in uint32, so a short declaration
// underflows to ~4 GB.
static void test_remaining_length_shorter_than_header(void) {
  Harness h;
  std::string topic = "0123456789";        // 10 bytes -> header is 12
  auto vh = var_header(topic, false);
  h.pi.remainingLength = 5;                // lies: smaller than the header

  PublishPacket* p = h.make();
  feed_header(p, vh, h);

  if (h.pi.bufferState == BufferState::PAYLOAD) {
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1024u * 1024u, (unsigned)h.last_total,
      "underflowed remaining length produced an absurd payload size");
  }
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

// A topic length field that overruns the data the broker actually sent.
static void test_topic_length_longer_than_data(void) {
  Harness h;
  std::vector<char> vh;
  vh.push_back(0x00);
  vh.push_back(0x40);            // claims 64 bytes of topic
  const char* got = "short";
  vh.insert(vh.end(), got, got + strlen(got));
  h.pi.remainingLength = vh.size();

  PublishPacket* p = h.make();
  size_t pos = 0;
  while (pos < vh.size() && h.pi.bufferState == BufferState::VARIABLE_HEADER) {
    p->parseVariableHeader(vh.data(), vh.size(), &pos);
  }
  // Must not have completed on a truncated topic, and must not have run off
  // the buffer. ASan enforces the second part.
  TEST_ASSERT_TRUE(h.guard_intact());
  delete p;
}

// REGRESSION. _bytePosition used to be uint8_t while being compared against
// `2 + _topicLength`, and _topicLength is bounded only by maxTopicLength - a
// public uint16_t setter. Above 253 the terminating comparison became
// unreachable, the position wrapped, the completion branch never fired and the
// parser wedged in VARIABLE_HEADER forever, silently killing MQTT for the rest
// of the connection.
//
// Never reachable as shipped (the constructor pins 128 and we never override
// it), but the ceiling was undocumented. Widened to uint16_t; this proves a
// large topic now parses through to completion.
static void test_large_max_topic_length_still_completes(void) {
  Harness h(300);
  std::string topic(300, 'x');
  auto vh = var_header(topic, false);
  h.pi.remainingLength = vh.size();

  PublishPacket* p = h.make();
  size_t pos = 0;
  int iterations = 0;
  const int LIMIT = 5000;
  while (pos < vh.size() && h.pi.bufferState == BufferState::VARIABLE_HEADER
         && iterations < LIMIT) {
    p->parseVariableHeader(vh.data(), vh.size(), &pos);
    iterations++;
  }

  TEST_ASSERT_TRUE_MESSAGE(h.guard_intact(), "wrote past topicBuffer");
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(LIMIT, iterations, "parser wedged");
  TEST_ASSERT_EQUAL_MESSAGE(BufferState::NONE, h.pi.bufferState,
    "parser did not complete a 300-byte topic");
  TEST_ASSERT_EQUAL_INT(1, h.complete_calls);
  TEST_ASSERT_EQUAL_size_t(300, h.last_topic.size());
  delete p;
}

// The topic length is carried in two raw network bytes. Lengths whose low byte
// has bit 7 set are the ones sign extension used to destroy, so walk a spread
// of them and confirm each parses to its true value.
static void test_topic_lengths_with_high_bit_set(void) {
  for (size_t len : { (size_t)128, (size_t)129, (size_t)200, (size_t)255,
                      (size_t)256, (size_t)384 }) {
    Harness h(512);
    std::string topic(len, 'z');
    auto vh = var_header(topic, false);
    h.pi.remainingLength = vh.size();

    PublishPacket* p = h.make();
    feed_header(p, vh, h);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, h.data_calls, "topic dropped");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len, h.last_topic.size(), "wrong topic length");
    TEST_ASSERT_TRUE(h.guard_intact());
    delete p;
  }
}

// Packet ids at or above 0x8000 come through the same raw-byte path.
static void test_packet_id_high_bit_set(void) {
  Harness h;
  h.pi.packetFlags = 0x02;                    // QOS1
  std::string topic = "t", body = "y";
  auto vh = var_header(topic, true, 0x8034);
  h.pi.remainingLength = vh.size() + body.size();

  PublishPacket* p = h.make();
  feed_header(p, vh, h);
  TEST_ASSERT_EQUAL_MESSAGE(BufferState::PAYLOAD, h.pi.bufferState,
    "high packet id broke the header parse");

  std::vector<char> pay(body.begin(), body.end());
  size_t pos = 0;
  p->parsePayload(pay.data(), pay.size(), &pos);
  TEST_ASSERT_EQUAL_INT(1, h.complete_calls);
  delete p;
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_qos0_no_payload);
  RUN_TEST(test_qos0_with_payload);
  RUN_TEST(test_qos1_has_packet_id);
  RUN_TEST(test_payload_split_across_segments);
  RUN_TEST(test_parser_depends_on_unsigned_char);
  RUN_TEST(test_topic_exactly_max_length);
  RUN_TEST(test_topic_over_max_is_ignored);
  RUN_TEST(test_remaining_length_shorter_than_header);
  RUN_TEST(test_topic_length_longer_than_data);
  RUN_TEST(test_large_max_topic_length_still_completes);
  RUN_TEST(test_topic_lengths_with_high_bit_set);
  RUN_TEST(test_packet_id_high_bit_set);
  return UNITY_END();
}
