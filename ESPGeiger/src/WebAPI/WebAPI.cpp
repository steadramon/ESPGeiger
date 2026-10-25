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

// test input never registers
#ifdef WEBAPI_TESTMODE_POST
#define WEBAPI_SENDS true
#else
#define WEBAPI_SENDS (!GEIGER_IS_TEST(GEIGER_TYPE))
#endif

#include <EGHttpServer.h>
#include "WebAPI.h"
#include "Features.h"
#include <EGBase64.h>   // no heap
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Net/SendSlot.h"
#include "Corium/Prefs/EGPrefs.h"
#include "App/AppKeys.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Clock.h"
#include "GRNG/sha256.h"
#include "GRNG/GRNG.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/CrashDump.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/WebPortal/WebPortal.h"
#include "MsgPack.h"
#include "Corium/System/Fs.h"
#include "Corium/System/KeyStore.h"
#include <uECC.h>
#include "Counter/CounterMeasures.h"
#include "Counter/Tube.h"
#include "Corium/Measure/EGMeasure.h"
#include "App/MeasureIds.h"
#include "App/ExternalPause.h"

WebAPI webapi;
EG_REGISTER_MODULE(webapi)

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
  bool off = _mode == 0 && !_forget && !request.busy();
  EGModuleRegistry::every(this, off ? 0 : LOOP_MS);
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

// hasher on the stack, noinline so it's gone before uECC_sign
static void __attribute__((noinline)) hash_body(const uint8_t* p, size_t n, uint8_t* out) {
  Sha256Class sha;
  sha.init();
  sha.write(p, n);
  memcpy(out, sha.result(), HASH_LENGTH);
}

#define WEBAPI_SIG_B64 (EGB64_LEN(WEBAPI_SIG_LEN) + 1)

// base64 into out, false = sign failed
static bool sign(const uint8_t* priv, const uint8_t* body, size_t len, char* out) {
  uint8_t signature[WEBAPI_SIG_LEN];
  uint8_t shahash[HASH_LENGTH];
  hash_body(body, len, shahash);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) return false;
  optimistic_yield(100 * 1000);
  encode_base64(signature, sizeof(signature), (uint8_t *)out);
  return true;
}

static const char CLOCK_OFF[] PROGMEM = "Clock off";

static bool denied(const uint8_t* b, size_t n) {
  const size_t len = sizeof(CLOCK_OFF) - 1;
  bool off = n == len && memcmp_P(b, CLOCK_OFF, len) == 0;
  if (off) {
    Clock::resync = true;
    Log::console(PSTR("WebAPI: Clock off, re-syncing NTP"));
  }
  return off;
}

WebAPI::WebAPI() : request(this) {}

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
      _ping_off = SendSlot::initial_offset(name(), pingIntervalMs);
    lastPing = SendSlot::ping_from_offset(_ping_off, now, pingIntervalMs);
  } else if (due_steps(now, lastPing, pingIntervalMs)) {
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
  take_reply();
  if (ExternalPause::active()) {
    EGModuleRegistry::due_at(this, now + LOOP_MS);
    return;
  }
  if (_forget && !request.busy()) {
    _forget = false;
    sendForget();
  }
  if (_mode == 0) {
    // forget reply still due
    if (request.busy()) EGModuleRegistry::due_at(this, request.due());
    else EGModuleRegistry::every(this, 0);
    return;
  }

  if (nextHandshake == 0) {
    // own slot, a crash boot goes at once
    bool first = _hs_off == 0xFFFFFFFFu;
    if (first)
      _hs_off = SendSlot::initial_offset(PSTR("wapi.hs"), pingIntervalMs);
    handshakeAt((uint32_t)now + (first && crash_to_send() ? 0 : _hs_off));
  } else if (reached_max(now, nextHandshake, WEBAPI_HANDSHAKE_MS)) {
    doHandshake();
  } else if (station_id != 0) {
    pingIfDue(now);
  }
  if (request.busy()) {
    EGModuleRegistry::due_at(this, request.due());   // the reply posts us sooner
    return;
  }
  EGModuleRegistry::due_at(this, wakeTarget());
}

