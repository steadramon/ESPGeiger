/*
  WebAPI.cpp - Signed-submission remote API client.

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
#ifdef WEBAPIOUT

#ifndef WEBAPI_URL
#define WEBAPI_URL "http://api.espgeiger.com"
#endif

#include <Arduino.h>
#include <EGHttpServer.h>
#include "WebAPI.h"
#include "Features.h"
#include <EGBase64.h>   // no heap
#include "Corium/Logger/Logger.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Clock.h"
#include "GRNG/sha256.h"
#include "GRNG/GRNG.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/CrashDump.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/WebPortal/WebPortal.h"
#include "MsgPack.h"
#include <FS.h>
#include "Corium/System/Fs.h"
#include <uECC.h>
#include "EnvSensor/EnvSensor.h"
#include "Counter/Tube.h"
#include "Corium/Measure/EGMeasure.h"
#ifdef ESPG_HV_ADC
#include "HV/HV.h"
#endif

WebAPI webapi;
EG_REGISTER_MODULE(webapi)

EG_PSTR(M_WARM,   "warm");
EG_PSTR(M_CPM_ST, "cpm_st");
EG_PSTR(M_USV_ST, "usv_st");

EG_PSTR(WA_L_MD,  "Sharing mode");
EG_PSTR(WA_O_MD,  "Off|Heartbeat|CPM readings");
EG_PSTR(WA_L_LAT, "Latitude");
EG_PSTR(WA_H_LAT, "Station latitude  (-90..90, 0 = use IP)");
EG_PSTR(WA_L_LON, "Longitude");
EG_PSTR(WA_H_LON, "Station longitude (-180..180, 0 = use IP)");

static const EGPref WEBAPI_PREF_ITEMS[] PROGMEM = {
  {EG_K_MODE, WA_L_MD,  nullptr,  "2", WA_O_MD, 0,    2,   0, EGP_ENUM,  0},
  {EG_K_LAT,  WA_L_LAT, WA_H_LAT, "0", nullptr, -90,  90,  0, EGP_FLOAT, 0},
  {EG_K_LON,  WA_L_LON, WA_H_LON, "0", nullptr, -180, 180, 0, EGP_FLOAT, 0},
};

static const EGPrefGroup WEBAPI_PREF_GROUP PROGMEM = {
  "webapi", "ESPGeiger Network", 3,
  WEBAPI_PREF_ITEMS,
  sizeof(WEBAPI_PREF_ITEMS) / sizeof(WEBAPI_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, EG_K_MODE,
};

const EGPrefGroup* WebAPI::prefs_group() { return &WEBAPI_PREF_GROUP; }

void WebAPI::on_prefs_loaded() {
  _mode = (uint8_t)EGPrefs::getUInt("webapi", EG_K_MODE);
  bool off = _mode == 0 && !_forget && request.idle();
  EGModuleRegistry::set_loop_interval(this, off ? -1 : 1000);
}

static int webapi_rng(uint8_t *dest, unsigned size) {
  GRNG::extract(dest, size);
  return 1;
}

static void bytestostr(const uint8_t* array, unsigned int len, char* buffer) {
  hex_encode(buffer, array, len);
  buffer[len*2] = '\0';
}

static const char HS_OK[] PROGMEM = "WebAPI: Handshake OK - station ID %u";

static void signedHeaders(EGHttpClient& r, const char* sig) {
  r.setReqHeader(FPSTR(HTTP_H_UA), DeviceInfo::useragent());
  r.setReqHeader(FPSTR(HTTP_H_CTYPE), F("application/msgpack"));
  r.setReqHeader(F("X-Auth"), sig);
  const uint16_t secs = 10;
  r.setTimeout(secs);
  r.setDeadline(secs * 2000UL);   // whole request
}

// hasher on the stack, noinline so it's gone before uECC_sign
static void __attribute__((noinline)) hash_body(const uint8_t* p, size_t n, uint8_t* out) {
  Sha256Class sha;
  sha.init();
  sha.write(p, n);
  memcpy(out, sha.result(), HASH_LENGTH);
}

static const char CLOCK_OFF[] PROGMEM = "Clock off";

static bool denied(EGHttpClient* r) {
  uint8_t b[16];
  size_t n = r->responseRead(b, sizeof(b));
  const size_t len = sizeof(CLOCK_OFF) - 1;
  bool off = n == len && memcmp_P(b, CLOCK_OFF, len) == 0;
  if (off) {
    Clock::resync = true;
    Log::console(PSTR("WebAPI: Clock off, re-syncing NTP"));
  }
  return off;
}

WebAPI::WebAPI() {}

void WebAPI::begin() {
  uECC_set_rng(&webapi_rng);
  GRNG::stir();
  loadConfig();
  Log::debug(PSTR("WebAPI: Ready - endpoint=%s, key=%s"),
    PSTR(WEBAPI_URL), _have_key ? PSTR("loaded") : PSTR("none (will generate)"));
}

void WebAPI::pingIfDue(unsigned long now) {
  if (lastPing == 0) {
    if (_ping_off == 0xFFFFFFFFu)
      _ping_off = EGModuleRegistry::initial_offset(name(), pingIntervalMs);
    lastPing = EGModuleRegistry::ping_from_offset(_ping_off, now, pingIntervalMs);
  } else if ((long)(now - lastPing) >= (long)pingIntervalMs) {
    while ((long)(now - lastPing) >= (long)pingIntervalMs) lastPing += pingIntervalMs;

    const bool healthDue = (healthPostCounter == 0);
    if (_mode == 2 || healthDue) {
      postMeasurement(_mode != 2);
    } else {
      if (++healthPostCounter >= WEBAPI_HEALTH_EVERY) healthPostCounter = 0;
    }
  }
}

unsigned long WebAPI::wakeTarget() {
  unsigned long target = nextHandshake;
  if (station_id != 0 && lastPing != 0) {
    unsigned long ping_target = lastPing + pingIntervalMs;
    if ((long)(ping_target - target) < 0) target = ping_target;
  }
  return target;
}

static bool crash_to_send() {
  return CrashDump::hasCrash() ||
         DeviceInfo::resetExc(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void WebAPI::loop(unsigned long now) {
  if (request.takeDone()) take_reply();
  if (_forget && request.idle()) {
    _forget = false;
    sendForget();
  }
  if (_mode == 0) {
    // forget reply still due
    EGModuleRegistry::set_loop_interval(this, request.idle() ? -1 : loop_interval_ms());
    return;
  }

  if (nextHandshake == 0) {
    // own slot, a crash boot goes at once
    bool first = _hs_off == 0xFFFFFFFFu;
    if (first)
      _hs_off = EGModuleRegistry::initial_offset(PSTR("wapi.hs"), pingIntervalMs);
    handshakeAt((uint32_t)now + (first && crash_to_send() ? 0 : _hs_off));
  } else if ((int32_t)((uint32_t)now - nextHandshake) >= 0) {
    doHandshake();
  } else if (station_id != 0) {
    pingIfDue(now);
  }
  if (!request.idle()) {
    EGModuleRegistry::set_loop_interval(this, loop_interval_ms());   // reply due, poll
    return;
  }
  EGModuleRegistry::sleep_until(this, now, wakeTarget());
}

void WebAPI::take_reply() {
  uint8_t sent = _sent;
  _sent = SENT_NONE;
  if (sent == SENT_HS)          onHandshake();
  else if (sent == SENT_POST)   onPost();
  else if (sent == SENT_FORGET) onForget();
}

namespace {
struct HsCrash {
  const CrashDump::Snapshot* cd = nullptr;
  uint32_t epc1 = 0, excvaddr = 0, epc2 = 0, epc3 = 0, depc = 0;
  uint8_t  exccause = 0;
  bool     sendExc   = false;
  bool     sendExtra = false;
  bool     sendStack = false;   // RTC snapshot only
};
}

// rst_info when CrashDump missed it
static void collect_crash(HsCrash& c) {
  c.cd = CrashDump::lastCrash();
  if (c.cd) {
    c.epc1     = c.cd->epc1;
    c.excvaddr = c.cd->excvaddr;
    c.exccause = (uint8_t)c.cd->exccause;
    c.epc2     = c.cd->epc2;
    c.epc3     = c.cd->epc3;
    c.depc     = c.cd->depc;
    c.sendExc   = true;
    c.sendExtra = c.epc2 || c.epc3 || c.depc;
    c.sendStack = (c.cd->word_count > 0) || c.cd->sp;
  } else if (DeviceInfo::resetExc(&c.epc1, &c.excvaddr, &c.exccause,
                                  &c.epc2, &c.epc3, &c.depc)) {
    c.sendExc   = true;
    c.sendExtra = c.epc2 || c.epc3 || c.depc;
  }
}

static void write_crash(MsgPack::Writer& mp, const HsCrash& c) {
  uint16_t stackBytes = (c.cd && c.cd->word_count > 0) ? (c.cd->word_count * 4) : 0;
  if (c.sendExc) {
    mp.kv(PSTR("ec"), (uint32_t)c.exccause);
    mp.kv(PSTR("pc"), c.epc1);
    mp.kv(PSTR("va"), c.excvaddr);
  }
  if (c.sendExtra) {
    mp.kv(PSTR("e2"), c.epc2);
    mp.kv(PSTR("e3"), c.epc3);
    mp.kv(PSTR("dp"), c.depc);
  }
  if (c.sendStack) {
    mp.kv(PSTR("sp"), c.cd->sp);
    // LE u32 words
    mp.kv(PSTR("st"), (const uint8_t*)c.cd->stack, stackBytes);
  }
}

static void write_handshake(MsgPack::Writer& mp, const char* pubKey, uint8_t mode,
                            const HsCrash& c) {
  char versionStr[48];
  if (GIT_VERSION[0] != '\0') {
    snprintf_P(versionStr, sizeof(versionStr), PSTR("%s/%s"), RELEASE_VERSION, GIT_VERSION);
  } else {
    strlcpy(versionStr, RELEASE_VERSION, sizeof(versionStr));
  }

  // 2dp (~1km) for privacy, 0,0 = use IP
  float latF = EGPrefs::getFloat("webapi", EG_K_LAT);
  float lonF = EGPrefs::getFloat("webapi", EG_K_LON);
  bool sendCoords = (latF != 0.0f || lonF != 0.0f);

  mp.map_begin();
  mp.kv(PSTR("n"),  Clock::epoch());
  mp.kv(PSTR("pk"), pubKey);
  mp.kv(PSTR("ci"), DeviceInfo::chipid());
  mp.kv(PSTR("v"),  versionStr);
  mp.kv(PSTR("bd"), DeviceInfo::chipmodel());
  mp.kv(PSTR("gm"), BUILD_ENV);
  mp.kv(PSTR("gc"), Tube::model());
  mp.kv(PSTR("td"), (uint32_t)Tube::detection());
  mp.kv(PSTR("fl"), (uint32_t)Features::flags());
  mp.kv(PSTR("rr"), (uint32_t)DeviceInfo::resetReason());
  mp.kv(PSTR("m"),  (uint32_t)mode);
  if (sendCoords) {
    mp.kv(PSTR("la"), (int32_t)(latF * 100.0f + (latF >= 0 ? 0.5f : -0.5f)) * 0.01f);
    mp.kv(PSTR("lo"), (int32_t)(lonF * 100.0f + (lonF >= 0 ? 0.5f : -0.5f)) * 0.01f);
  }
  write_crash(mp, c);
  mp.map_end();
}

void WebAPI::doHandshake() {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("WebAPI: Testmode"));
    handshakeAt(fast_millis() + WEBAPI_HANDSHAKE_MS);
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  // before anything can fail, else it spins
  backoffHandshake();

  GRNG::stir();

  if (!_have_key) {
    Log::console(PSTR("WebAPI: Generating keys"));
    GRNG::stir();
    if (uECC_make_key(pub_k, priv_k, uECC_secp192r1()) != 1) return;   // again after the backoff
    _have_key = true;
    optimistic_yield(100 * 1000);
    encode_base64(pub_k, sizeof(pub_k), (uint8_t *)pub_k_64);
    saveConfig();
  }

  Log::debug(PSTR("WebAPI: Handshake -> " WEBAPI_URL "/api/1/handshake"));

  uint8_t buffer[512];   // ~200B, +180B after a crash

  // first handshake only
  HsCrash crash;
  if (!_exc_sent) collect_crash(crash);

  MsgPack::Writer mp(buffer, sizeof(buffer));
  write_handshake(mp, pub_k_64, _mode, crash);
  if (mp.overflow) {
    Log::console(PSTR("WebAPI: Handshake encode overflow"));
    return;
  }
  size_t bodyLen = mp.length();

  uint8_t signature[WEBAPI_SIG_LEN];
  uint8_t shahash[HASH_LENGTH];
  hash_body(buffer, bodyLen, shahash);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[EGB64_LEN(sizeof(signature)) + 1];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  if (request.idle()) {
    _sent = SENT_HS;
    if (request.open(F("POST"), F(WEBAPI_URL "/api/1/handshake"))) {
      LedSignal::activity();
      signedHeaders(request, basesig);
      if (request.send(buffer, bodyLen)) {
        handshakeAt(fast_millis() + WEBAPI_HANDSHAKE_MS);
        send_indicator = 2;
      } else {
        Log::console(PSTR("WebAPI: Handshake send failed"));
      }
    } else {
      // no reply will come to count it
      _sent = SENT_NONE;
      note_publish(false);
      Log::console(PSTR("WebAPI: Handshake connect failed"));
    }
  }
}

void WebAPI::handshakeAt(uint32_t at) {
  nextHandshake = at ? at : 1;
}

void WebAPI::backoffHandshake() {
  handshakeAt(fast_millis() + _hs_backoff_ms);
  uint32_t next = _hs_backoff_ms * 2;
  _hs_backoff_ms = next > WEBAPI_BACKOFF_MAX ? WEBAPI_BACKOFF_MAX : next;
}

// epoch_s as uint32_t, 2038
static uint32_t wall_clock_wait_ms(uint32_t k, uint32_t interval_ms,
                                   uint32_t period_s, uint32_t epoch_s) {
  uint32_t target_ms = k % interval_ms;
  uint32_t cur_ms    = (epoch_s % period_s) * 1000UL;
  return (target_ms >= cur_ms) ? target_ms - cur_ms
                               : interval_ms - cur_ms + target_ms;
}

void WebAPI::onHandshake() {
  // every exit calls note_publish()
  if (request.responseHTTPcode() == 200) {
    uint8_t r[64];
    size_t n = request.responseRead(r, sizeof(r));
    MsgPack::Reader reader(r, n);
    uint32_t id = 0;
    if (reader.find_key(PSTR("id"))) {
      reader.read_uint(&id);
    }
    if (!reader.error) {
      if (id == 0) {
        Log::debug(PSTR("WebAPI: Handshake rejected (no ID in response)"));
        backoffHandshake();
        EGModuleRegistry::set_loop_interval(this, 100);
        note_publish(false);
        return;
      }
      if (station_id != id) {
        Log::console(HS_OK, id);
        station_id = id;
        uint32_t k = ((uint32_t)pub_k[4] << 24) | ((uint32_t)pub_k[5] << 16)
                   | ((uint32_t)pub_k[6] << 8)  |  (uint32_t)pub_k[7];
        handshakeAt(fast_millis() +
          wall_clock_wait_ms(k, WEBAPI_HANDSHAKE_MS, 3600, Clock::epoch()));
        EGModuleRegistry::set_loop_interval(this, 100);
      } else {
        Log::debug(HS_OK, id);
      }
      _hs_backoff_ms = WEBAPI_BACKOFF_MS;
      _exc_sent = true;
      note_publish(true);
      return;
    }
    Log::debug(PSTR("WebAPI: Handshake parse error"));
  } else {
    int code = request.responseHTTPcode();
    char why[32];
    strncpy_P(why, (PGM_P)request.responseHTTPStringF(), sizeof(why) - 1);
    why[sizeof(why) - 1] = '\0';
    Log::debug(PSTR("WebAPI: Handshake error %d - %s"), code, why);
    if (code == 403) denied(&request);
    if (code == 429) {
      Log::debug(PSTR("WebAPI: Rate-limited; waiting for next scheduled interval"));
      note_publish(false);
      return;
    }
  }
  // backoff, uECC_sign is slow
  backoffHandshake();
  EGModuleRegistry::set_loop_interval(this, 100);
  note_publish(false);
}

static void write_env(MsgPack::Writer& mp) {
  if (!envsensor.present()) return;
  float t = envsensor.tempC(), h = envsensor.humidity(), p = envsensor.pressure();
  if (!isnan(t)) mp.kv(PSTR("t"), t);
  if (!isnan(h)) mp.kv(PSTR("h"), h);
  if (!isnan(p)) mp.kv(PSTR("p"), p);
}

// float32, no soft-float printer
static void write_readings(MsgPack::Writer& mp) {
  mp.kv(PSTR("c"),  egm_f(M_CPM_ST));
  mp.kv(PSTR("u"),  egm_f(M_USV_ST));
#ifdef ESPG_HV_ADC
  if (hv.get_pwm_pin() >= 0 && hv.get_vd_ratio() != 0) mp.kv(PSTR("hv"), hv.hvReading.get());
#endif
  write_env(mp);
}

static void write_health(MsgPack::Writer& mp) {
  mp.kv(PSTR("ut"), (uint32_t)DeviceInfo::uptime());
  mp.kv(PSTR("mh"), DeviceInfo::freeHeap());
  mp.kv(PSTR("l"),  (uint32_t)TickProfile::lps);
  mp.kv(PSTR("tk"), (uint32_t)TickProfile::tick_max_us);
  mp.kv(PSTR("rs"), (int32_t)Wifi::rssi);
#ifdef ESP32
  float ct = DeviceInfo::chipTempC();
  if (!isnan(ct)) mp.kv(PSTR("ct"), ct);
#endif
#ifdef WEBAPI_TESTMODE_POST
  mp.kv(PSTR("lfb"), DeviceInfo::largestFreeBlockLow());
  DeviceInfo::largestFreeBlockLowReset();
#endif
}

static void write_post(MsgPack::Writer& mp, uint32_t stationId, bool censusOnly,
                       bool sendHealth) {
  mp.map_begin();
  mp.kv(PSTR("id"), stationId);
  mp.kv(PSTR("n"),  Clock::epoch());
  if (!censusOnly) write_readings(mp);
  if (sendHealth)  write_health(mp);
  mp.map_end();
}

void WebAPI::postMeasurement(bool censusOnly) {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("WebAPI: Testmode"));
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  if (!egm_b(M_WARM)) {
    Log::debug(PSTR("WebAPI: Skipping early post (CPM bucket not full)"));
    return;
  }

  Log::debug(PSTR("WebAPI: Posting station %u%s ..."), station_id, censusOnly ? PSTR(" (census)") : PSTR(""));

  uint8_t buffer[192];

  MsgPack::Writer mp(buffer, sizeof(buffer));
  write_post(mp, station_id, censusOnly, healthPostCounter == 0);

  if (mp.overflow) {
    Log::console(PSTR("WebAPI: Post encode overflow"));
    return;
  }
  size_t bodyLen = mp.length();

  uint8_t signature[WEBAPI_SIG_LEN];
  uint8_t shahash[HASH_LENGTH];
  hash_body(buffer, bodyLen, shahash);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    lastPing -= pingIntervalMs;  // retry next loop
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[EGB64_LEN(sizeof(signature)) + 1];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  if (request.idle()) {
    _sent = SENT_POST;
    if (request.open(F("POST"), F(WEBAPI_URL "/api/1/post"))) {
      LedSignal::activity();
      signedHeaders(request, basesig);
      request.send(buffer, bodyLen);
      send_indicator = 2;
    } else {
      _sent = SENT_NONE;
      note_publish(false);
      Log::console(PSTR("WebAPI: Post connect failed"));
    }
  }
}

void WebAPI::onPost() {
  bool ok = false;
  if (request.responseHTTPcode() == 200) {
    uint8_t r[64];
    size_t n = request.responseRead(r, sizeof(r));
    MsgPack::Reader reader(r, n);
    if (reader.find_key(PSTR("ok"))) reader.read_bool(&ok);
    if (reader.error) ok = false;
    if (ok) {
      Log::debug(PSTR("WebAPI: Post OK - station %u"), station_id);
      if (++healthPostCounter >= WEBAPI_HEALTH_EVERY) healthPostCounter = 0;
    } else {
      Log::debug(PSTR("WebAPI: Post response not ok"));
    }
  } else {
    int code = request.responseHTTPcode();
    char why[32];
    strncpy_P(why, (PGM_P)request.responseHTTPStringF(), sizeof(why) - 1);
    why[sizeof(why) - 1] = '\0';
    Log::debug(PSTR("WebAPI: Error %d - %s"), code, why);
    if (code == 403) {
      denied(&request);
      station_id = 0;
      nextHandshake = 0;
      EGModuleRegistry::set_loop_interval(this, 100);
    } else if (code == 409) {
      Log::debug(PSTR("WebAPI: Replay rejected (check NTP clock)"));
    }
  }
  note_publish(ok);
}

void WebAPI::sendForget() {
  if (station_id == 0 || !_have_key) {
    Log::debug(PSTR("WebAPI: Forget skipped (no station)"));
    return;
  }
  if (!Clock::synced) {
    Log::console(PSTR("WebAPI: Forget skipped (clock not synced)"));
    return;
  }

  uint8_t buffer[24];
  MsgPack::Writer mp(buffer, sizeof(buffer));
  mp.map(2);
  mp.kv(PSTR("id"), station_id);
  mp.kv(PSTR("n"),  Clock::epoch());
  if (mp.overflow) return;
  size_t bodyLen = mp.length();

  uint8_t signature[WEBAPI_SIG_LEN];
  uint8_t shahash[HASH_LENGTH];
  hash_body(buffer, bodyLen, shahash);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    Log::console(PSTR("WebAPI: Forget sign failed"));
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[EGB64_LEN(sizeof(signature)) + 1];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  _sent = SENT_FORGET;
  if (request.open(F("POST"), F(WEBAPI_URL "/api/1/forget"))) {
    Log::console(PSTR("WebAPI: Forget station %u"), station_id);
    LedSignal::activity();
    signedHeaders(request, basesig);
    request.send(buffer, bodyLen);
    send_indicator = 2;
  } else {
    _sent = SENT_NONE;
    note_publish(false);
    Log::console(PSTR("WebAPI: Forget connect failed"));
  }
}

void WebAPI::onForget() {
  int code = request.responseHTTPcode();
  // 403 = gone, unless clock off
  bool off = code == 403 && denied(&request);
  bool ok = (code == 200 || code == 403) && !off;
  if (ok) {
    Log::console(PSTR("WebAPI: Station forgotten (HTTP %d)"), code);
    station_id = 0;
    nextHandshake = 0;
    lastPing = 0;
    healthPostCounter = 0;
    EGModuleRegistry::set_loop_interval(this, 100);
  } else {
    char why[32];
    strncpy_P(why, (PGM_P)request.responseHTTPStringF(), sizeof(why) - 1);
    why[sizeof(why) - 1] = '\0';
    Log::console(PSTR("WebAPI: Forget failed - %s"), why);
  }
  note_publish(ok);
}

static const char API_KEY_P[] PROGMEM = "/api.key";
static const char API_BAD_P[] PROGMEM = "/api.bad";
#define API_KEY_PATH FPSTR(API_KEY_P)
#define API_BAD_PATH FPSTR(API_BAD_P)

void WebAPI::saveConfig() {
  Log::console(PSTR("WebAPI: Saving key ..."));
  File f = Fs::open_w(API_KEY_PATH);
  if (!f) {
    Log::console(PSTR("WebAPI: Failed to open api.key"));
    return;
  }
  char private_key[2 * WEBAPI_PRIV_LEN + 1] = "";
  bytestostr(priv_k, sizeof(priv_k), private_key);
  f.println(private_key);
  f.close();
}

void WebAPI::loadConfig() {
  File f = Fs::open_r(API_KEY_PATH);
  if (!f) return;
  char key_hex[2 * WEBAPI_PRIV_LEN + 2];   // hex, \r
  size_t n = f.readBytesUntil('\n', key_hex, sizeof(key_hex) - 1);
  key_hex[n] = '\0';
  f.close();
  if (n && key_hex[n - 1] == '\r') key_hex[n - 1] = '\0';
  // a cut or bad file would sign as some other station
  _have_key = hex_decode(priv_k, key_hex, sizeof(priv_k)) &&
              uECC_compute_public_key(priv_k, pub_k, uECC_secp192r1()) == 1;
  if (_have_key) {
    encode_base64(pub_k, sizeof(pub_k), (uint8_t *)pub_k_64);
    Log::console(PSTR("WebAPI: Key loaded"));
  } else {
    memset(priv_k, 0, sizeof(priv_k));
    // kept to look at
    Fs::remove(API_BAD_PATH);
    Fs::rename(API_KEY_PATH, API_BAD_PATH);
    Log::console(PSTR("WebAPI: Stored key invalid, kept as api.bad"));
  }
}

size_t WebAPI::status_json(char* buf, size_t cap, unsigned long now) {
  if (_mode == 0) return 0;
  return write_status_json(buf, cap, PSTR("webapi"), last_ok, last_attempt_ms, now);
}

// HTTP routes

static const char FORGET_BODY[] PROGMEM =
  "<p>A delete request has been sent to the server. Sharing is now <b>Off</b>.</p>"
  "<p class=muted>Re-enabling sharing later will register a new station; old readings will not be linked.</p>";

// not atof, too big
static bool validDeg(const char* s, int maxWhole) {
  if (!s || !*s) return false;
  if (*s == '-' || *s == '+') s++;
  int w = 0; bool any = false;
  while (*s >= '0' && *s <= '9') {
    w = w * 10 + (*s++ - '0'); any = true;
    if (w > maxWhole) return false;
  }
  if (!any) return false;
  if (*s == '.') { s++; while (*s >= '0' && *s <= '9') s++; }
  return !*s;
}

// false = a field refused, rest still saved
static bool saveSettings(EGHttpRequest& req) {
  bool ok = true;
  // arg() reuses its buffer
  const char* m = req.arg(F("m"));
  if (m && !EGPrefs::put("webapi", EG_K_MODE, m)) ok = false;
  const char* lat = req.arg(FPSTR(EG_K_LAT));
  if (lat) {
    if (!*lat) EGPrefs::put("webapi", EG_K_LAT, "0");
    else if (validDeg(lat, 90)) EGPrefs::put("webapi", EG_K_LAT, lat);
    else ok = false;
  }
  const char* lon = req.arg(FPSTR(EG_K_LON));
  if (lon) {
    if (!*lon) EGPrefs::put("webapi", EG_K_LON, "0");
    else if (validDeg(lon, 180)) EGPrefs::put("webapi", EG_K_LON, lon);
    else ok = false;
  }
  EGPrefs::commit();
  return ok;
}

static void sendStation(EGHttpResponse& res) {
  if (webapi.getStationId() != 0) {
    char status[160];
    int n = snprintf_P(status, sizeof(status),
      PSTR("<p>Station: <a href='" WEBAPI_STATION_URL "' target=_blank>#%u</a></p>"),
      webapi.getStationId(), webapi.getStationId());
    if (n > 0 && (size_t)n < sizeof(status)) res.sendChunk(status, (size_t)n);
  } else {
    res.sendChunk(F(
      "<p>Station: not yet registered (handshake pending) "
      "<button type=button onclick='location.reload()'>Refresh</button></p>"));
  }
}

// POST saves first, both show the page
static void hWebAPI(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const bool posted = req.method() == EGHttpRequest::POST;
  const bool saved = posted && saveSettings(req);

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("ESPGeiger Network"));
  if (saved) res.sendChunk(F("<div class='a ak'>Saved.</div>"));
  if (posted && !saved) res.sendChunk(F("<div class='a ae'>A value was out of range and not saved.</div>"));
  res.sendChunk(F(
    "<p class=muted>ESPGeiger can share data with the public station "
    "network so it appears on the global map. The device identity and "
    "location are obscured before publication.</p>"));

  uint8_t mode = (uint8_t)EGPrefs::getUInt("webapi", EG_K_MODE);
  if (mode > 0) sendStation(res);

  res.sendChunk(F(
    "<form method=POST action=/webapi><fieldset><legend>Sharing</legend>"
    "<label><input type=radio name=m value=0"));
  if (mode == 0) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F("> Off</label>"
    "<label><input type=radio name=m value=1"));
  if (mode == 1) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F("> Heartbeat</label>"
    "<label><input type=radio name=m value=2"));
  if (mode == 2) res.sendChunk(FPSTR(W_CHECKED));
  res.sendKV(F("> CPM Readings</label></fieldset>"
               "<label for=lat>Latitude</label>"
               "<input id=lat name=lat type=number step=any min=-90 max=90 placeholder='approximate by IP if blank' value='"),
             EGPrefs::getString("webapi", EG_K_LAT),
             F("'><label for=lon>Longitude</label>"
               "<input id=lon name=lon type=number step=any min=-180 max=180 placeholder='approximate by IP if blank' value='"));
  res.sendKV(nullptr, EGPrefs::getString("webapi", EG_K_LON),
             F("'><p style=margin-top:.5em>"));
  res.sendChunk(F(
    "<button type=button onclick=\"window.open('https://loc.espgeiger.com/','espgeiger-loc')\">Find location</button></p>"
    "<button type=submit>Save</button></form>"
    "<script>window.addEventListener('message',function(e){if(e.origin!=='https://loc.espgeiger.com')return;var d=e.data;if(!d||d.type!=='espgeiger-location')return;var la=document.getElementById('lat');var lo=document.getElementById('lon');if(la&&typeof d.lat==='number')la.value=d.lat;if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"));

  res.sendChunk(F(
    "<details style=margin-top:1em><summary>Advanced</summary>"
    "<p class=muted>Reset the device key. The next handshake registers a new station &mdash; the existing one <b>cannot be retrieved</b>.</p>"
    "<form method=POST action=/webapikeyreset onsubmit=\"return confirm('Reset the ESPGeiger Network key? This orphans the existing station.')\">"
    "<button type=submit class=danger>Reset key</button></form>"));
  if (mode > 0 && webapi.getStationId() != 0) {
    res.sendChunk(F(
      "<p class=muted style=margin-top:1em>Delete this station from the network. Removes all readings and history server-side and switches sharing to Off. <b>Cannot be undone.</b></p>"
      "<form method=POST action=/webapiforget onsubmit=\"return confirm('Permanently delete this station from the network? This cannot be undone.')\">"
      "<button type=submit class=danger>Forget station</button></form>"));
  }
  res.sendChunk(FPSTR(W_DETAILS_END));

  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hKeyReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  Log::console(PSTR("WebAPI: Resetting api.key"));
  Fs::remove(API_KEY_PATH);
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Key Reset"));
  res.sendChunk(F("<p>ESPGeiger Network key cleared. Device is restarting; a new station identity will be created on next connection.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

static void hForget(EGHttpRequest& req, EGHttpResponse& res, void*) {
  webapi.forget();
  EGPrefs::put("webapi", EG_K_MODE, "0");
  EGPrefs::commit();

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Station Forgotten"));
  res.sendChunk(FPSTR(FORGET_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static const EGMenuEntry WEBAPI_MENU[] PROGMEM = {
  {"/webapi", "ESPGeiger Network"},
  {"", ""}
};
const EGMenuEntry* WebAPI::menuEntries() { return WEBAPI_MENU; }

void WebAPI::registerRoutes(EGHttpServer& http) {
  static const char P_WEBAPI[] PROGMEM = "/webapi";
  http.on(FPSTR(P_WEBAPI),      EGHttpRequest::GET,  hWebAPI);
  http.on(FPSTR(P_WEBAPI),      EGHttpRequest::POST, hWebAPI);
  http.on(F("/webapikeyreset"), EGHttpRequest::POST, hKeyReset);
  http.on(F("/webapiforget"),   EGHttpRequest::POST, hForget);
}
#endif
