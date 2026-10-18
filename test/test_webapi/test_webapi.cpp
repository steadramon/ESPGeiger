/*
  test_webapi - the handshake schedule and the exact bytes WebAPI sends.

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

#include "wapi_fakes.h"
#include <unity.h>
#include <stdint.h>
#include <string>
#include <LittleFS.h>

#include "Corium/System/Clock.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/TickProfile.h"
#include "EGHttpClient.h"
#include "WebAPI/MsgPack.h"
#define private public
#include "WebAPI/WebAPI.h"
#undef private

size_t post_body(uint8_t* buf, size_t cap, uint32_t id, bool census, bool health);
size_t handshake_body(uint8_t* buf, size_t cap, const char* pubKey, uint8_t mode, bool crash);
bool webapi_denied(EGHttpClient* r);
uint32_t first_wake(uint32_t now);

void setUp(void) {
  wapi = {};
  wapi.warm = true;
  wapi.cpm = 22.4f;
  wapi.usv = 0.1503f;
  wapi.uptime = 3600;
  wapi.heap = 24000;
  wapi.reset_reason = 1;
  Wifi::rssi = -61;
  TickProfile::lps = 1500;
  TickProfile::tick_max_us = 120;
  fake_now = 1760000000;
  _fast_ms_counter = 0;
  Clock::resync = false;
}
void tearDown(void) {}

static const uint32_t HOUR_MS = 60UL * 60UL * 1000UL;

#define KEY_11 "111111111111111111111111111111111111111111111111"
#define KEY_00 "001111111111111111111111111111111111111111111111"

static void load_key(WebAPI& w) {
  LittleFS.files.clear();
  LittleFS.files["/api.key"] = KEY_11 "\r\n";
  w.begin();
}

static std::string id_reply(uint32_t id) {
  uint8_t r[16];
  MsgPack::Writer mp(r, sizeof(r));
  mp.map(1);
  mp.kv("id", id);
  return std::string((const char*)r, mp.length());
}

// handshake schedule

static void hs_fresh(void) {
  WebAPI w;
  TEST_ASSERT_EQUAL_UINT32(0, w.nextHandshake);
  TEST_ASSERT_EQUAL_UINT32(30000, w._hs_backoff_ms);
}

static void hs_backoff(void) {
  WebAPI w;
  _fast_ms_counter = 1000;
  w.backoffHandshake();
  TEST_ASSERT_EQUAL_UINT32(1000 + 30000, w.nextHandshake);
  TEST_ASSERT_EQUAL_UINT32(60000, w._hs_backoff_ms);
  w.backoffHandshake();
  TEST_ASSERT_EQUAL_UINT32(1000 + 60000, w.nextHandshake);
  TEST_ASSERT_EQUAL_UINT32(120000, w._hs_backoff_ms);
  for (int i = 0; i < 5; i++) w.backoffHandshake();
  TEST_ASSERT_EQUAL_UINT32(300000, w._hs_backoff_ms);
}

// 0 means not set
static void hs_never_zero(void) {
  WebAPI w;
  _fast_ms_counter = 0u - 30000;
  w.backoffHandshake();
  TEST_ASSERT_EQUAL_UINT32(1, w.nextHandshake);
}

static void hs_due(void) {
  WebAPI w;
  load_key(w);
  w.nextHandshake = 5000;
  w.loop(4999);
  TEST_ASSERT_TRUE(w.request.body.empty());
  _fast_ms_counter = 5000;
  w.loop(5000);
  TEST_ASSERT_FALSE(w.request.body.empty());
  TEST_ASSERT_EQUAL_UINT32(5000 + HOUR_MS, w.nextHandshake);
}

static void hs_accepted(void) {
  WebAPI w;
  for (int i = 0; i < 3; i++) w.backoffHandshake();
  _fast_ms_counter = 1000;
  w.request.reply = id_reply(4321);
  w.onHandshake();
  TEST_ASSERT_EQUAL_UINT32(4321, w.station_id);
  TEST_ASSERT_EQUAL_UINT32(30000, w._hs_backoff_ms);
}

// key bytes 4-7 pick the slot in the hour
static uint32_t slot_wait(uint32_t k, time_t epoch) {
  WebAPI w;
  w.pub_k[4] = (uint8_t)(k >> 24);
  w.pub_k[5] = (uint8_t)(k >> 16);
  w.pub_k[6] = (uint8_t)(k >> 8);
  w.pub_k[7] = (uint8_t)k;
  fake_now = epoch;
  _fast_ms_counter = 1000;
  w.request.reply = id_reply(4321);
  w.onHandshake();
  return w.nextHandshake - 1000;
}

static void hs_slot_wait(void) {
  TEST_ASSERT_EQUAL_UINT32(540000, slot_wait(600000, 60));
  TEST_ASSERT_EQUAL_UINT32(HOUR_MS - 60000, slot_wait(0, 60));   // past it, rest of the hour
  TEST_ASSERT_EQUAL_UINT32(0, slot_wait(60000, 60));
}

// 403 drops the station, next handshake on its slot
static void hs_403(void) {
  WebAPI w;
  w.station_id = 77;
  w.nextHandshake = HOUR_MS;
  w._sent = WebAPI::SENT_POST;
  w.request.code = 403;
  w.request.done = true;
  wapi.hs_off = 50000;
  w.loop(1000);
  TEST_ASSERT_EQUAL_UINT32(0, w.station_id);
  TEST_ASSERT_EQUAL_UINT32(51000, w.nextHandshake);
}

static void hs_slot(void) {
  wapi.hs_off = 50000;
  TEST_ASSERT_EQUAL_UINT32(51000, first_wake(1000));
}

static void hs_crash(void) {
  wapi.hs_off = 50000;
  wapi.reset_reason = 4;
  wapi.exc = true;
  TEST_ASSERT_EQUAL_UINT32(1000, first_wake(1000));
}

static void hs_snap(void) {
  wapi.hs_off = 50000;
  wapi.snap = true;
  TEST_ASSERT_EQUAL_UINT32(1000, first_wake(1000));
}

// wire

static uint8_t buf[512];

static void same(const uint8_t* want, size_t len, size_t got) {
  TEST_ASSERT_EQUAL_UINT(len, got);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, len);
}

#define ID   0xa2,'i','d', 0xcd,0x10,0xe1               /* 4321 */
#define N    0xa1,'n', 0xce,0x68,0xe7,0x78,0x00         /* 1760000000 */
#define CPM  0xa1,'c', 0xca,0x41,0xb3,0x33,0x33         /* 22.4 */
#define USV  0xa1,'u', 0xca,0x3e,0x19,0xe8,0x3e         /* 0.1503 */
#define HEALTH \
  0xa2,'u','t', 0xcd,0x0e,0x10,                         /* 3600 */ \
  0xa2,'m','h', 0xcd,0x5d,0xc0,                         /* 24000 */ \
  0xa1,'l', 0xcd,0x05,0xdc,                             /* 1500 */ \
  0xa2,'t','k', 0x78,                                   /* 120 */ \
  0xa2,'r','s', 0xd0,0xc3                               /* -61 */