void WebAPI::take_reply() {
  if (!WEBAPI_SENDS) return;
  uint8_t r[64];
  int code;
  size_t n;
  if (!request.take(&code, r, sizeof(r), &n)) return;
  uint8_t sent = _sent;
  _sent = SENT_NONE;
  if (sent == SENT_HS)          onHandshake(code, r, n);
  else if (sent == SENT_POST)   onPost(code, r, n);
  else if (sent == SENT_FORGET) onForget(code, r, n);
}

// NOT_OPENED / NO_MEMORY counted here, no reply comes
EGHttpDriver::Sent WebAPI::send_signed(uint8_t kind, PGM_P url, const uint8_t* body, size_t len,
                                       const char* sig) {
  EGHttpMeta m;
  m.method = PSTR("POST");
  m.url = url;
  m.flash = true;
  m.ctype = PSTR("application/msgpack");
  m.extra = PSTR("X-Auth");
  m.extra_value = sig;
  m.timeout_s = 10;
  _sent = kind;
  EGHttpDriver::Sent s = request.send(m, body, len);
  if (s == EGHttpDriver::NOT_OPENED || s == EGHttpDriver::NO_MEMORY) {
    _sent = SENT_NONE;
    note_publish(false);
  }
  return s;
}

namespace {
struct HsCrash {
  const CrashDump::Snapshot* cd = nullptr;
  uint32_t epc1 = 0, excvaddr = 0, epc2 = 0, epc3 = 0, depc = 0;
  uint8_t  exccause = 0;
  bool     sendExc   = false;
  bool     sendExtra = false;
  bool     sendStack = false;   // RTC snapshot only
  const char* in = nullptr;     // what the loop was in, esp32
};
}

