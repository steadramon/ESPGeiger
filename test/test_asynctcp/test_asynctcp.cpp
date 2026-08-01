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

// run under native_asan, no DRAM check

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

static tcp_pcb* connect_and_establish(AsyncClient* cl) {
  TEST_ASSERT_TRUE(cl->connect(IPAddress(10, 0, 0, 1), 1883));
  tcp_pcb* pcb = tcp_active_pcbs;
  TEST_ASSERT_NOT_NULL(pcb);
  FakeLwip::fire_connected(pcb, ERR_OK);
  return pcb;
}

static void connect_allocs(void) {
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

static void establish_cbs(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  TEST_ASSERT_NOT_NULL(pcb->recv);
  TEST_ASSERT_NOT_NULL(pcb->sent);
  TEST_ASSERT_NOT_NULL(pcb->poll);
}

static void connect_twice(void) {
  TEST_ASSERT_TRUE(c->connect(IPAddress(10, 0, 0, 1), 1883));
  TEST_ASSERT_FALSE(c->connect(IPAddress(10, 0, 0, 2), 1884));
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_new);
}

static bool s_connected_fired = false;
static void on_connected(void* arg, AsyncClient* cl) {
  (void)arg; (void)cl;
  s_connected_fired = true;
}

static void connected_cb(void) {
  s_connected_fired = false;
  c->onConnect(on_connected);
  connect_and_establish(c);
  TEST_ASSERT_TRUE(s_connected_fired);
  TEST_ASSERT_TRUE(c->connected());
}

static void closed_reconnects(void) {
  c->onConnect(on_connected);
  connect_and_establish(c);
  c->close(true);
  TEST_ASSERT_FALSE(c->connected());

  s_connected_fired = false;
  connect_and_establish(c);
  TEST_ASSERT_TRUE(s_connected_fired);
  TEST_ASSERT_TRUE(c->connected());
}

static void replaced_pcb(void) {
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

static int s_data_calls = 0;
static void on_data_close(void* arg, AsyncClient* cl, void* data, size_t len) {
  (void)arg; (void)data; (void)len;
  s_data_calls++;
  cl->close(true);       // frees the pcb under the dispatcher
}

// pcb may be gone after the callback
static void recv_no_ack(void) {
  s_data_calls = 0;
  c->onData(on_data_close);
  tcp_pcb* pcb = connect_and_establish(c);

  pbuf* pb = FakeLwip::make_pbuf("hello", 5);
  FakeLwip::fire_recv(pcb, pb, ERR_OK);

  TEST_ASSERT_EQUAL_INT(1, s_data_calls);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

// library still frees the pbuf
static void late_recv(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::Snapshot snap = FakeLwip::snapshot_cbs(pcb);
  c->close(true);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));

  pbuf* pb = FakeLwip::make_pbuf("late", 4);
  FakeLwip::deliver_recv(snap, pcb, pb, ERR_OK);

  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
  TEST_ASSERT_GREATER_THAN_INT(0, FakeLwip::counts().pbuf_free);
}

static void remote_ip_gone(void) {
  connect_and_establish(c);
  TEST_ASSERT_EQUAL_UINT16(1883, c->getRemotePort());
  c->close(true);
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->remoteIP());
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->localIP());
}

// off active list, still allocated
static void timewait_pcb(void) {
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::move_to_timewait(pcb);
  TEST_ASSERT_FALSE(FakeLwip::is_active(pcb));
  TEST_ASSERT_EQUAL_UINT16(1883, c->getRemotePort());
}

static bool s_error_fired = false;
static void on_error(void* arg, AsyncClient* cl, err_t e) {
  (void)arg; (void)cl; (void)e;
  s_error_fired = true;
}

static void error_cb_freed(void) {
  s_error_fired = false;
  c->onError(on_error);
  tcp_pcb* pcb = connect_and_establish(c);
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_error_fired);
  TEST_ASSERT_FALSE(c->connected());
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)c->remoteIP());
}

// Disconnect callback

static bool s_disconnected_fired = false;
static void on_disconnected(void* arg, AsyncClient* cl) {
  (void)arg; (void)cl;
  s_disconnected_fired = true;
}

// ok only if _pcb nulled first
static AsyncClient* s_self_delete_target = nullptr;
static void on_disconnected_delete(void* arg, AsyncClient* cl) {
  (void)arg;
  s_disconnected_fired = true;
  delete cl;
  s_self_delete_target = nullptr;
}

