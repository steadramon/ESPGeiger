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

// test input never registers
#ifdef WEBAPI_TESTMODE_POST
#define WEBAPI_SENDS true
#else
#define WEBAPI_SENDS (!GEIGER_IS_TEST(GEIGER_TYPE))
#endif

#include <EGHttpServer.h>
#include "WebAPI.h"
#include "WebAPIKey.h"
#include "Features.h"
#include <EGBase64.h>   // no heap
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Net/SendSlot.h"
#include "Corium/Prefs/Prefs.h"
#include "App/AppKeys.h"
#include "App/Health.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Clock.h"
#include "GRNG/sha256.h"
#include "GRNG/GRNG.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/CrashDump.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Platform/Chip.h"
#include "MsgPack.h"
#include "Corium/System/Fs.h"
#include "Corium/System/KeyStore.h"
#include <uECC.h>
#include "Counter/CounterMeasures.h"
#include "Counter/Tube.h"
#include "Corium/Measure/Measure.h"
#include "App/MeasureIds.h"
#include "Counter/Counter.h"
#include "App/ExternalPause.h"
#ifdef ESPGEIGER_ATTEST
#include "Corium/System/HwKey.h"
#define WEBAPI_REPLY_MAX 112   // + the attest nonce, hex
#else
#define WEBAPI_REPLY_MAX 64
#endif

WebAPI webapi;
CORIUM_REGISTER_MODULE(webapi)

CORIUM_PSTR(WA_L_MD,  "Sharing mode");
CORIUM_PSTR(WA_O_MD,  "Off|Heartbeat|CPM readings");
CORIUM_PSTR(WA_L_LAT, "Latitude");
CORIUM_PSTR(WA_H_IP,  "0 = from IP");
CORIUM_PSTR(WA_L_LON, "Longitude");

static const corium::Pref WEBAPI_PREF_ITEMS[] PROGMEM = {
  {EG_K_MODE, WA_L_MD,  nullptr,  "2", WA_O_MD, 0,    2,   0, corium::P_ENUM,  0},
  {EG_K_LAT,  WA_L_LAT, WA_H_IP,  "0", nullptr, -90,  90,  0, corium::P_FLOAT, 0},
  {EG_K_LON,  WA_L_LON, WA_H_IP,  "0", nullptr, -180, 180, 0, corium::P_FLOAT, 0},
};

static const corium::PrefGroup WEBAPI_PREF_GROUP PROGMEM = {
  "webapi", "ESPGeiger Stations", 3,
  WEBAPI_PREF_ITEMS,
  sizeof(WEBAPI_PREF_ITEMS) / sizeof(WEBAPI_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_MODE, 0,
};

const corium::PrefGroup* WebAPI::prefsGroup() { return &WEBAPI_PREF_GROUP; }

void WebAPI::onPrefsLoaded() {
  if (_gone) {   // station was gone, a save tries again
    _gone = false;
    _hs_at = 0;
  }
  _mode = (uint8_t)corium::Prefs::getUInt("webapi", EG_K_MODE);
  bool off = _mode == WA_OFF && !_forget && !request.busy();
  corium::Registry::every(this, off ? 0 : LOOP_MS);
}

static int webapi_rng(uint8_t *dest, unsigned size) {
  GRNG::extract(dest, size);
  return 1;
}

static void hex_str(const uint8_t* in, unsigned int len, char* out) {
  hexEncode(out, in, len);
  out[len*2] = '\0';
}

static const char HS_OK[] PROGMEM = "WebAPI: Handshake OK - station ID %u";

// Hasher on the stack, noinline so it's gone before uECC_sign
static void __attribute__((noinline)) hash_body(const uint8_t* p, size_t n, uint8_t* out) {
  Sha256Class sha;
  sha.init();
  sha.write(p, n);
  memcpy(out, sha.result(), HASH_LENGTH);
}

// X-Auth: secp192r1 over the body's sha256, base64
static bool sign(const uint8_t* priv, const uint8_t* body, size_t len, char* out) {
  uint8_t signature[WEBAPI_SIG_LEN];
  uint8_t hash[HASH_LENGTH];
  hash_body(body, len, hash);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv, hash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) return false;
  optimistic_yield(100 * 1000);
  encode_base64(signature, sizeof(signature), (uint8_t *)out);
  return true;
}

static const char CLOCK_OFF[] PROGMEM = "Clock off";