// rst_info when CrashDump missed it
static void collect_crash(HsCrash& c) {
  c.in = EGModuleRegistry::crashed_in();
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
  if (c.in) mp.kv(PSTR("cm"), c.in);
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
  mp.kv(PSTR("bd"), DeviceInfo::board());
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

  char basesig[WEBAPI_SIG_B64];
  if (!sign(priv_k, buffer, bodyLen, basesig)) return;

  if (request.busy()) return;
  EGHttpDriver::Sent s = send_signed(SENT_HS, PSTR(WEBAPI_URL "/api/1/handshake"), buffer, bodyLen, basesig);
  if (s == EGHttpDriver::SENT) {
    handshakeAt(fast_millis() + WEBAPI_HANDSHAKE_MS);
    EGModuleRegistry::emit(TOPIC_SENT);
  } else if (s == EGHttpDriver::NOT_SEALED) {
    Log::console(PSTR("WebAPI: Handshake send failed"));
  } else {
    Log::console(PSTR("WebAPI: Handshake connect failed"));
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

void WebAPI::onHandshake(int code, const uint8_t* r, size_t n) {
  // every exit calls note_publish()
  if (code == 200) {
    MsgPack::Reader reader(r, n);
    uint32_t id = 0;
    if (reader.find_key(PSTR("id"))) {
      reader.read_uint(&id);
    }
    if (!reader.error) {
      if (id == 0) {
        Log::debug(PSTR("WebAPI: Handshake rejected (no ID in response)"));
        backoffHandshake();
        EGModuleRegistry::due_in(this, REPLAN_MS);
        note_publish(false);
        return;
      }
      if (station_id != id) {
        Log::console(HS_OK, id);
        station_id = id;
        uint32_t k = rd_be32(pub_k + 4);
        handshakeAt(fast_millis() +
          wall_clock_wait_ms(k, WEBAPI_HANDSHAKE_MS, 3600, Clock::epoch()));
        EGModuleRegistry::due_in(this, REPLAN_MS);
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
    Log::debug(PSTR("WebAPI: Handshake error %d - %s"), code, (PGM_P)EGHttpClient::reason(code));
    if (code == 403) denied(r, n);
    if (code == 429) {
      Log::debug(PSTR("WebAPI: Rate-limited; waiting for next scheduled interval"));
      note_publish(false);
      return;
    }
  }
  // backoff, uECC_sign is slow
  backoffHandshake();
  EGModuleRegistry::due_in(this, REPLAN_MS);
  note_publish(false);
}

static void write_env(MsgPack::Writer& mp) {
  float t = egm_f(M_T), h = egm_f(M_H), p = egm_f(M_P);
  if (!isnan(t)) mp.kv(PSTR("t"), t);
  if (!isnan(h)) mp.kv(PSTR("h"), h);
  if (!isnan(p)) mp.kv(PSTR("p"), p);
}

// float32, no soft-float printer
static void write_readings(MsgPack::Writer& mp) {
  mp.kv(PSTR("c"),  egm_f(M_CPM_ST));
  mp.kv(PSTR("u"),  egm_f(M_USV_ST));
#ifdef ESPG_HV_ADC
  EGMeasureIter hv(0xFF, false, M_HV);
  if (hv.next()) mp.kv(PSTR("hv"), hv.value().i);
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

  char basesig[WEBAPI_SIG_B64];
  if (!sign(priv_k, buffer, bodyLen, basesig)) {
    lastPing -= pingIntervalMs;  // retry next loop
    return;
  }

  if (request.busy()) return;
  EGHttpDriver::Sent s = send_signed(SENT_POST, PSTR(WEBAPI_URL "/api/1/post"), buffer, bodyLen, basesig);
  if (s == EGHttpDriver::SENT || s == EGHttpDriver::NOT_SEALED) EGModuleRegistry::emit(TOPIC_SENT);
  else Log::console(PSTR("WebAPI: Post connect failed"));
}

void WebAPI::onPost(int code, const uint8_t* r, size_t n) {
  bool ok = false;
  if (code == 200) {
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
    Log::debug(PSTR("WebAPI: Error %d - %s"), code, (PGM_P)EGHttpClient::reason(code));
    if (code == 403) {
      denied(r, n);
      station_id = 0;
      nextHandshake = 0;
      EGModuleRegistry::due_in(this, REPLAN_MS);
    } else if (code == 409) {
      Log::debug(PSTR("WebAPI: Replay rejected (check NTP clock)"));
    }
  }
  note_publish(ok);
}

void WebAPI::sendForget() {
  if (!WEBAPI_SENDS || station_id == 0 || !_have_key) {
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

  char basesig[WEBAPI_SIG_B64];
  if (!sign(priv_k, buffer, bodyLen, basesig)) {
    Log::console(PSTR("WebAPI: Forget sign failed"));
    return;
  }

  EGHttpDriver::Sent s = send_signed(SENT_FORGET, PSTR(WEBAPI_URL "/api/1/forget"), buffer, bodyLen, basesig);
  if (s == EGHttpDriver::SENT || s == EGHttpDriver::NOT_SEALED) {
    Log::console(PSTR("WebAPI: Forget station %u"), station_id);
    EGModuleRegistry::emit(TOPIC_SENT);
  } else {
    Log::console(PSTR("WebAPI: Forget connect failed"));
  }
}

void WebAPI::onForget(int code, const uint8_t* r, size_t n) {
  // 403 = gone, unless clock off
  bool off = code == 403 && denied(r, n);
  bool ok = (code == 200 || code == 403) && !off;
  if (ok) {
    Log::console(PSTR("WebAPI: Station forgotten (HTTP %d)"), code);
    station_id = 0;
    nextHandshake = 0;
    lastPing = 0;
    healthPostCounter = 0;
    EGModuleRegistry::due_in(this, REPLAN_MS);
  } else {
    Log::console(PSTR("WebAPI: Forget failed - %s"), (PGM_P)EGHttpClient::reason(code));
  }
  note_publish(ok);
}

static const char API_KEY_P[] PROGMEM = "/api.key";
static const char API_BAD_P[] PROGMEM = "/api.bad";
#define API_KEY_PATH FPSTR(API_KEY_P)
#define API_BAD_PATH FPSTR(API_BAD_P)

// file only, the key store has it
static void write_key_file(const uint8_t* priv) {
  File f = Fs::open_w(API_KEY_PATH);
  if (!f) {
    Log::console(PSTR("WebAPI: Failed to open api.key"));
    return;
  }
  char private_key[2 * WEBAPI_PRIV_LEN + 1] = "";
  bytestostr(priv, WEBAPI_PRIV_LEN, private_key);
  f.println(private_key);
  f.close();
}

// key reset; factory reset keeps the station
static void forget_key() {
  Fs::remove(API_KEY_PATH);
  KeyStore::clear();
}

void WebAPI::saveConfig() {
  Log::console(PSTR("WebAPI: Saving key ..."));
  KeyStore::write(priv_k, sizeof(priv_k));
  write_key_file(priv_k);
}

// a cut or bad file would sign as some other station
bool WebAPI::readKeyFile() {
  File f = Fs::open_r(API_KEY_PATH);
  if (!f) return false;
  char key_hex[2 * WEBAPI_PRIV_LEN + 2];   // hex, \r
  size_t n = f.readBytesUntil('\n', key_hex, sizeof(key_hex) - 1);
  key_hex[n] = '\0';
  f.close();
  if (n && key_hex[n - 1] == '\r') key_hex[n - 1] = '\0';
  if (hex_decode(priv_k, key_hex, sizeof(priv_k)) &&
      uECC_compute_public_key(priv_k, pub_k, uECC_secp192r1()) == 1) return true;
  memset(priv_k, 0, sizeof(priv_k));
  // kept to look at
  Fs::remove(API_BAD_PATH);
  Fs::rename(API_KEY_PATH, API_BAD_PATH);
  Log::console(PSTR("WebAPI: Stored key invalid, kept as api.bad"));
  return false;
}

// the file wins, the key store is its copy outside LittleFS
void WebAPI::loadConfig() {
  uint8_t kept[WEBAPI_PRIV_LEN];
  bool have_kept = KeyStore::read(kept, sizeof(kept));
  _have_key = readKeyFile();
  if (_have_key) {
    if (!have_kept || memcmp(kept, priv_k, sizeof(kept)) != 0) KeyStore::write(priv_k, sizeof(priv_k));
  } else if (have_kept && uECC_compute_public_key(kept, pub_k, uECC_secp192r1()) == 1) {
    memcpy(priv_k, kept, sizeof(priv_k));
    _have_key = true;
    write_key_file(priv_k);
    Log::console(PSTR("WebAPI: Key restored from backup"));
  }
  if (!_have_key) return;
  encode_base64(pub_k, sizeof(pub_k), (uint8_t *)pub_k_64);
  Log::console(PSTR("WebAPI: Key loaded"));
}

size_t WebAPI::status_json(char* buf, size_t cap, unsigned long now) {
  if (_mode == 0) return 0;
  return write_status_json(buf, cap, PSTR("webapi"), last_ok, last_attempt_ms, now);
}

// HTTP routes

static const char FORGET_BODY[] PROGMEM =
  "<p>A delete request has been sent to the server. Sharing is now <b>Off</b>.</p>"
  "<p class=muted>Re-enabling sharing later will register a new station; old readings will not be linked.</p>";

// false = a field refused, rest still saved; the pref rows are the rule
static bool saveSettings(EGHttpRequest& req) {
  bool ok = true;
  // arg() reuses its buffer
  const char* m = req.arg(F("m"));
  if (m && !EGPrefs::put("webapi", EG_K_MODE, m)) ok = false;
  const char* lat = req.arg(FPSTR(EG_K_LAT));
  if (lat && !EGPrefs::put("webapi", EG_K_LAT, *lat ? lat : "0")) ok = false;
  const char* lon = req.arg(FPSTR(EG_K_LON));
  if (lon && !EGPrefs::put("webapi", EG_K_LON, *lon ? lon : "0")) ok = false;
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
    "<button type=button onclick=\"window.open('" APP_LOC_URL "/','" APP_LOC_WIN "')\">Find location</button></p>"
    "<button type=submit>Save</button></form>"
    "<script>window.addEventListener('message',function(e){if(e.origin!=='" APP_LOC_URL "')return;var d=e.data;if(!d||d.type!=='" APP_LOC_MSG "')return;var la=document.getElementById('lat');var lo=document.getElementById('lon');if(la&&typeof d.lat==='number')la.value=d.lat;if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"));

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
  forget_key();
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