#define TEMP 0xa1,'t', 0xca,0x41,0xac,0x00,0x00         /* 21.5 */
#define HUM  0xa1,'h', 0xca,0x42,0x35,0x00,0x00         /* 45.25 */
#define PRES 0xa1,'p', 0xca,0x44,0x7d,0x4c,0xcd         /* 1013.2 */

static void post(void) {
  static const uint8_t want[] = { 0x84, ID, N, CPM, USV };
  same(want, sizeof(want), post_body(buf, sizeof(buf), 4321, false, false));
}

static void post_health(void) {
  static const uint8_t want[] = { 0x89, ID, N, CPM, USV, HEALTH };
  same(want, sizeof(want), post_body(buf, sizeof(buf), 4321, false, true));
}

static void post_env(void) {
  static const uint8_t want[] = { 0x87, ID, N, CPM, USV, TEMP, HUM, PRES };
  wapi.env = true;
  wapi.t = 21.5f; wapi.h = 45.25f; wapi.p = 1013.2f;
  same(want, sizeof(want), post_body(buf, sizeof(buf), 4321, false, false));
}

static void post_env_gap(void) {
  static const uint8_t want[] = { 0x86, ID, N, CPM, USV, TEMP, PRES };
  wapi.env = true;
  wapi.t = 21.5f; wapi.h = NAN; wapi.p = 1013.2f;
  same(want, sizeof(want), post_body(buf, sizeof(buf), 4321, false, false));
}

static void post_census(void) {
  static const uint8_t want[] = { 0x87, ID, N, HEALTH };
  wapi.env = true;
  wapi.t = 21.5f; wapi.h = 45.25f; wapi.p = 1013.2f;
  same(want, sizeof(want), post_body(buf, sizeof(buf), 4321, true, true));
}

static void post_full(void) {
  TEST_ASSERT_EQUAL_UINT(0, post_body(buf, 20, 4321, false, true));
}

#define HS_HEAD \
  0xa1,'n', 0xce,0x68,0xe7,0x78,0x00, \
  0xa2,'p','k', 0xa3,'P','K','1', \
  0xa2,'c','i', 0xa6,'c','0','f','f','e','e', \
  0xa1,'v', 0xae,'0','.','1','3','.','0','/','a','b','c','1','2','3','4', \
  0xa2,'b','d', 0xa7,'E','S','P','8','2','6','6', \
  0xa2,'g','m', 0xad,'e','s','p','8','2','6','6','_','p','u','l','s','e', \
  0xa2,'g','c', 0xa6,'S','B','M','-','2','0', \
  0xa2,'t','d', 0x06, \
  0xa2,'f','l', 0xcd,0x01,0x03