static bool denied(const uint8_t* b, size_t n) {
  const size_t len = sizeof(CLOCK_OFF) - 1;
  bool off = n == len && memcmp_P(b, CLOCK_OFF, len) == 0;
  if (off) {
    corium::Clock::requestResync();
    corium::Log::console(PSTR("WebAPI: Clock off, re-syncing NTP"));
  }
  return off;
}

WebAPI::WebAPI() : request(this) {}

void WebAPI::begin() {
  uECC_set_rng(&webapi_rng);
  GRNG::stir();
  loadConfig();
  corium::Log::debug(PSTR("WebAPI: Ready - endpoint=%s, key=%s"),
    PSTR(WEBAPI_URL), _have_key ? PSTR("loaded") : PSTR("none (will generate)"));
}

void WebAPI::pingIfDue(unsigned long now) {
  if (_ping_ms == 0) {
    if (_ping_off == SLOT_UNSET)
      _ping_off = corium::SendSlot::initialOffset(name(), PING_MS);
    _ping_ms = corium::SendSlot::pingFromOffset(_ping_off, now, PING_MS);
  } else if (corium::dueSteps(now, _ping_ms, PING_MS)) {
    if (_hold) {
      _hold--;
      return;
    }
    const bool health = (_health_n == 0);
    if (_mode == WA_CPM || health) {
      postMeasurement(_mode != WA_CPM);
    } else {
      if (++_health_n >= WEBAPI_HEALTH_EVERY) _health_n = 0;
    }
  }
}

unsigned long WebAPI::wakeTarget() {
  unsigned long target = _hs_at;
  if (_station != 0 && _ping_ms != 0) {
    unsigned long ping_target = _ping_ms + PING_MS;
    if ((int32_t)(ping_target - target) < 0) target = ping_target;
  }
  return target;
}

static bool crash_to_send() {
  return corium::CrashDump::reset().crash;
}

// A forget is owed until it's sent. After a restart the station id has to come from a handshake first.
// Returns true when off
bool WebAPI::offStep() {
  if (!WEBAPI_SENDS || !_have_key) _forget = false;   // never had a station
  uint32_t now = corium::fastMillis();
  if (_forget && _station && corium::Clock::synced && !request.busy() &&
      corium::reachedMax(now, _forget_at, WEBAPI_BACKOFF_MAX)) {
    _forget_at = now + backoff();   // the reply clears it
    sendForget();
  }
  if ((_mode != WA_OFF && !_gone) || (_forget && !_station)) return false;
  // still waiting on the forget reply, or on the clock
  if (request.busy()) corium::Registry::dueAt(this, request.due());
  else corium::Registry::every(this, _forget ? LOOP_MS : 0);
  return true;
}

void WebAPI::loop(unsigned long now) {
  take_reply();
#ifdef ESPGEIGER_ATTEST
  const bool sign = takeSign();
#endif
  if (offStep()) return;   // A forget isn't a reading, so a pause doesn't hold it
  if (ExternalPause::active()) {
    corium::Registry::dueAt(this, now + LOOP_MS);
    return;
  }

#ifdef ESPGEIGER_ATTEST
  attestIfDue(sign);
#endif
  if (_hs_at == 0) {
    // Own send slot, but a crash boot handshakes straight away
    bool first = _hs_off == SLOT_UNSET;
    if (first)
      _hs_off = corium::SendSlot::initialOffset(PSTR("wapi.hs"), PING_MS);
    handshakeAt((uint32_t)now + (first && crash_to_send() ? 0 : _hs_off));
  } else if (corium::reachedMax(now, _hs_at, WEBAPI_HANDSHAKE_MS)) {
    doHandshake();
  } else if (_station != 0) {
    pingIfDue(now);
  }
  if (request.busy()) {
    corium::Registry::dueAt(this, request.due());   // the reply posts the module sooner
    return;
  }
  corium::Registry::dueAt(this, wakeTarget());
}

