/*
  AsyncClient lifetime and callback-ordering tests.

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

// Lifetime faults. Run under native_asan or a regression reads as a pass.
// ESP8266 is not defined here: _pcbAlive's DRAM range check would reject every
// host pointer. The list walk is what is covered.

#include <unity.h>
#include <string.h>

#include "fake_lwip.h"
#include "EGAsyncTCP.h"

static AsyncClient* c = nullptr;

void setUp(void) {
  FakeLwip::reset();
  c = new AsyncClient();
}

void tearDown(void) {
  delete c;
  c = nullptr;
  FakeLwip::reset();
}

// Returns the pcb lwIP handed out.
static tcp_pcb* connect_and_establish(AsyncClient* cl) {
  TEST_ASSERT_TRUE(cl->connect(IPAddress(10, 0, 0, 1), 1883));
  tcp_pcb* pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(pcb);
  FakeLwip::fire_connected(pcb, ERR_OK);
  return pcb;
}

// --- connect path -----------------------------------------------------------

// arg and err up front; recv, sent and poll after the handshake.
static void test_connect_allocates_a_pcb_and_registers_the_error_callback(void) {
  TEST_ASSERT_TRUE(c->connect(IPAddress(10, 0, 0, 1), 1883));
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_new);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
  tcp_pcb* pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_UINT16(1883, pcb->remote_port);

  TEST_ASSERT_EQUAL_PTR(c, pcb->callback_arg);
  TEST_ASSERT_NOT_NULL(pcb->errf);
  TEST_ASSERT_NULL(pcb->recv);
  TEST_ASSERT_NULL(pcb->sent);
}

static void test_establishing_registers_the_remaining_callbacks(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  TEST_ASSERT_NOT_NULL(pcb->recv);
  TEST_ASSERT_NOT_NULL(pcb->sent);
  TEST_ASSERT_NOT_NULL(pcb->poll);
}

static void test_connect_twice_is_refused(void) {
  TEST_ASSERT_TRUE(c->connect(IPAddress(10, 0, 0, 1), 1883));
  TEST_ASSERT_FALSE(c->connect(IPAddress(10, 0, 0, 2), 1884));
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_new);
}

static bool s_connected_fired = false;
static void on_connected(void* arg, AsyncClient* cl) {
  (void)arg; (void)cl;
  s_connected_fired = true;
}

static void test_connected_callback_reaches_the_handler(void) {
  s_connected_fired = false;
  c->onConnect(on_connected);
  connect_and_establish(c);
  TEST_ASSERT_TRUE(s_connected_fired);
  TEST_ASSERT_TRUE(c->connected());
}

// A client is reusable; MQTT holds one for the life of the process.
static void test_a_closed_client_connects_again(void) {
  c->onConnect(on_connected);
  connect_and_establish(c);
  c->close(true);
  TEST_ASSERT_FALSE(c->connected());

  s_connected_fired = false;
  connect_and_establish(c);
  TEST_ASSERT_TRUE(s_connected_fired);
  TEST_ASSERT_TRUE(c->connected());
}

// Reuse must not re-admit a callback for the abandoned pcb.
static void test_connected_for_a_replaced_pcb_is_ignored(void) {
  c->onConnect(on_connected);
  TEST_ASSERT_TRUE(c->connect(IPAddress(10, 0, 0, 1), 1883));
  tcp_pcb* first = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(first);
  FakeLwip::pin_pcb(first);
  FakeLwip::Snapshot snap = FakeLwip::snapshot_cbs(first);
  c->close(true);

  TEST_ASSERT_TRUE(c->connect(IPAddress(10, 0, 0, 2), 1884));
  s_connected_fired = false;
  FakeLwip::deliver_connected(snap, first, ERR_OK);
  TEST_ASSERT_FALSE(s_connected_fired);
}

// --- recv delivered for a pcb the data handler closed -------------------------

static int s_data_calls = 0;
static void on_data_close(void* arg, AsyncClient* cl, void* data, size_t len) {
  (void)arg; (void)data; (void)len;
  s_data_calls++;
  cl->close(true);       // frees the pcb underneath the dispatcher
}

// After the user callback the pcb may be gone; liveness, not hasClient().
static void test_recv_does_not_ack_a_pcb_the_data_handler_closed(void) {
  s_data_calls = 0;
  c->onData(on_data_close);
  tcp_pcb* pcb = connect_and_establish(c);

  pbuf* pb = FakeLwip::make_pbuf("hello", 5);
  FakeLwip::fire_recv(pcb, pb, ERR_OK);

  TEST_ASSERT_EQUAL_INT(1, s_data_calls);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));
  // close() flushes deferred acks on the live pcb; none may land after it.
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

// Segment after close: the pbuf is still the library's to release.
static void test_late_recv_after_close_releases_the_pbuf(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::Snapshot snap = FakeLwip::snapshot_cbs(pcb);
  c->close(true);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));

  pbuf* pb = FakeLwip::make_pbuf("late", 4);
  FakeLwip::deliver_recv(snap, pcb, pb, ERR_OK);

  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
  // Released, not leaked.
  TEST_ASSERT_GREATER_THAN_INT(0, FakeLwip::counts().pbuf_free);
}

// --- pcb liveness -----------------------------------------------------------

static void test_remote_ip_is_zero_once_the_pcb_is_gone(void) {
  connect_and_establish(c);
  TEST_ASSERT_EQUAL_UINT16(1883, c->getRemotePort());
  c->close(true);
  // Reject, do not dereference.
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->remoteIP());
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->localIP());
}

// TIME_WAIT pcbs are off the active list but still allocated.
static void test_a_timewait_pcb_is_still_live(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::move_to_timewait(pcb);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));
  TEST_ASSERT_EQUAL_UINT16(1883, c->getRemotePort());
}

// --- error path -------------------------------------------------------------

static bool s_error_fired = false;
static void on_error(void* arg, AsyncClient* cl, err_t e) {
  (void)arg; (void)cl; (void)e;
  s_error_fired = true;
}

// lwIP frees the pcb before the error callback.
static void test_error_callback_runs_with_the_pcb_already_freed(void) {
  s_error_fired = false;
  c->onError(on_error);
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_error_fired);
  TEST_ASSERT_FALSE(c->connected());
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->remoteIP());
}

// --- disconnect callback ----------------------------------------------------
//
// _discard_cb is how the owner learns the connection is gone; a suppressed one
// wedges it. Both directions asserted.

static bool s_disconnected_fired = false;
static void on_disconnected(void* arg, AsyncClient* cl) {
  (void)arg; (void)cl;
  s_disconnected_fired = true;
}

// Deletes the client; safe only because every _discard_cb site nulls _pcb first.
static AsyncClient* s_self_delete_target = nullptr;
static void on_disconnected_delete(void* arg, AsyncClient* cl) {
  (void)arg;
  s_disconnected_fired = true;
  delete cl;
  s_self_delete_target = nullptr;
}

static void test_close_fires_the_disconnect_handler(void) {
  c->onDisconnect(on_disconnected);
  connect_and_establish(c);
  s_disconnected_fired = false;
  c->close(true);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

static void test_error_fires_the_disconnect_handler(void) {
  c->onDisconnect(on_disconnected);
  tcp_pcb* pcb = connect_and_establish(c);
  s_disconnected_fired = false;
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

static void test_dns_failure_fires_the_disconnect_handler(void) {
  c->onDisconnect(on_disconnected);
  TEST_ASSERT_TRUE(c->connect("nope.example", 1883));
  s_disconnected_fired = false;
  FakeLwip::fire_dns_found(0, nullptr);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

// clearClient() must precede _close() in the destructor.
static void test_destroying_a_client_does_not_fire_the_disconnect_handler(void) {
  AsyncClient* victim = new AsyncClient();
  victim->onDisconnect(on_disconnected);
  connect_and_establish(victim);
  s_disconnected_fired = false;
  delete victim;
  TEST_ASSERT_FALSE(s_disconnected_fired);
}

// A _discard_cb site dispatching before nulling _pcb double-frees here.
static void test_a_disconnect_handler_may_delete_the_client(void) {
  s_self_delete_target = new AsyncClient();
  s_self_delete_target->onDisconnect(on_disconnected_delete);
  tcp_pcb* pcb = connect_and_establish(s_self_delete_target);
  s_disconnected_fired = false;
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_disconnected_fired);
  TEST_ASSERT_NULL(s_self_delete_target);
}

// --- writing ----------------------------------------------------------------

static void test_write_reports_what_the_send_buffer_took(void) {
  connect_and_establish(c);
  FakeLwip::set_sndbuf(8);
  size_t n = c->write("0123456789", 10);
  TEST_ASSERT_EQUAL_size_t(8, n);
  TEST_ASSERT_EQUAL_size_t(8, FakeLwip::bytes_written());
}

static void test_write_on_a_closed_client_writes_nothing(void) {
  connect_and_establish(c);
  c->close(true);
  TEST_ASSERT_EQUAL_size_t(0, c->write("abc", 3));
  TEST_ASSERT_EQUAL_size_t(0, FakeLwip::bytes_written());
}

// --- destruction ------------------------------------------------------------

static void test_destroying_a_connected_client_closes_its_pcb(void) {
  AsyncClient* victim = new AsyncClient();
  connect_and_establish(victim);
  delete victim;
  TEST_ASSERT_NULL(tcp_active_pcbs);
}

// Freed mid-handshake: the destructor must strip every callback slot.
static void test_destroying_a_client_mid_handshake_clears_its_callbacks(void) {
  AsyncClient* victim = new AsyncClient();
  TEST_ASSERT_TRUE(victim->connect(IPAddress(10, 0, 0, 1), 1883));
  tcp_pcb* pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(pcb);
  FakeLwip::pin_pcb(pcb);
  delete victim;

  TEST_ASSERT_NULL(pcb->recv);
  TEST_ASSERT_NULL(pcb->sent);
  TEST_ASSERT_NULL(pcb->errf);
  TEST_ASSERT_NULL(pcb->poll);
  TEST_ASSERT_NULL(pcb->callback_arg);
}

// --- callbacks dispatched after the client is gone --------------------------
//
// tcp_arg is NULL after clearTcpCallbacks(); each entry point must drop the
// delivery. _acArgAlive cannot see a freed-but-plausible pointer.

struct Orphan { FakeLwip::Snapshot snap; tcp_pcb* pcb; };

// Connect, pin the pcb, destroy the client.
static Orphan orphan_client() {
  AsyncClient* victim = new AsyncClient();
  tcp_pcb* pcb = connect_and_establish(victim);
  FakeLwip::pin_pcb(pcb);
  Orphan o{ FakeLwip::snapshot_cbs(pcb), pcb };
  delete victim;
  o.snap.arg = pcb->callback_arg;   // nulled by the destructor
  return o;
}

static void test_recv_after_the_client_is_gone_is_dropped(void) {
  Orphan o = orphan_client();
  pbuf* pb = FakeLwip::make_pbuf("late", 4);
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_recv(o.snap, o.pcb, pb, ERR_OK));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

static void test_poll_after_the_client_is_gone_is_dropped(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_poll(o.snap, o.pcb));
}

static void test_sent_after_the_client_is_gone_is_dropped(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_sent(o.snap, o.pcb, 4));
}

static void test_connected_after_the_client_is_gone_is_dropped(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_connected(o.snap, o.pcb, ERR_OK));
}

static void test_error_after_the_client_is_gone_is_dropped(void) {
  Orphan o = orphan_client();
  FakeLwip::deliver_error(o.snap, ERR_RST);   // returns void; must not fault
}

// --- pcb retired without the client being told ------------------------------
//
// Should not arise; pins the guards. Unlinked, not freed, or the test itself
// would be the use-after-free.

static tcp_pcb* strand_client(AsyncClient* cl) {
  tcp_pcb* pcb = connect_and_establish(cl);
  FakeLwip::unlink_pcb(pcb);
  return pcb;
}

static void test_write_after_the_pcb_was_freed_writes_nothing(void) {
  strand_client(c);
  TEST_ASSERT_EQUAL_size_t(0, c->write("abc", 3));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().wrote_on_dead_pcb);
}

static void test_close_after_the_pcb_was_freed_acks_nothing(void) {
  strand_client(c);
  c->close(true);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

static int s_data_seen = 0;
static void on_data_count(void* arg, AsyncClient* cl, void* data, size_t len) {
  (void)arg; (void)cl; (void)data; (void)len;
  s_data_seen++;
}

// Segment queued before lwIP freed the pcb: the data handler must not run.
static void test_recv_for_a_freed_pcb_never_reaches_the_data_handler(void) {
  s_data_seen = 0;
  c->onData(on_data_count);
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::Snapshot snap = FakeLwip::snapshot_cbs(pcb);
  FakeLwip::unlink_pcb(pcb);

  pbuf* pb = FakeLwip::make_pbuf("x", 1);
  FakeLwip::deliver_recv(snap, pcb, pb, ERR_OK);

  TEST_ASSERT_EQUAL_INT(0, s_data_seen);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

// --- server accept ----------------------------------------------------------

// Server destroyed with an accept queued: reject and abort the incoming pcb.
static void test_accept_after_the_server_is_gone_is_dropped(void) {
  AsyncServer* srv = new AsyncServer(8080);
  srv->begin();
  tcp_pcb* listen_pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(listen_pcb);
  FakeLwip::pin_pcb(listen_pcb);
  FakeLwip::Snapshot snap = FakeLwip::snapshot_cbs(listen_pcb);
  delete srv;
  snap.arg = listen_pcb->callback_arg;   // nulled by end()

  tcp_pcb* incoming = FakeLwip::new_pcb();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_accept(snap, incoming, ERR_OK));
  TEST_ASSERT_FALSE(FakeLwip::is_active(incoming));
}

// --- DNS --------------------------------------------------------------------

static void test_connect_by_name_starts_a_lookup(void) {
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  TEST_ASSERT_EQUAL_size_t(1, FakeLwip::dns_pending_count());
  TEST_ASSERT_EQUAL_STRING("mqtt.example", FakeLwip::dns_pending_name(0));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_new);
}

static void test_dns_success_proceeds_to_connect(void) {
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
  TEST_ASSERT_NOT_NULL(tcp_active_pcbs);
  TEST_ASSERT_EQUAL_UINT16(1883, tcp_active_pcbs->remote_port);
}

static void test_dns_failure_does_not_connect(void) {
  s_error_fired = false;
  c->onError(on_error);
  TEST_ASSERT_TRUE(c->connect("nope.example", 1883));
  FakeLwip::fire_dns_found(0, nullptr);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_connect);
}

// The found-callback has no pcb to clear; the registry gates it.
static void test_found_callback_after_the_client_is_freed_is_dropped(void) {
  AsyncClient* victim = new AsyncClient();
  TEST_ASSERT_TRUE(victim->connect("mqtt.example", 1883));
  TEST_ASSERT_EQUAL_size_t(1, FakeLwip::dns_pending_count());
  delete victim;

  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);   // must not touch the freed client
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_connect);
}

// Registry holds 8; a ninth lookup must fail the connect outright.
static void test_lookup_beyond_the_registry_fails_the_connect(void) {
  AsyncClient* cl[9];
  for (int i = 0; i < 8; i++) {
    cl[i] = new AsyncClient();
    TEST_ASSERT_TRUE(cl[i]->connect("mqtt.example", 1883));
  }
  cl[8] = new AsyncClient();
  TEST_ASSERT_FALSE(cl[8]->connect("mqtt.example", 1883));

  // Registered lookups still resolve.
  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);

  for (int i = 0; i < 9; i++) delete cl[i];
}

// Same client twice is one slot.
static void test_repeated_connect_does_not_consume_extra_registry_slots(void) {
  AsyncClient* cl[8];
  for (int i = 0; i < 8; i++) {
    cl[i] = new AsyncClient();
    TEST_ASSERT_TRUE(cl[i]->connect("mqtt.example", 1883));
  }
  // Already registered, not refused.

  TEST_ASSERT_TRUE(cl[0]->connect("mqtt.example", 1883));
  for (int i = 0; i < 8; i++) delete cl[i];
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_allocates_a_pcb_and_registers_the_error_callback);
  RUN_TEST(test_establishing_registers_the_remaining_callbacks);
  RUN_TEST(test_connect_twice_is_refused);
  RUN_TEST(test_a_closed_client_connects_again);
  RUN_TEST(test_connected_for_a_replaced_pcb_is_ignored);
  RUN_TEST(test_connected_callback_reaches_the_handler);
  RUN_TEST(test_recv_does_not_ack_a_pcb_the_data_handler_closed);
  RUN_TEST(test_late_recv_after_close_releases_the_pbuf);
  RUN_TEST(test_remote_ip_is_zero_once_the_pcb_is_gone);
  RUN_TEST(test_a_timewait_pcb_is_still_live);
  RUN_TEST(test_error_callback_runs_with_the_pcb_already_freed);
  RUN_TEST(test_close_fires_the_disconnect_handler);
  RUN_TEST(test_error_fires_the_disconnect_handler);
  RUN_TEST(test_dns_failure_fires_the_disconnect_handler);
  RUN_TEST(test_destroying_a_client_does_not_fire_the_disconnect_handler);
  RUN_TEST(test_a_disconnect_handler_may_delete_the_client);
  RUN_TEST(test_write_reports_what_the_send_buffer_took);
  RUN_TEST(test_write_on_a_closed_client_writes_nothing);
  RUN_TEST(test_destroying_a_connected_client_closes_its_pcb);
  RUN_TEST(test_destroying_a_client_mid_handshake_clears_its_callbacks);
  RUN_TEST(test_recv_after_the_client_is_gone_is_dropped);
  RUN_TEST(test_poll_after_the_client_is_gone_is_dropped);
  RUN_TEST(test_sent_after_the_client_is_gone_is_dropped);
  RUN_TEST(test_connected_after_the_client_is_gone_is_dropped);
  RUN_TEST(test_error_after_the_client_is_gone_is_dropped);
  RUN_TEST(test_write_after_the_pcb_was_freed_writes_nothing);
  RUN_TEST(test_close_after_the_pcb_was_freed_acks_nothing);
  RUN_TEST(test_recv_for_a_freed_pcb_never_reaches_the_data_handler);
  RUN_TEST(test_accept_after_the_server_is_gone_is_dropped);
  RUN_TEST(test_connect_by_name_starts_a_lookup);
  RUN_TEST(test_dns_success_proceeds_to_connect);
  RUN_TEST(test_dns_failure_does_not_connect);
  RUN_TEST(test_found_callback_after_the_client_is_freed_is_dropped);
  RUN_TEST(test_lookup_beyond_the_registry_fails_the_connect);
  RUN_TEST(test_repeated_connect_does_not_consume_extra_registry_slots);
  return UNITY_END();
}
