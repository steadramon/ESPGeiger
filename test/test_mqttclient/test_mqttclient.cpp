/*
  test_mqttclient - AsyncMqttClient's connection state machine.

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

// The client holds one AsyncClient for its whole life, so every reconnect
// reuses it. A guard that survives a close therefore silences the client
// permanently: connect() no-ops from any state but DISCONNECTED, nothing
// times CONNECTING out, and only a reboot recovers. Shipped that way in
// 0.12.5 through 0.12.7.
//
// Drives the real client over the fake lwIP from test_asynctcp.

#include <unity.h>
#include <string.h>

#include "fake_lwip.h"
#include "AsyncMqttClient.hpp"

static AsyncMqttClient* m = nullptr;

void setUp(void) {
  FakeLwip::reset();
  m = new AsyncMqttClient();
  m->setServer(IPAddress(10, 0, 0, 1), 1883);
}

void tearDown(void) {
  delete m;
  m = nullptr;
  FakeLwip::reset();
}

// Starts an attempt and returns the pcb lwIP handed out.
static tcp_pcb* dispatch(AsyncMqttClient* cl) {
  TEST_ASSERT_TRUE(cl->connect());
  tcp_pcb* pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(pcb);
  return pcb;
}

// CONNACK, session absent, return code 0.
static void deliver_connack(tcp_pcb* pcb) {
  static const char CONNACK[] = { 0x20, 0x02, 0x00, 0x00 };
  pbuf* pb = FakeLwip::make_pbuf(CONNACK, sizeof(CONNACK));
  FakeLwip::fire_recv(pcb, pb, ERR_OK);
}

// TCP up, CONNECT out, CONNACK back.
static tcp_pcb* connect_fully(AsyncMqttClient* cl) {
  tcp_pcb* pcb = dispatch(cl);
  FakeLwip::fire_connected(pcb, ERR_OK);
  TEST_ASSERT_TRUE(FakeLwip::has_recv_cb(pcb));
  deliver_connack(pcb);
  return pcb;
}

static void test_connect_dispatches_a_tcp_connect(void) {
  dispatch(m);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
  TEST_ASSERT_TRUE(m->connecting());
  TEST_ASSERT_FALSE(m->connected());
}

static void test_connect_while_an_attempt_is_in_flight_is_refused(void) {
  dispatch(m);
  TEST_ASSERT_FALSE(m->connect());
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
}

static void test_connack_completes_the_connection(void) {
  connect_fully(m);
  TEST_ASSERT_TRUE(m->connected());
  TEST_ASSERT_FALSE(m->connecting());
}

// The field bug, end to end at the layer it bit. A broker restart, a WiFi
// blip or a prefs save all reach this path.
static void test_a_disconnected_client_connects_again(void) {
  connect_fully(m);
  TEST_ASSERT_TRUE(m->connected());

  m->disconnect(true);
  TEST_ASSERT_FALSE(m->connected());
  TEST_ASSERT_FALSE(m->connecting());

  connect_fully(m);
  TEST_ASSERT_TRUE(m->connected());
}

// CHARACTERISATION: _error() never routes through close(), so this path was
// reusable even while the close() paths were latched shut. A broker that RST
// recovered on its own; one that went quiet and failed the keepalive did not.
static void test_a_client_whose_peer_reset_connects_again(void) {
  tcp_pcb* pcb = connect_fully(m);
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_FALSE(m->connected());

  connect_fully(m);
  TEST_ASSERT_TRUE(m->connected());
}

// Nothing times CONNECTING out, so a forced disconnect is the only way back
// and it has to leave the client able to retry.
static void test_forced_disconnect_from_connecting_allows_a_retry(void) {
  dispatch(m);
  TEST_ASSERT_TRUE(m->connecting());

  m->disconnect(true);
  TEST_ASSERT_FALSE(m->connecting());

  connect_fully(m);
  TEST_ASSERT_TRUE(m->connected());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_dispatches_a_tcp_connect);
  RUN_TEST(test_connect_while_an_attempt_is_in_flight_is_refused);
  RUN_TEST(test_connack_completes_the_connection);
  RUN_TEST(test_a_disconnected_client_connects_again);
  RUN_TEST(test_a_client_whose_peer_reset_connects_again);
  RUN_TEST(test_forced_disconnect_from_connecting_allows_a_retry);
  return UNITY_END();
}