// a finished request: handshake, post or attest reply
void WebAPI::take_reply() {
  if (!WEBAPI_SENDS) return;
#ifdef ESPGEIGER_ATTEST
  if (_sent == SENT_ATT_BLOB) {
    takeBlob();
    return;
  }
#endif
  uint8_t r[WEBAPI_REPLY_MAX];
  int code;
  size_t n;
  if (!request.take(&code, r, sizeof(r), &n)) return;
  uint8_t sent = _sent;
  _sent = SENT_NONE;
  if (sent == SENT_HS)          onHandshake(code, r, n);
  else if (sent == SENT_POST)   onPost(code, r, n);
  else if (sent == SENT_FORGET) onForget(code, r, n);
#ifdef ESPGEIGER_ATTEST
  else if (sent == SENT_ATT_CH) onChallenge(code, r, n);
  else if (sent == SENT_ATT)    onAttest(code);
#endif
}

// Anything but SENT / NOT_SEALED gets no reply, so it's counted here
corium::HttpDriver::Sent WebAPI::send_signed(uint8_t kind, PGM_P url, const uint8_t* body, size_t len,
                                       const char* sig) {
  corium::HttpMeta m;
  m.method = PSTR("POST");
  m.url = url;
  m.flash = true;
  m.ctype = PSTR("application/msgpack");
  m.extra = PSTR("X-Auth");
  m.extraValue = sig;
  m.timeoutS = 10;
  _sent = kind;
  corium::HttpDriver::Sent s = request.send(m, body, len);
  if (s != corium::HttpDriver::SENT && s != corium::HttpDriver::NOT_SEALED) {
    _sent = SENT_NONE;
    notePublish(false);
  }
  return s;
}

namespace {
struct HsCrash {
  const corium::CrashDump::Snapshot* cd = nullptr;
  uint32_t epc1 = 0, excvaddr = 0, epc2 = 0, epc3 = 0, depc = 0;
  uint8_t  exccause = 0;
  bool     sendExc   = false;
  bool     sendExtra = false;
  bool     sendStack = false;   // RTC snapshot only
  const char* in = nullptr;     // what the loop was in, esp32
  const corium::CrashDump::Reset* r = nullptr;   // crash boots
};
}

// Falls back to rst_info when CrashDump missed it
static void collect_crash(HsCrash& c) {
  c.in = corium::Registry::crashedIn();
  c.cd = corium::CrashDump::lastCrash();
  if (corium::CrashDump::reset().crash) c.r = &corium::CrashDump::reset();
  if (c.cd) {
    c.epc1     = c.cd->epc1;
    c.excvaddr = c.cd->excvaddr;
    c.exccause = (uint8_t)c.cd->exccause;
    c.epc2     = c.cd->epc2;
    c.epc3     = c.cd->epc3;
    c.depc     = c.cd->depc;
    c.sendExc   = true;
    c.sendExtra = c.epc2 || c.epc3 || c.depc;
    c.sendStack = (c.cd->wordCount > 0) || c.cd->sp;
  } else if (corium::DeviceInfo::resetExc(&c.epc1, &c.excvaddr, &c.exccause,
                                  &c.epc2, &c.epc3, &c.depc)) {
    c.sendExc   = true;
    c.sendExtra = c.epc2 || c.epc3 || c.depc;
  }
  // No dump, send zeros. The server files a crash on ec/pc/va and rk says which kind
  if (c.r) c.sendExc = true;
}

static void write_crash(MsgPack::Writer& mp, const HsCrash& c) {
  uint16_t stack_len = (c.cd && c.cd->wordCount > 0) ? (c.cd->wordCount * 4) : 0;
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
    mp.kv(PSTR("st"), (const uint8_t*)c.cd->stack, stack_len);
  }
  if (c.in) mp.kv(PSTR("cm"), c.in);
  if (c.r) {
    mp.kv(PSTR("rk"), (uint32_t)c.r->kind);
    if (c.r->kind == corium::CrashDump::K_OOM) mp.kv(PSTR("fa"), c.r->pc);
  }
}

static float round_2dp(float v) {
  return (int32_t)(v * 100.0f + (v >= 0 ? 0.5f : -0.5f)) * 0.01f;
}