static void close_disconnect(void) {
  c->onDisconnect(on_disconnected);
  connect_and_establish(c);
  s_disconnected_fired = false;
  c->close(true);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

static void error_disconnect(void) {
  c->onDisconnect(on_disconnected);
  tcp_pcb* pcb = connect_and_establish(c);
  s_disconnected_fired = false;
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

static void dns_fail_disconnect(void) {
  c->onDisconnect(on_disconnected);
  TEST_ASSERT_TRUE(c->connect("nope.example", 1883));
  s_disconnected_fired = false;
  FakeLwip::fire_dns_found(0, nullptr);
  TEST_ASSERT_TRUE(s_disconnected_fired);
}

// clearClient() before _close() in the dtor
static void dtor_no_disconnect(void) {
  AsyncClient* victim = new AsyncClient();
  victim->onDisconnect(on_disconnected);
  connect_and_establish(victim);
  s_disconnected_fired = false;
  delete victim;
  TEST_ASSERT_FALSE(s_disconnected_fired);
}

static void disconnect_deletes(void) {
  s_self_delete_target = new AsyncClient();
  s_self_delete_target->onDisconnect(on_disconnected_delete);
  tcp_pcb* pcb = connect_and_establish(s_self_delete_target);
  s_disconnected_fired = false;
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_disconnected_fired);
  TEST_ASSERT_NULL(s_self_delete_target);
}

static void on_error_delete(void* arg, AsyncClient* cl, err_t e) {
  (void)arg; (void)e;
  s_error_fired = true;
  delete cl;
}

// client not read after error callback
static void error_deletes(void) {
  AsyncClient* victim = new AsyncClient();
  victim->onError(on_error_delete);
  victim->onDisconnect(on_disconnected);
  tcp_pcb* pcb = connect_and_establish(victim);
  s_error_fired = s_disconnected_fired = false;
  FakeLwip::fire_error(pcb, ERR_RST);
  TEST_ASSERT_TRUE(s_error_fired);
  TEST_ASSERT_FALSE(s_disconnected_fired);
}

static void dns_fail_deletes(void) {
  AsyncClient* victim = new AsyncClient();
  victim->onError(on_error_delete);
  victim->onDisconnect(on_disconnected);
  TEST_ASSERT_TRUE(victim->connect("nope.example", 1883));
  s_error_fired = s_disconnected_fired = false;
  FakeLwip::fire_dns_found(0, nullptr);
  TEST_ASSERT_TRUE(s_error_fired);
  TEST_ASSERT_FALSE(s_disconnected_fired);
}

static void write_sndbuf(void) {
  connect_and_establish(c);
  FakeLwip::set_sndbuf(8);
  size_t n = c->write("0123456789", 10);
  TEST_ASSERT_EQUAL_size_t(8, n);
  TEST_ASSERT_EQUAL_size_t(8, FakeLwip::bytes_written());
}

static void write_after_close(void) {
  connect_and_establish(c);
  c->close(true);
  TEST_ASSERT_EQUAL_size_t(0, c->write("abc", 3));
  TEST_ASSERT_EQUAL_size_t(0, FakeLwip::bytes_written());
}

static void dtor_closes(void) {
  AsyncClient* victim = new AsyncClient();
  connect_and_establish(victim);
  delete victim;
  TEST_ASSERT_NULL(tcp_active_pcbs);
}

static void dtor_mid_handshake(void) {
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

// Callbacks after the client is gone

struct Orphan { FakeLwip::Snapshot snap; tcp_pcb* pcb; };

static Orphan orphan_client() {
  AsyncClient* victim = new AsyncClient();
  tcp_pcb* pcb = connect_and_establish(victim);
  FakeLwip::pin_pcb(pcb);
  Orphan o{ FakeLwip::snapshot_cbs(pcb), pcb };
  delete victim;
  o.snap.arg = pcb->callback_arg;   // nulled by the dtor
  return o;
}

static void recv_gone(void) {
  Orphan o = orphan_client();
  pbuf* pb = FakeLwip::make_pbuf("late", 4);
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_recv(o.snap, o.pcb, pb, ERR_OK));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

static void poll_gone(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_poll(o.snap, o.pcb));
}

static void sent_gone(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_sent(o.snap, o.pcb, 4));
}

static void connected_gone(void) {
  Orphan o = orphan_client();
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, FakeLwip::deliver_connected(o.snap, o.pcb, ERR_OK));
}

static void error_gone(void) {
  Orphan o = orphan_client();
  FakeLwip::deliver_error(o.snap, ERR_RST);
}

// pcb retired, client not told

static tcp_pcb* strand_client(AsyncClient* cl) {
  tcp_pcb* pcb = connect_and_establish(cl);
  FakeLwip::unlink_pcb(pcb);
  return pcb;
}

static void write_freed_pcb(void) {
  strand_client(c);
  TEST_ASSERT_EQUAL_size_t(0, c->write("abc", 3));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().wrote_on_dead_pcb);
}

static void close_freed_pcb(void) {
  strand_client(c);
  c->close(true);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().recved_on_dead_pcb);
}

