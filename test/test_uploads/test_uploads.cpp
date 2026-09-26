/*
  test_uploads - the exact URL each uploader sends for a known reading.

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

// services parse these, byte for byte

#include "up_fakes.h"
#include <unity.h>
#include "Radmon/Radmon.h"
#include "Thingspeak/Thingspeak.h"
#include "GMC/GMC.h"
#include "Webhook/Webhook.h"
#include "Corium/Util/Words.h"
#include <EGJson.h>

struct TRadmon : Radmon         { using Radmon::prepare; using Radmon::interpret; };
struct TThingspeak : Thingspeak { using Thingspeak::prepare; using Thingspeak::interpret; };
struct TGMC : GMC               { using GMC::prepare; using GMC::interpret; };
struct TWebhook : Webhook       { using Webhook::prepare; using Webhook::interpret; };

extern char last_log[256];
static char url[384];
static const char* body;

void setUp(void) {
  // differ, so a swap shows
  reading = { true, 22.6f, 21.3f, 20.1f, 0.1523f, 0.1418f, 0.1483f };
  more = { 22.4f, 0.37f, 151.0f, 0, true, false, false, false, false, 0, true };
  gcounter.total_clicks = 1234;
  env = { false, NAN, NAN, NAN };
  test_mode = false;
  FakePrefs::reset();
  url[0] = '\0';
  body = nullptr;
  last_log[0] = '\0';
}

// real replies off a station

static void radmon_reply(void) {
  TRadmon r;
  TEST_ASSERT_TRUE(r.interpret("OK<br>"));
  TEST_ASSERT_FALSE(r.interpret("There is no user by that name, please register."));
  TEST_ASSERT_EQUAL_STRING("Radmon: Username incorrect, please check", last_log);
  TEST_ASSERT_FALSE(r.interpret("OK but not quite"));
  TEST_ASSERT_FALSE(r.interpret("<html>OK<br></html>"));
  TEST_ASSERT_FALSE(r.interpret(""));
}

static void gmc_reply(void) {
  TGMC g;
  TEST_ASSERT_TRUE(g.interpret("OK.ERR0"));
  TEST_ASSERT_FALSE(g.interpret("OK.ERR1"));
  TEST_ASSERT_EQUAL_STRING("GMC: User is not found", last_log);
  TEST_ASSERT_FALSE(g.interpret("x.ERR2"));
  TEST_ASSERT_EQUAL_STRING("GMC: Geiger Counter is not found", last_log);
  TEST_ASSERT_FALSE(g.interpret("OK"));
  TEST_ASSERT_FALSE(g.interpret("ERR0"));
  TEST_ASSERT_FALSE(g.interpret("Error 500"));
  TEST_ASSERT_FALSE(g.interpret(""));
}

static void ts_reply(void) {
  TThingspeak t;
  TEST_ASSERT_TRUE(t.interpret("1179514"));
  TEST_ASSERT_FALSE(t.interpret("0"));
  TEST_ASSERT_FALSE(t.interpret(""));
  TEST_ASSERT_FALSE(t.interpret("<html>"));
  TEST_ASSERT_FALSE(t.interpret("12a"));
  TEST_ASSERT_FALSE(t.interpret("1.5"));
}

static void webhook_reply(void) {
  TWebhook w;
  TEST_ASSERT_TRUE(w.interpret("OK"));
  TEST_ASSERT_FALSE(w.interpret("no\n00:00:00 MQTT: fake"));
  TEST_ASSERT_EQUAL_STRING("Webhook: Error - no\\n00:00:00 MQTT: fake", last_log);
}
void tearDown(void) {
  free((void*)body);   // malloc'd, as post() frees it
  body = nullptr;
}

static void radmon_url(void) {
  TRadmon r;
  FakePrefs::set("radmon", EG_K_USER, "paul");
  FakePrefs::set("radmon", EG_K_PASSWORD, "p@ss w");
  TEST_ASSERT_TRUE(r.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://radmon.org/radmon.php?function=submit"
                           "&user=paul&password=p%40ss%20w&value=22.6&unit=CPM", url);
  TEST_ASSERT_NULL(body);
}

static void radmon_5min(void) {
  TRadmon r;
  FakePrefs::set("radmon", EG_K_USER, "paul");
  FakePrefs::set("radmon", EG_K_PASSWORD, "pw");
  r.setInterval(300);
  TEST_ASSERT_TRUE(r.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://radmon.org/radmon.php?function=submit"
                           "&user=paul&password=pw&value=21.3&unit=CPM", url);
}

static void radmon_15min(void) {
  TRadmon r;
  FakePrefs::set("radmon", EG_K_USER, "paul");
  FakePrefs::set("radmon", EG_K_PASSWORD, "pw");
  r.setInterval(900);
  TEST_ASSERT_TRUE(r.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://radmon.org/radmon.php?function=submit"
                           "&user=paul&password=pw&value=20.1&unit=CPM", url);
}

static void radmon_skips(void) {
  TRadmon r;
  TEST_ASSERT_FALSE(r.prepare(url, sizeof(url), &body));   // no login
  FakePrefs::set("radmon", EG_K_USER, "paul");
  FakePrefs::set("radmon", EG_K_PASSWORD, "pw");
  reading.warm = false;
  TEST_ASSERT_FALSE(r.prepare(url, sizeof(url), &body));
  reading.warm = true;
  test_mode = true;
  TEST_ASSERT_FALSE(r.prepare(url, sizeof(url), &body));
}

static void thingspeak_url(void) {
  TThingspeak t;
  FakePrefs::set("thingspeak", "channel_key", "ABC123");
  TEST_ASSERT_TRUE(t.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://api.thingspeak.com/update?api_key=ABC123"
                           "&field1=22&field2=0.15230&field3=21&field4=20", url);
  TEST_ASSERT_NULL(body);
}

static void thingspeak_env(void) {
  TThingspeak t;
  FakePrefs::set("thingspeak", "channel_key", "ABC123");
  env = { true, 21.5f, 45.25f, 1013.2f };
  TEST_ASSERT_TRUE(t.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://api.thingspeak.com/update?api_key=ABC123"
                           "&field1=22&field2=0.15230&field3=21&field4=20"
                           "&field5=21.50&field6=45.25&field7=1013.20", url);
}

static void thingspeak_env_gap(void) {
  TThingspeak t;
  FakePrefs::set("thingspeak", "channel_key", "ABC123");
  env = { true, 21.5f, NAN, 1013.2f };
  TEST_ASSERT_TRUE(t.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://api.thingspeak.com/update?api_key=ABC123"
                           "&field1=22&field2=0.15230&field3=21&field4=20"
                           "&field5=21.50&field7=1013.20", url);
}

static void thingspeak_skips(void) {
  TThingspeak t;
  TEST_ASSERT_FALSE(t.prepare(url, sizeof(url), &body));   // no key
  FakePrefs::set("thingspeak", "channel_key", "ABC123");
  reading.warm = false;
  TEST_ASSERT_FALSE(t.prepare(url, sizeof(url), &body));
}

static void gmc_url(void) {
  TGMC g;
  FakePrefs::set("gmc", "aid", "123");
  FakePrefs::set("gmc", "gcid", "456");
  TEST_ASSERT_TRUE(g.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://www.gmcmap.com/log2.asp?AID=123&GID=456"
                           "&CPM=23&ACPM=21.30&uSV=0.1418", url);
  TEST_ASSERT_NULL(body);
}

static void gmc_skips(void) {
  TGMC g;
  TEST_ASSERT_FALSE(g.prepare(url, sizeof(url), &body));   // nothing set
  FakePrefs::set("gmc", "aid", "123");
  TEST_ASSERT_FALSE(g.prepare(url, sizeof(url), &body));   // half set
  FakePrefs::set("gmc", "gcid", "456");
  reading.warm = false;
  TEST_ASSERT_FALSE(g.prepare(url, sizeof(url), &body));
}

static void webhook_body(void) {
  TWebhook w;
  FakePrefs::set("webhook", "url", "http://example.com/hook");
  TEST_ASSERT_TRUE(w.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://example.com/hook", url);
  TEST_ASSERT_EQUAL_STRING("{\"id\":\"c0ffee\",\"ut\":3600,\"cpm\":22.40,\"cpm5\":21.30"
                           ",\"cpm15\":20.10,\"usv\":0.1523,\"cps\":0.37,\"tc\":1234"
                           ",\"mem\":24000,\"rssi\":-61,\"tube\":1,\"sat\":0"
                           ",\"warn\":0,\"alert\":0}", body);
}

static void webhook_key(void) {
  TWebhook w;
  FakePrefs::set("webhook", "url", "example.com/hook");
  FakePrefs::set("webhook", "key", "se\"cr\\t");
  more.alert = true;
  TEST_ASSERT_TRUE(w.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("{\"id\":\"c0ffee\",\"key\":\"se\\\"cr\\\\t\",\"ut\":3600"
                           ",\"cpm\":22.40,\"cpm5\":21.30,\"cpm15\":20.10,\"usv\":0.1523"
                           ",\"cps\":0.37,\"tc\":1234,\"mem\":24000,\"rssi\":-61"
                           ",\"tube\":1,\"sat\":0,\"warn\":0,\"alert\":1}", body);
}

static void webhook_https(void) {
  TWebhook w;
  FakePrefs::set("webhook", "url", "https://h.io/x");
  TEST_ASSERT_TRUE(w.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://h.io/x", url);
  free((void*)body);
  body = nullptr;
  FakePrefs::reset();
  FakePrefs::set("webhook", "url", "h.io/x");
  TEST_ASSERT_TRUE(w.prepare(url, sizeof(url), &body));
  TEST_ASSERT_EQUAL_STRING("http://h.io/x", url);
}

// 255 quotes -> 510 bytes
static void webhook_long(void) {
  TWebhook w;
  char key[256];
  memset(key, '"', 255);
  key[255] = '\0';
  FakePrefs::set("webhook", "url", "h.io/x");
  FakePrefs::set("webhook", "key", key);
  TEST_ASSERT_FALSE(w.prepare(url, sizeof(url), &body));
}

// cut is not sent
static void url_cut(void) {
  char small[40];
  FakePrefs::set("radmon", EG_K_USER, "paul");
  FakePrefs::set("radmon", EG_K_PASSWORD, "pw");
  FakePrefs::set("thingspeak", "channel_key", "ABC123");
  FakePrefs::set("gmc", "aid", "123");
  FakePrefs::set("gmc", "gcid", "456");
  FakePrefs::set("webhook", "url", "example.com/a/hook/path/past/the/cap");
  TRadmon r;
  TThingspeak t;
  TGMC g;
  TWebhook w;
  TEST_ASSERT_FALSE(r.prepare(small, sizeof(small), &body));
  TEST_ASSERT_NOT_NULL(strstr(last_log, "URL too long"));
  last_log[0] = '\0';
  TEST_ASSERT_FALSE(t.prepare(small, sizeof(small), &body));
  TEST_ASSERT_NOT_NULL(strstr(last_log, "URL too long"));
  last_log[0] = '\0';
  TEST_ASSERT_FALSE(g.prepare(small, sizeof(small), &body));
  TEST_ASSERT_NOT_NULL(strstr(last_log, "URL too long"));
  last_log[0] = '\0';
  TEST_ASSERT_FALSE(w.prepare(small, sizeof(small), &body));
  TEST_ASSERT_NOT_NULL(strstr(last_log, "URL too long"));
}

static void webhook_skips(void) {
  TWebhook w;
  TEST_ASSERT_FALSE(w.prepare(url, sizeof(url), &body));   // no URL
  FakePrefs::set("webhook", "url", "h.io/x");
  reading.warm = false;
  TEST_ASSERT_FALSE(w.prepare(url, sizeof(url), &body));
  reading.warm = true;
  test_mode = true;
  TEST_ASSERT_FALSE(w.prepare(url, sizeof(url), &body));
}

static void mqtt_sensor(void) {
  char s[256];
  EGJson j(s, sizeof(s));
  j.map_begin();
  egm_json_fields(EGM_OUT_MQTT, j);
  j.map_end();
  TEST_ASSERT_EQUAL_STRING("{\"cpm\":22.40,\"cpm5\":21.30,\"cpm15\":20.10,\"usv\":0.1523"
                           ",\"cps\":0.37,\"tube\":1,\"sat\":0,\"warn\":0,\"alert\":0}", s);
}

static void history(void) {
  int h[64];
  gcounter.cpm_history.clear();
  TEST_ASSERT_EQUAL_size_t(0, egm_history(h, 64));
  for (int i = 1; i <= 50; i++) gcounter.cpm_history.push(i);
  TEST_ASSERT_EQUAL_size_t(45, egm_history(h, 64));
  TEST_ASSERT_EQUAL_INT(6, h[0]);
  TEST_ASSERT_EQUAL_INT(50, h[44]);
  TEST_ASSERT_EQUAL_size_t(3, egm_history(h, 3));
  TEST_ASSERT_EQUAL_INT(48, h[0]);
  TEST_ASSERT_EQUAL_INT(50, h[2]);
  gcounter.cpm_history.clear();
}

static void by_id(void) {
  more.missing = true;
  more.blip = 123456;
  more.alert = true;
  reading.warm = false;
  TEST_ASSERT_EQUAL_FLOAT(22.4f, egm_f(PSTR("cpm")));
  TEST_ASSERT_EQUAL_FLOAT(21.3f, egm_f(PSTR("cpm5")));
  TEST_ASSERT_EQUAL_FLOAT(20.1f, egm_f(PSTR("cpm15")));
  TEST_ASSERT_TRUE(egm_b(PSTR("alert")));
  TEST_ASSERT_EQUAL_FLOAT(0.1523f, egm_f(PSTR("usv")));
  TEST_ASSERT_EQUAL_FLOAT(0.37f, egm_f(PSTR("cps")));
  TEST_ASSERT_EQUAL_FLOAT(151.0f, egm_f(PSTR("ratio")));
  TEST_ASSERT_FALSE(egm_b(PSTR("warm")));
  TEST_ASSERT_TRUE(egm_b(PSTR("no_counts")));
  TEST_ASSERT_EQUAL_UINT32(123456, egm_u(PSTR("blip")));
  TEST_ASSERT_EQUAL_FLOAT(22.6f, egm_f(PSTR("cpm_st")));
  TEST_ASSERT_EQUAL_FLOAT(0.1483f, egm_f(PSTR("usv_st")));
  TEST_ASSERT_EQUAL_FLOAT(0.1418f, egm_f(PSTR("usv5")));
  TEST_ASSERT_TRUE(egm_b(PSTR("ser_ok")));
  more.ser_ok = false;
  TEST_ASSERT_FALSE(egm_b(PSTR("ser_ok")));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(radmon_url);
  RUN_TEST(radmon_5min);
  RUN_TEST(radmon_15min);
  RUN_TEST(radmon_skips);
  RUN_TEST(thingspeak_url);
  RUN_TEST(thingspeak_env);
  RUN_TEST(thingspeak_env_gap);
  RUN_TEST(thingspeak_skips);
  RUN_TEST(gmc_url);
  RUN_TEST(gmc_skips);
  RUN_TEST(webhook_body);
  RUN_TEST(webhook_key);
  RUN_TEST(webhook_https);
  RUN_TEST(webhook_long);
  RUN_TEST(webhook_skips);
  RUN_TEST(url_cut);
  RUN_TEST(mqtt_sensor);
  RUN_TEST(radmon_reply);
  RUN_TEST(gmc_reply);
  RUN_TEST(ts_reply);
  RUN_TEST(webhook_reply);
  RUN_TEST(history);
  RUN_TEST(by_id);
  return UNITY_END();
}