static void write_handshake(MsgPack::Writer& mp, const char* pub64, uint8_t mode,
                            const HsCrash& c) {
  char ver[48];
  if (GIT_VERSION[0] != '\0') {
    snprintf_P(ver, sizeof(ver), PSTR("%s/%s"), RELEASE_VERSION, GIT_VERSION);
  } else {
    strlcpy(ver, RELEASE_VERSION, sizeof(ver));
  }

  // 2dp (~1km) for privacy, 0,0 = use IP
  float lat = corium::Prefs::getFloat("webapi", EG_K_LAT);
  float lon = corium::Prefs::getFloat("webapi", EG_K_LON);
  bool coords = (lat != 0.0f || lon != 0.0f);

  mp.map_begin();
  mp.kv_u64(PSTR("n"), (uint64_t)corium::Clock::now());
  mp.kv(PSTR("pk"), pub64);
  mp.kv(PSTR("ci"), corium::DeviceInfo::chipId());
  mp.kv(PSTR("v"),  ver);
  mp.kv(PSTR("bd"), corium::DeviceInfo::board());
  if (const char* b = corium::DeviceInfo::boardName()) mp.kv(PSTR("bn"), b);
  mp.kv(PSTR("gm"), BUILD_ENV);
  mp.kv(PSTR("gc"), Tube::model());
  mp.kv(PSTR("td"), (uint32_t)Tube::detection());
  mp.kv(PSTR("fl"), (uint32_t)Features::flags());
  mp.kv(PSTR("rr"), (uint32_t)corium::DeviceInfo::resetReason());
  mp.kv(PSTR("m"),  (uint32_t)mode);
  if (coords) {
    mp.kv(PSTR("la"), round_2dp(lat));
    mp.kv(PSTR("lo"), round_2dp(lon));
  }
  write_crash(mp, c);
  mp.map_end();
}

static const char WA_TESTMODE[] PROGMEM = "WebAPI: Testmode";

void WebAPI::doHandshake() {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    corium::Log::console(WA_TESTMODE);
    handshakeAt(corium::fastMillis() + WEBAPI_HANDSHAKE_MS);
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  // Back off before anything can fail, else it spins
  backoffHandshake();

  GRNG::stir();

  if (!_have_key) {
    corium::Log::console(PSTR("WebAPI: Generating keys"));
    GRNG::stir();
    if (uECC_make_key(_pub, _priv, uECC_secp192r1()) != 1) return;   // tries again after the backoff
    optimistic_yield(100 * 1000);
    if (!saveConfig()) return;   // don't get a station for a key a reboot would lose
    _have_key = true;
    encode_base64(_pub, sizeof(_pub), (uint8_t *)_pub64);
  }

  corium::Log::debug(PSTR("WebAPI: Handshake -> " WEBAPI_URL "/api/1/handshake"));

  uint8_t buf[512];   // ~200B, +180B after a crash

  // Crash details go on the first handshake only
  HsCrash crash;
  if (!_exc_sent) collect_crash(crash);

  MsgPack::Writer mp(buf, sizeof(buf));
  write_handshake(mp, _pub64, _mode, crash);
  if (mp.overflow) {
    corium::Log::console(PSTR("WebAPI: Handshake encode overflow"));
    return;
  }
  size_t len = mp.length();

  char sig[WEBAPI_SIG_B64];
  if (!sign(_priv, buf, len, sig)) return;

  if (request.busy()) return;
  corium::HttpDriver::Sent s = send_signed(SENT_HS, PSTR(WEBAPI_URL "/api/1/handshake"), buf, len, sig);
  if (s == corium::HttpDriver::SENT) {
    handshakeAt(corium::fastMillis() + WEBAPI_HANDSHAKE_MS);
    corium::Registry::emit(TOPIC_SENT);
  } else if (s == corium::HttpDriver::NOT_SEALED) {
    corium::Log::console(PSTR("WebAPI: Handshake send failed"));
  } else {
    corium::Log::console(PSTR("WebAPI: Handshake connect failed"));
  }
}

void WebAPI::handshakeAt(uint32_t at) {
  _hs_at = at ? at : 1;
}

void WebAPI::backoffHandshake() {
  handshakeAt(corium::fastMillis() + backoff());
}

// Doubles each time up to the max
uint32_t WebAPI::backoff() {
  uint32_t wait = _hs_backoff_ms;
  uint32_t next = wait * 2;
  _hs_backoff_ms = next > WEBAPI_BACKOFF_MAX ? WEBAPI_BACKOFF_MAX : next;
  return wait;
}

static uint32_t wall_clock_wait_ms(uint32_t k, uint32_t interval_ms, uint32_t period_s) {
  return corium::SendSlot::untilPhase(k % interval_ms, corium::Clock::phase(period_s) * 1000UL, interval_ms);
}