static int s_data_seen = 0;
static void on_data_count(void* arg, AsyncClient* cl, void* data, size_t len) {
  (void)arg; (void)cl; (void)data; (void)len;
  s_data_seen++;
}

static void recv_freed_pcb(void) {
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

static void accept_gone(void) {
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

static void connect_by_name(void) {
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  TEST_ASSERT_EQUAL_size_t(1, FakeLwip::dns_pending_count());
  TEST_ASSERT_EQUAL_STRING("mqtt.example", FakeLwip::dns_pending_name(0));
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_new);
}

static void dns_ok_connects(void) {
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
  TEST_ASSERT_NOT_NULL(tcp_active_pcbs);
  TEST_ASSERT_EQUAL_UINT16(1883, tcp_active_pcbs->remote_port);
}

static void dns_fail_no_connect(void) {
  s_error_fired = false;
  c->onError(on_error);
  TEST_ASSERT_TRUE(c->connect("nope.example", 1883));
  FakeLwip::fire_dns_found(0, nullptr);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_connect);
}

// no pcb for the answer: the owner still hears
static void dns_no_pcb(void) {
  s_error_fired = false;
  c->onError(on_error);
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  FakeLwip::fail_next_pcb();
  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_TRUE(s_error_fired);
}

// closed mid-lookup, no dial
static void dns_closed(void) {
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  c->close(true);
  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_connect);
  TEST_ASSERT_TRUE(c->connect("mqtt.example", 1883));
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);
}

// no pcb, registry gates it
static void found_cb_gone(void) {
  AsyncClient* victim = new AsyncClient();
  TEST_ASSERT_TRUE(victim->connect("mqtt.example", 1883));
  TEST_ASSERT_EQUAL_size_t(1, FakeLwip::dns_pending_count());
  delete victim;

  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(0, FakeLwip::counts().tcp_connect);
}

// registry holds 8
static void registry_full(void) {
  AsyncClient* cl[9];
  for (int i = 0; i < 8; i++) {
    cl[i] = new AsyncClient();
    TEST_ASSERT_TRUE(cl[i]->connect("mqtt.example", 1883));
  }
  cl[8] = new AsyncClient();
  TEST_ASSERT_FALSE(cl[8]->connect("mqtt.example", 1883));

  ip_addr_t ip;
  IP4_ADDR(ip_2_ip4(&ip), 10, 0, 0, 5);
  FakeLwip::fire_dns_found(0, &ip);
  TEST_ASSERT_EQUAL_INT(1, FakeLwip::counts().tcp_connect);

  for (int i = 0; i < 9; i++) delete cl[i];
}

static void registry_reuse(void) {
  AsyncClient* cl[8];
  for (int i = 0; i < 8; i++) {
    cl[i] = new AsyncClient();
    TEST_ASSERT_TRUE(cl[i]->connect("mqtt.example", 1883));
  }

  TEST_ASSERT_TRUE(cl[0]->connect("mqtt.example", 1883));
  for (int i = 0; i < 8; i++) delete cl[i];
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(connect_allocs);
  RUN_TEST(establish_cbs);
  RUN_TEST(connect_twice);
  RUN_TEST(closed_reconnects);
  RUN_TEST(replaced_pcb);
  RUN_TEST(connected_cb);
  RUN_TEST(recv_no_ack);
  RUN_TEST(late_recv);
  RUN_TEST(remote_ip_gone);
  RUN_TEST(timewait_pcb);
  RUN_TEST(error_cb_freed);
  RUN_TEST(close_disconnect);
  RUN_TEST(error_disconnect);
  RUN_TEST(dns_fail_disconnect);
  RUN_TEST(dtor_no_disconnect);
  RUN_TEST(disconnect_deletes);
  RUN_TEST(error_deletes);
  RUN_TEST(dns_fail_deletes);
  RUN_TEST(write_sndbuf);
  RUN_TEST(write_after_close);
  RUN_TEST(dtor_closes);
  RUN_TEST(dtor_mid_handshake);
  RUN_TEST(recv_gone);
  RUN_TEST(poll_gone);
  RUN_TEST(sent_gone);
  RUN_TEST(connected_gone);
  RUN_TEST(error_gone);
  RUN_TEST(write_freed_pcb);
  RUN_TEST(close_freed_pcb);
  RUN_TEST(recv_freed_pcb);
  RUN_TEST(accept_gone);
  RUN_TEST(connect_by_name);
  RUN_TEST(dns_ok_connects);
  RUN_TEST(dns_fail_no_connect);
  RUN_TEST(dns_no_pcb);
  RUN_TEST(dns_closed);
  RUN_TEST(found_cb_gone);
  RUN_TEST(registry_full);
  RUN_TEST(registry_reuse);
  return UNITY_END();
}