static void handshake(void) {
  static const uint8_t want[] = { 0x8b, HS_HEAD,
    0xa2,'r','r', 0x01,
    0xa1,'m', 0x02 };
  same(want, sizeof(want), handshake_body(buf, sizeof(buf), "PK1", 2, false));
}

// > 15 entries, map16
static void handshake_crash(void) {
  static const uint8_t want[] = { 0xde,0x00,0x13, HS_HEAD,
    0xa2,'r','r', 0x04,
    0xa1,'m', 0x01,
    0xa2,'l','a', 0xca,0x42,0x4e,0x0a,0x3d,              /* 51.51 */
    0xa2,'l','o', 0xca,0xbe,0x05,0x1e,0xb8,              /* -0.13 */
    0xa2,'e','c', 0x1c,
    0xa2,'p','c', 0xce,0x40,0x20,0x10,0x00,
    0xa2,'v','a', 0xce,0x3f,0xfe,0x80,0x00,
    0xa2,'e','2', 0x00,
    0xa2,'e','3', 0x00,
    0xa2,'d','p', 0xce,0x40,0x21,0xa0,0x00 };
  wapi.lat = 51.5074f;
  wapi.lon = -0.1278f;
  wapi.reset_reason = 4;
  wapi.exc = true;
  wapi.exccause = 28;
  wapi.epc1 = 0x40201000;
  wapi.excvaddr = 0x3ffe8000;
  wapi.depc = 0x4021a000;
  same(want, sizeof(want), handshake_body(buf, sizeof(buf), "PK1", 1, true));
}

uint32_t hs_reply(uint32_t id, bool arrived);

static void reply_taken(void) {
  TEST_ASSERT_EQUAL_UINT32(4321, hs_reply(4321, true));
  TEST_ASSERT_EQUAL_UINT32(0, hs_reply(4321, false));
}

bool forget_off(void);

static void forget_sent(void) {
  TEST_ASSERT_TRUE(forget_off());
}

bool hs_refused(void);

static void connect_fail(void) {
  TEST_ASSERT_TRUE(hs_refused());
}

bool key_kept(const char* file);
bool key_refused(const char* file);

static void key_load(void) {
  TEST_ASSERT_TRUE(key_kept(KEY_11 "\r\n"));
  TEST_ASSERT_TRUE(key_kept(KEY_11));
  TEST_ASSERT_TRUE(key_kept("ABCDEF" "111111" "111111" "111111" "111111" "111111" "111111" "111111" "\r\n"));
}

// a key can start with 0
static void key_zero_first(void) {
  TEST_ASSERT_TRUE(key_kept(KEY_00 "\r\n"));
}

static void key_bad(void) {
  TEST_ASSERT_TRUE(key_refused("1111111111111111111111111111111111111111\r\n"));
  TEST_ASSERT_TRUE(key_refused("11111111111111111111zz11111111111111111111111111\r\n"));
  TEST_ASSERT_TRUE(key_refused(KEY_11 "11\r\n"));
  TEST_ASSERT_TRUE(key_refused("000000000000000000000000000000000000000000000000\r\n"));
  TEST_ASSERT_TRUE(key_refused(""));
}

static bool deny(const char* body) {
  EGHttpClient r;
  r.code = 403;
  r.reply = body;
  return webapi_denied(&r);
}

static void clock_off(void) {
  TEST_ASSERT_TRUE(deny("Clock off"));
  TEST_ASSERT_TRUE(Clock::resync);
}

static void other_403(void) {
  TEST_ASSERT_FALSE(deny("Not authorized"));
  TEST_ASSERT_FALSE(deny(""));
  TEST_ASSERT_FALSE(deny("Clock of"));
  TEST_ASSERT_FALSE(deny("Clock off\n"));
  TEST_ASSERT_FALSE(deny("clock off"));
  TEST_ASSERT_FALSE(Clock::resync);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(connect_fail);
  RUN_TEST(key_load);
  RUN_TEST(key_zero_first);
  RUN_TEST(key_bad);
  RUN_TEST(clock_off);
  RUN_TEST(other_403);
  RUN_TEST(post);
  RUN_TEST(post_health);
  RUN_TEST(post_env);
  RUN_TEST(post_env_gap);
  RUN_TEST(post_census);
  RUN_TEST(post_full);
  RUN_TEST(handshake);
  RUN_TEST(handshake_crash);
  RUN_TEST(hs_slot);
  RUN_TEST(hs_crash);
  RUN_TEST(hs_snap);
  RUN_TEST(reply_taken);
  RUN_TEST(forget_sent);
  RUN_TEST(hs_fresh);
  RUN_TEST(hs_backoff);
  RUN_TEST(hs_never_zero);
  RUN_TEST(hs_due);
  RUN_TEST(hs_accepted);
  RUN_TEST(hs_slot_wait);
  RUN_TEST(hs_403);
  return UNITY_END();
}