void WebAPI::onHandshake(int code, const uint8_t* r, size_t n) {
  // every exit calls notePublish()
  if (code == 200) {
    MsgPack::Reader reader(r, n);
    uint32_t id = 0;
    if (reader.find_key(PSTR("id"))) {
      reader.read_uint(&id);
    }
    if (reader.error) {
      corium::Log::debug(PSTR("WebAPI: Handshake parse error"));
    } else if (id == 0) {
      corium::Log::debug(PSTR("WebAPI: Handshake rejected (no ID in response)"));
    } else {
      if (_station != id) {
        corium::Log::console(HS_OK, id);
        _station = id;
#ifdef ESPGEIGER_ATTEST
        _att = ATT_WAIT;   // a new or regained station attests again
#endif
        uint32_t k = rdBe32(_pub + 4);
        handshakeAt(corium::fastMillis() +
          wall_clock_wait_ms(k, WEBAPI_HANDSHAKE_MS, WEBAPI_HANDSHAKE_MS / 1000));
        corium::Registry::dueIn(this, REPLAN_MS);
      } else {
        corium::Log::debug(HS_OK, id);
      }
      _hs_backoff_ms = WEBAPI_BACKOFF_MS;
      _exc_sent = true;
#ifdef ESPGEIGER_ATTEST
      // a burned chip with no blob asks for it back, the rest never ask
      if (_att == ATT_WAIT && corium::HwKey::present()) _att = ATT_DUE;
      else if (_att == ATT_WAIT && corium::HwKey::burned()) _att = ATT_FETCH;
#endif
      notePublish(true);
      return;
    }
  } else if (replyError(code, r, n, true)) {
    notePublish(false);
    return;
  }
  // Back off, uECC_sign is slow
  backoffHandshake();
  corium::Registry::dueIn(this, REPLAN_MS);
  notePublish(false);
}

// true = no backoff
bool WebAPI::replyError(int code, const uint8_t* r, size_t n, bool hs) {
  corium::HttpDriver::logError(PSTR("WebAPI"), code);
  if (code == 410 || (hs && code == 404)) {   // a post's 404 isn't gone
    _gone = true;
    corium::Log::console(PSTR("WebAPI: Off till a save or restart"));
    return true;
  }
  if (code == 429) {
    if (!hs) _hold = WEBAPI_429_HOLD;
    return true;
  }
  if (code == 403) {
    denied(r, n);
    if (!hs) {   // drop the station and handshake again
      _station = 0;
      _hs_at = 0;
      corium::Registry::dueIn(this, REPLAN_MS);
    }
  } else if (code == 409) {
    corium::Log::debug(PSTR("WebAPI: Replay rejected (check NTP clock)"));
  }
  return false;
}

#ifdef ESPGEIGER_ATTEST
// official hardware, once a boot: the chip signs a challenge with the station key
static const char ATTEST_TAG[] PROGMEM = "espgeiger-attest-v1";

// nonce nullptr = ask for one; a lost reply asks again after the next handshake
void WebAPI::sendAttest(const uint8_t* nonce) {
  const bool fetch = _att == ATT_FETCH;
  _att = ATT_WAIT;
  uint8_t buf[448], mac[ATT_MAC], hw_sig[corium::HwKey::SIG];
  if (!corium::DeviceInfo::efuseMac(mac)) return;
  MsgPack::Writer mp(buf, sizeof(buf));
  mp.map_begin();
  mp.kv_u64(PSTR("n"), (uint64_t)corium::Clock::now());
  mp.kv(PSTR("pk"), _pub64);
  mp.kv(PSTR("hw"), mac, ATT_MAC);
  if (nonce) {
    Sha256Class sha;
    sha.init();
    sha.write((const uint8_t*)ATTEST_TAG, sizeof(ATTEST_TAG) - 1);
    sha.write(mac, ATT_MAC);
    sha.write(nonce, ATT_NONCE);
    sha.write(_pub, sizeof(_pub));
    if (!corium::HwKey::sign(sha.result(), hw_sig)) {
      corium::Log::console(PSTR("WebAPI: Attest sign failed"));
      _att = ATT_DONE;
      return;
    }
    mp.kv(PSTR("an"), nonce, ATT_NONCE);
    mp.kv(PSTR("as"), hw_sig, sizeof(hw_sig));
  }
  mp.map_end();
  char sig[WEBAPI_SIG_B64];
  if (mp.overflow || !sign(_priv, buf, mp.length(), sig)) return;
  uint8_t kind = SENT_ATT_CH;
  PGM_P url = PSTR(WEBAPI_URL "/api/1/attest/challenge");
  if (nonce) {
    kind = SENT_ATT;
    url = PSTR(WEBAPI_URL "/api/1/attest");
  } else if (fetch) {
    kind = SENT_ATT_BLOB;
    url = PSTR(WEBAPI_URL "/api/1/attest/blob");
  }
  send_signed(kind, url, buf, mp.length(), sig);
}
#endif

static void write_env(MsgPack::Writer& mp) {
  float t = corium::m_f(M_T), h = corium::m_f(M_H), p = corium::m_f(M_P);
  if (!isnan(t)) mp.kv(PSTR("t"), t);
  if (!isnan(h)) mp.kv(PSTR("h"), h);
  if (!isnan(p)) mp.kv(PSTR("p"), p);
}

// Sent as float32, so no soft-float printing
static void write_readings(MsgPack::Writer& mp) {
  mp.kv(PSTR("c"),  gcounter.get_cpmf_stable());
  mp.kv(PSTR("u"),  gcounter.get_usv_stable());
  if (health().live() & H_TUBE_MASK) mp.kv(PSTR("hb"), (uint32_t)(health().live() & H_TUBE_MASK));   // App/Health.h bits, nc was bit 0; h is humidity
#ifdef ESPG_HV_ADC
  int32_t hv;
  if (m_hv_volts(&hv)) mp.kv(M_HV, hv);
#endif
  write_env(mp);
}

static void write_health(MsgPack::Writer& mp) {
  mp.kv(PSTR("ut"), (uint32_t)corium::DeviceInfo::uptime());
  mp.kv(PSTR("mh"), (uint32_t)corium::DeviceInfo::freeHeap());
  mp.kv(PSTR("l"),  (uint32_t)corium::TickProfile::lps);
  mp.kv(PSTR("tk"), (uint32_t)corium::TickProfile::tickMaxUs);
  mp.kv(PSTR("rs"), (int32_t)corium::Wifi::rssi);
#if CORIUM_HAS_CHIPTEMP
  float ct = corium::DeviceInfo::chipTempC();
  if (!isnan(ct)) mp.kv(PSTR("ct"), ct);
#endif
#ifdef WEBAPI_TESTMODE_POST
  mp.kv(PSTR("lfb"), corium::DeviceInfo::largestFreeBlockLowTake());
#endif
}

// n = epoch seconds on every body, the server uses it for its replay check
static void write_post(MsgPack::Writer& mp, uint32_t id, bool census,
                       bool health) {
  mp.map_begin();
  mp.kv(PSTR("id"), id);
  mp.kv_u64(PSTR("n"), (uint64_t)corium::Clock::now());
  if (!census) write_readings(mp);
  if (health)  write_health(mp);
  mp.map_end();
}

void WebAPI::postMeasurement(bool census) {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    corium::Log::console(WA_TESTMODE);
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  if (!gcounter.is_warm()) {
    corium::Log::debug(PSTR("WebAPI: Skipping early post (CPM bucket not full)"));
    return;
  }

  corium::Log::debug(PSTR("WebAPI: Posting station %u%s ..."), _station, census ? PSTR(" (census)") : PSTR(""));

  uint8_t buf[192];

  MsgPack::Writer mp(buf, sizeof(buf));
  write_post(mp, _station, census, _health_n == 0);

  if (mp.overflow) {
    corium::Log::console(PSTR("WebAPI: Post encode overflow"));
    return;
  }
  size_t len = mp.length();

  char sig[WEBAPI_SIG_B64];
  if (!sign(_priv, buf, len, sig)) return;   // try again next slot

  if (request.busy()) return;
  corium::HttpDriver::Sent s = send_signed(SENT_POST, PSTR(WEBAPI_URL "/api/1/post"), buf, len, sig);
  if (s == corium::HttpDriver::SENT || s == corium::HttpDriver::NOT_SEALED) corium::Registry::emit(TOPIC_SENT);
  else corium::Log::console(PSTR("WebAPI: Post connect failed"));
}

void WebAPI::onPost(int code, const uint8_t* r, size_t n) {
  bool ok = false;
  if (code == 200) {
    MsgPack::Reader reader(r, n);
    if (reader.find_key(PSTR("ok"))) reader.read_bool(&ok);
    if (reader.error) ok = false;
    if (ok) {
      corium::Log::debug(PSTR("WebAPI: Post OK - station %u"), _station);
      if (++_health_n >= WEBAPI_HEALTH_EVERY) _health_n = 0;
    } else {
      corium::Log::debug(PSTR("WebAPI: Post response not ok"));
    }
  } else {
    (void)replyError(code, r, n, false);
  }
  notePublish(ok);
}

static const char API_FGT_P[] PROGMEM = "/api.fgt";   // exists while a forget is owed
#define API_FGT_PATH FPSTR(API_FGT_P)

void WebAPI::forget() {
  _forget = true;
  _forget_at = corium::fastMillis();
  if (!corium::Fs::writeWhole(API_FGT_PATH, "1", 1)) corium::Log::console(PSTR("WebAPI: Forget not kept over a restart"));
}

void WebAPI::sendForget() {
  if (!WEBAPI_SENDS || _station == 0 || !_have_key) {
    corium::Log::debug(PSTR("WebAPI: Forget skipped (no station)"));
    return;
  }
  if (!corium::Clock::synced) {
    corium::Log::console(PSTR("WebAPI: Forget skipped (clock not synced)"));
    return;
  }

  uint8_t buf[24];
  MsgPack::Writer mp(buf, sizeof(buf));
  mp.map(2);
  mp.kv(PSTR("id"), _station);
  mp.kv_u64(PSTR("n"), (uint64_t)corium::Clock::now());
  if (mp.overflow) return;
  size_t len = mp.length();

  char sig[WEBAPI_SIG_B64];
  if (!sign(_priv, buf, len, sig)) {
    corium::Log::console(PSTR("WebAPI: Forget sign failed"));
    return;
  }

  corium::HttpDriver::Sent s = send_signed(SENT_FORGET, PSTR(WEBAPI_URL "/api/1/forget"), buf, len, sig);
  if (s == corium::HttpDriver::SENT || s == corium::HttpDriver::NOT_SEALED) {
    corium::Log::console(PSTR("WebAPI: Forget station %u"), _station);
    corium::Registry::emit(TOPIC_SENT);
  } else {
    corium::Log::console(PSTR("WebAPI: Forget connect failed"));
  }
}

void WebAPI::onForget(int code, const uint8_t* r, size_t n) {
  // 403 means already gone, unless it's the clock that's off
  bool off = code == 403 && denied(r, n);
  bool ok = (code == 200 || code == 403) && !off;
  if (ok) {
    corium::Log::console(PSTR("WebAPI: Station forgotten (HTTP %d)"), code);
    _forget = false;
    (void)corium::Fs::remove(API_FGT_PATH);   // if it stays it's sent again next boot, the server just says gone
    _hs_backoff_ms = WEBAPI_BACKOFF_MS;
    _station = 0;
    _hs_at = 0;
    _ping_ms = 0;
    _health_n = 0;
    corium::Registry::dueIn(this, REPLAN_MS);
  } else {
    corium::Log::console(PSTR("WebAPI: Forget failed - %s"), (PGM_P)EGHttpClient::reason(code));
  }
  notePublish(ok);
}

static const char API_KEY_P[] PROGMEM = "/api.key";
static const char API_BAD_P[] PROGMEM = "/api.bad";
#define API_KEY_PATH FPSTR(API_KEY_P)
#define API_BAD_PATH FPSTR(API_BAD_P)

// Second line is the key's crc32, older builds only read the first
static constexpr size_t KEY_CRC_HEX = 8;
static bool s_key_crc;   // the file had a crc line

// File only, the key store has its own copy. writeWhole reads it back since close() reports nothing
static bool write_key_file(const uint8_t* priv) {
  char line[2 * WEBAPI_PRIV_LEN + 2 + KEY_CRC_HEX + 2 + 1];   // hex \r\n as println had it, crc \r\n, NUL
  hex_str(priv, WEBAPI_PRIV_LEN, line);
  char* p = line + 2 * WEBAPI_PRIV_LEN;
  *p++ = '\r';
  *p++ = '\n';
  uint8_t c[4];
  wrBe32(c, crc32Update(0, priv, WEBAPI_PRIV_LEN));
  hex_str(c, sizeof(c), p);
  p += KEY_CRC_HEX;
  *p++ = '\r';
  *p++ = '\n';
  if (corium::Fs::writeWhole(API_KEY_PATH, line, p - line)) return true;
  corium::Log::console(PSTR("WebAPI: Failed to write api.key"));
  return false;
}

// the copy outside LittleFS
static bool keep_key(const uint8_t* priv) {
  if (corium::KeyStore::write(priv, WEBAPI_PRIV_LEN)) return true;
  corium::Log::console(PSTR("WebAPI: Key backup not saved"));
  return false;
}

// A factory reset keeps the station. false = the old key could come back
bool forget_key() {
  // Clear the copy first, else a power cut could bring it back
  bool ok = corium::KeyStore::clear();
  if (!ok) corium::Log::console(PSTR("WebAPI: Key backup not cleared"));
  return corium::Fs::remove(API_KEY_PATH) && ok;
}

// Either copy keeps it over a reboot
bool WebAPI::saveConfig() {
  corium::Log::console(PSTR("WebAPI: Saving key ..."));
  bool kept = keep_key(_priv);
  if (write_key_file(_priv) || kept) return true;
  corium::Log::console(PSTR("WebAPI: Key not saved"));
  return false;
}

// 1 = holds, 0 = no line (an older file), -1 = damaged
static int key_crc(File& f, const uint8_t* priv) {
  char hex[KEY_CRC_HEX + 2];   // room for the \r
  size_t n = f.readBytesUntil('\n', hex, sizeof(hex) - 1);
  hex[n] = '\0';
  if (n && hex[n - 1] == '\r') hex[--n] = '\0';
  if (!n) return 0;
  uint8_t c[4];
  return hexDecode(c, hex, sizeof(c)) && rdBe32(c) == crc32Update(0, priv, WEBAPI_PRIV_LEN) ? 1 : -1;
}

// A cut short or bad file would sign as some other station, so it's checked
bool WebAPI::readKeyFile() {
  File f = corium::Fs::openR(API_KEY_PATH);
  if (!f) return false;
  char key_hex[2 * WEBAPI_PRIV_LEN + 3];   // hex, \r, room so the \n is taken too
  size_t n = f.readBytesUntil('\n', key_hex, sizeof(key_hex) - 1);
  key_hex[n] = '\0';
  if (n && key_hex[n - 1] == '\r') key_hex[n - 1] = '\0';
  int crc = hexDecode(_priv, key_hex, sizeof(_priv)) ? key_crc(f, _priv) : -1;
  f.close();
  s_key_crc = crc > 0;
  if (crc >= 0 && uECC_compute_public_key(_priv, _pub, uECC_secp192r1()) == 1) return true;
  memset(_priv, 0, sizeof(_priv));
  // keep it to look at
  (void)corium::Fs::remove(API_BAD_PATH);   // the rename result is what gets logged
  bool kept = corium::Fs::rename(API_KEY_PATH, API_BAD_PATH);
  corium::Log::console(PSTR("WebAPI: Stored key invalid, %skept as api.bad"), kept ? PSTR("") : PSTR("NOT "));
  return false;
}

// File wins, the key store is a copy outside LittleFS
void WebAPI::loadConfig() {
  uint8_t kept[WEBAPI_PRIV_LEN];
  bool have_kept = corium::KeyStore::read(kept, sizeof(kept));
  _have_key = readKeyFile();
  if (_have_key) {
    if (!have_kept || memcmp(kept, _priv, sizeof(kept)) != 0) (void)keep_key(_priv);
    if (!s_key_crc) (void)write_key_file(_priv);   // an older file gets its crc line
  } else if (have_kept && uECC_compute_public_key(kept, _pub, uECC_secp192r1()) == 1) {
    memcpy(_priv, kept, sizeof(_priv));
    _have_key = true;
    (void)write_key_file(_priv);   // file was lost but the copy still has it
    corium::Log::console(PSTR("WebAPI: Key restored from backup"));
  }
  if (!_have_key) return;
  _forget = corium::Fs::exists(API_FGT_PATH);
  encode_base64(_pub, sizeof(_pub), (uint8_t *)_pub64);
  corium::Log::console(PSTR("WebAPI: Key loaded"));
}

size_t WebAPI::statusJson(char* buf, size_t cap) {
  if (_mode == WA_OFF) return 0;
  return writeStatusJson(buf, cap, PSTR("webapi"), lastOk, lastAttempt);
}
#endif
