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
#include "Corium/Module/Registry.h"
#include "Corium/Net/SendSlot.h"
#include "Corium/Prefs/Prefs.h"
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
#include "Corium/Measure/Measure.h"
#include "App/MeasureIds.h"
#include "App/ExternalPause.h"
#ifdef ESPGEIGER_ATTEST
#include "Corium/Console/Console.h"
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
CORIUM_PSTR(WA_H_LAT, "Station latitude  (-90..90, 0 = use IP)");
CORIUM_PSTR(WA_L_LON, "Longitude");
CORIUM_PSTR(WA_H_LON, "Station longitude (-180..180, 0 = use IP)");

static const corium::Pref WEBAPI_PREF_ITEMS[] PROGMEM = {
  {EG_K_MODE, WA_L_MD,  nullptr,  "2", WA_O_MD, 0,    2,   0, corium::P_ENUM,  0},
  {EG_K_LAT,  WA_L_LAT, WA_H_LAT, "0", nullptr, -90,  90,  0, corium::P_FLOAT, 0},
  {EG_K_LON,  WA_L_LON, WA_H_LON, "0", nullptr, -180, 180, 0, corium::P_FLOAT, 0},
};

static const corium::PrefGroup WEBAPI_PREF_GROUP PROGMEM = {
  "webapi", "ESPGeiger Network", 3,
  WEBAPI_PREF_ITEMS,
  sizeof(WEBAPI_PREF_ITEMS) / sizeof(WEBAPI_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_MODE,
};

const corium::PrefGroup* WebAPI::prefsGroup() { return &WEBAPI_PREF_GROUP; }

void WebAPI::onPrefsLoaded() {
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

// hasher on the stack, noinline so it's gone before uECC_sign
static void __attribute__((noinline)) hash_body(const uint8_t* p, size_t n, uint8_t* out) {
  Sha256Class sha;
  sha.init();
  sha.write(p, n);
  memcpy(out, sha.result(), HASH_LENGTH);
}

#define WEBAPI_SIG_B64 (EGB64_LEN(WEBAPI_SIG_LEN) + 1)

// X-Auth: secp192r1 over the body's sha256, base64 into out; false = sign failed
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
    if ((long)(ping_target - target) < 0) target = ping_target;
  }
  return target;
}

static bool crash_to_send() {
  return corium::CrashDump::hasCrash() ||
         corium::DeviceInfo::resetExc(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void WebAPI::loop(unsigned long now) {
  take_reply();
#ifdef ESPGEIGER_ATTEST
  const bool sign = takeSign();
#endif
  if (ExternalPause::active()) {
    corium::Registry::dueAt(this, now + LOOP_MS);
    return;
  }
  if (_forget && !request.busy()) {
    _forget = false;
    sendForget();
  }
  if (_mode == WA_OFF) {
    // forget reply still due
    if (request.busy()) corium::Registry::dueAt(this, request.due());
    else corium::Registry::every(this, 0);
    return;
  }

#ifdef ESPGEIGER_ATTEST
  attestIfDue(sign);
#endif
  if (_hs_at == 0) {
    // own slot, a crash boot goes at once
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
    corium::Registry::dueAt(this, request.due());   // the reply posts us sooner
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

// past SENT / NOT_SEALED no reply comes, counted here
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
};
}

// rst_info when CrashDump missed it
static void collect_crash(HsCrash& c) {
  c.in = corium::Registry::crashedIn();
  c.cd = corium::CrashDump::lastCrash();
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
  mp.kv_u64(PSTR("n"), (uint64_t)time(nullptr));
  mp.kv(PSTR("pk"), pub64);
  mp.kv(PSTR("ci"), corium::DeviceInfo::chipId());
  mp.kv(PSTR("v"),  ver);
  mp.kv(PSTR("bd"), corium::DeviceInfo::board());
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

void WebAPI::doHandshake() {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    corium::Log::console(PSTR("WebAPI: Testmode"));
    handshakeAt(corium::fastMillis() + WEBAPI_HANDSHAKE_MS);
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  // before anything can fail, else it spins
  backoffHandshake();

  GRNG::stir();

  if (!_have_key) {
    corium::Log::console(PSTR("WebAPI: Generating keys"));
    GRNG::stir();
    if (uECC_make_key(_pub, _priv, uECC_secp192r1()) != 1) return;   // again after the backoff
    optimistic_yield(100 * 1000);
    if (!saveConfig()) return;   // no station for a key a reboot loses
    _have_key = true;
    encode_base64(_pub, sizeof(_pub), (uint8_t *)_pub64);
  }

  corium::Log::debug(PSTR("WebAPI: Handshake -> " WEBAPI_URL "/api/1/handshake"));

  uint8_t buf[512];   // ~200B, +180B after a crash

  // first handshake only
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
  handshakeAt(corium::fastMillis() + _hs_backoff_ms);
  uint32_t next = _hs_backoff_ms * 2;
  _hs_backoff_ms = next > WEBAPI_BACKOFF_MAX ? WEBAPI_BACKOFF_MAX : next;
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
  } else {
    corium::Log::debug(PSTR("WebAPI: Handshake error %d - %s"), code, (PGM_P)EGHttpClient::reason(code));
    if (code == 403) denied(r, n);
    if (code == 429) {
      corium::Log::debug(PSTR("WebAPI: Rate-limited; waiting for next scheduled interval"));
      notePublish(false);
      return;
    }
  }
  // backoff, uECC_sign is slow
  backoffHandshake();
  corium::Registry::dueIn(this, REPLAN_MS);
  notePublish(false);
}

#ifdef ESPGEIGER_ATTEST
// official hardware, once a boot: challenge, then the chip signs it with the station key
static const char ATTEST_TAG[] PROGMEM = "espgeiger-attest-v1";
static constexpr size_t ATT_MAC = 6;

// a challenge is answered this pass or never, a stale nonce only fails
bool WebAPI::takeSign() {
  if (_att != ATT_SIGN) return false;
  _att = ATT_WAIT;
  return true;
}

// after the off / forget checks, before a post takes the request
void WebAPI::attestIfDue(bool sign) {
  if ((sign || _att == ATT_DUE || _att == ATT_FETCH) && _station != 0 && !request.busy())
    sendAttest(sign ? _nonce : nullptr);
}

// nonce nullptr = ask for one, or for the blob; a lost reply asks again after the next handshake
void WebAPI::sendAttest(const uint8_t* nonce) {
  const bool fetch = _att == ATT_FETCH;
  _att = ATT_WAIT;
  uint8_t buf[448], mac[ATT_MAC], hw_sig[corium::HwKey::SIG];
  if (!corium::DeviceInfo::efuseMac(mac)) return;
  MsgPack::Writer mp(buf, sizeof(buf));
  mp.map_begin();
  mp.kv_u64(PSTR("n"), (uint64_t)time(nullptr));
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

static_assert(EGHttpClient::BODY_CAP > corium::HwKey::BLOB, "blob reply needs EGHTTP_BODY_CAP");

// +1 sees a longer reply
void __attribute__((noinline)) WebAPI::takeBlob() {
  uint8_t r[corium::HwKey::BLOB + 1];
  int code;
  size_t n;
  if (!request.take(&code, r, sizeof(r), &n)) return;
  _sent = SENT_NONE;
  onBlob(code, r, n);
}

// raw blob; 403 / 404 = not for this boot
void WebAPI::onBlob(int code, const uint8_t* r, size_t n) {
  if (code == 200 && n == corium::HwKey::BLOB && corium::HwKey::put(r)) {
    corium::Log::debug(PSTR("WebAPI: Key restored"));
    _att = ATT_DUE;
  } else if (code == 200 || code == 403 || code == 404) {
    corium::Log::debug(PSTR("WebAPI: Key not restored"));
    _att = ATT_DONE;
  }
}

// 404 = not official hardware, nothing more this boot
void WebAPI::onChallenge(int code, const uint8_t* r, size_t n) {
  MsgPack::Reader reader(r, n);
  char hex[2 * ATT_NONCE + 1];
  if (code == 200 && reader.find_key(PSTR("an")) && reader.read_str(hex, sizeof(hex)) &&
      hexDecode(_nonce, hex, ATT_NONCE)) {
    _att = ATT_SIGN;
  } else if (code == 403 || code == 404) {
    _att = ATT_DONE;
  }
}

void WebAPI::onAttest(int code) {
  if (code == 200) {
    corium::Log::debug(PSTR("WebAPI: Official hardware verified"));
    _att = ATT_DONE;
  } else if (code == 403 || code == 404) {
    corium::Log::debug(PSTR("WebAPI: Official hardware not verified"));
    _att = ATT_DONE;
  }
}

// genuine <64 hex>: setup.espgeiger.com's check over serial. Its own tag, never a station attest
static const char CHECK_TAG[] PROGMEM = "espgeiger-check-v1";
static constexpr size_t SIG_LINE = 32;

static void genuine(const char* hex) {
  uint8_t nonce[ATT_NONCE], mac[ATT_MAC], sig[corium::HwKey::SIG];
  if (!hex || !hexDecode(nonce, hex, ATT_NONCE) || !corium::DeviceInfo::efuseMac(mac)) {
    corium::Log::console(PSTR("genuine <64 hex>"));
    return;
  }
  Sha256Class sha;
  sha.init();
  sha.write((const uint8_t*)CHECK_TAG, sizeof(CHECK_TAG) - 1);
  sha.write(mac, ATT_MAC);
  sha.write(nonce, ATT_NONCE);
  if (!corium::HwKey::sign(sha.result(), sig)) {
    corium::Log::console(PSTR("Genuine: no"));
    return;
  }
  char out[2 * SIG_LINE + 1];
  hexEncode(out, mac, ATT_MAC);
  out[2 * ATT_MAC] = '\0';
  corium::Log::console(PSTR("Genuine: mac %s"), out);
  for (size_t i = 0; i < corium::HwKey::SIG / SIG_LINE; i++) {
    hexEncode(out, sig + i * SIG_LINE, SIG_LINE);
    out[2 * SIG_LINE] = '\0';
    corium::Log::console(PSTR("Genuine: sig %u %s"), (unsigned)i, out);
  }
  corium::Log::console(PSTR("Genuine: end"));
}

// the reply reaches USB even while SerialOut streams
static void cmd_genuine() {
  const bool was = corium::Log::serialOn();
  corium::Log::setSerialOn(true);
  genuine(corium::Console::next());
  corium::Log::setSerialOn(was);
}
CORIUM_REGISTER_CMD(genuine, cmd_genuine)
#endif

static void write_env(MsgPack::Writer& mp) {
  float t = corium::m_f(M_T), h = corium::m_f(M_H), p = corium::m_f(M_P);
  if (!isnan(t)) mp.kv(PSTR("t"), t);
  if (!isnan(h)) mp.kv(PSTR("h"), h);
  if (!isnan(p)) mp.kv(PSTR("p"), p);
}

// float32, no soft-float printer
static void write_readings(MsgPack::Writer& mp) {
  mp.kv(PSTR("c"),  corium::m_f(M_CPM_ST));
  mp.kv(PSTR("u"),  corium::m_f(M_USV_ST));
  if (corium::m_b(M_NONE)) mp.kv(PSTR("nc"), true);
#ifdef ESPG_HV_ADC
  int32_t hv;
  if (m_hv_volts(&hv)) mp.kv(PSTR("hv"), hv);
#endif
  write_env(mp);
}

static void write_health(MsgPack::Writer& mp) {
  mp.kv(PSTR("ut"), (uint32_t)corium::DeviceInfo::uptime());
  mp.kv(PSTR("mh"), corium::DeviceInfo::freeHeap());
  mp.kv(PSTR("l"),  (uint32_t)corium::TickProfile::lps);
  mp.kv(PSTR("tk"), (uint32_t)corium::TickProfile::tickMaxUs);
  mp.kv(PSTR("rs"), (int32_t)corium::Wifi::rssi);
#ifdef ESP32
  float ct = corium::DeviceInfo::chipTempC();
  if (!isnan(ct)) mp.kv(PSTR("ct"), ct);
#endif
#ifdef WEBAPI_TESTMODE_POST
  mp.kv(PSTR("lfb"), corium::DeviceInfo::largestFreeBlockLowTake());
#endif
}

// n = epoch s on every body, the server's replay check
static void write_post(MsgPack::Writer& mp, uint32_t id, bool census,
                       bool health) {
  mp.map_begin();
  mp.kv(PSTR("id"), id);
  mp.kv_u64(PSTR("n"), (uint64_t)time(nullptr));
  if (!census) write_readings(mp);
  if (health)  write_health(mp);
  mp.map_end();
}

void WebAPI::postMeasurement(bool census) {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    corium::Log::console(PSTR("WebAPI: Testmode"));
#ifndef WEBAPI_TESTMODE_POST
    return;
#endif
  }

  if (!corium::m_b(M_WARM)) {
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
  if (!sign(_priv, buf, len, sig)) return;   // next slot

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
    corium::Log::debug(PSTR("WebAPI: Error %d - %s"), code, (PGM_P)EGHttpClient::reason(code));
    if (code == 403) {
      denied(r, n);
      _station = 0;
      _hs_at = 0;
      corium::Registry::dueIn(this, REPLAN_MS);
    } else if (code == 409) {
      corium::Log::debug(PSTR("WebAPI: Replay rejected (check NTP clock)"));
    }
  }
  notePublish(ok);
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
  mp.kv_u64(PSTR("n"), (uint64_t)time(nullptr));
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
  // 403 = gone, unless clock off
  bool off = code == 403 && denied(r, n);
  bool ok = (code == 200 || code == 403) && !off;
  if (ok) {
    corium::Log::console(PSTR("WebAPI: Station forgotten (HTTP %d)"), code);
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

// file only, the key store has it; read back, close() says nothing
static bool write_key_file(const uint8_t* priv) {
  char line[2 * WEBAPI_PRIV_LEN + 2];   // hex, \r\n as println had it
  hex_str(priv, WEBAPI_PRIV_LEN, line);
  line[2 * WEBAPI_PRIV_LEN] = '\r';
  line[2 * WEBAPI_PRIV_LEN + 1] = '\n';
  if (corium::Fs::writeWhole(API_KEY_PATH, line, sizeof(line))) return true;
  corium::Log::console(PSTR("WebAPI: Failed to write api.key"));
  return false;
}

// the copy outside LittleFS
static bool keep_key(const uint8_t* priv) {
  if (corium::KeyStore::write(priv, WEBAPI_PRIV_LEN)) return true;
  corium::Log::console(PSTR("WebAPI: Key backup not saved"));
  return false;
}

// key reset; factory reset keeps the station. false = either left, the old key comes back
static bool forget_key() {
  // copy first, else a cut brings it back
  bool ok = corium::KeyStore::clear();
  if (!ok) corium::Log::console(PSTR("WebAPI: Key backup not cleared"));
  return corium::Fs::remove(API_KEY_PATH) && ok;
}

// either copy keeps it over a reboot
bool WebAPI::saveConfig() {
  corium::Log::console(PSTR("WebAPI: Saving key ..."));
  bool kept = keep_key(_priv);
  if (write_key_file(_priv) || kept) return true;
  corium::Log::console(PSTR("WebAPI: Key not saved"));
  return false;
}

// a cut or bad file would sign as some other station
bool WebAPI::readKeyFile() {
  File f = corium::Fs::openR(API_KEY_PATH);
  if (!f) return false;
  char key_hex[2 * WEBAPI_PRIV_LEN + 2];   // hex, \r
  size_t n = f.readBytesUntil('\n', key_hex, sizeof(key_hex) - 1);
  key_hex[n] = '\0';
  f.close();
  if (n && key_hex[n - 1] == '\r') key_hex[n - 1] = '\0';
  if (hexDecode(_priv, key_hex, sizeof(_priv)) &&
      uECC_compute_public_key(_priv, _pub, uECC_secp192r1()) == 1) return true;
  memset(_priv, 0, sizeof(_priv));
  // kept to look at
  (void)corium::Fs::remove(API_BAD_PATH);   // the rename says
  bool kept = corium::Fs::rename(API_KEY_PATH, API_BAD_PATH);
  corium::Log::console(PSTR("WebAPI: Stored key invalid, %skept as api.bad"), kept ? PSTR("") : PSTR("NOT "));
  return false;
}

// the file wins, the key store is its copy outside LittleFS
void WebAPI::loadConfig() {
  uint8_t kept[WEBAPI_PRIV_LEN];
  bool have_kept = corium::KeyStore::read(kept, sizeof(kept));
  _have_key = readKeyFile();
  if (_have_key) {
    if (!have_kept || memcmp(kept, _priv, sizeof(kept)) != 0) (void)keep_key(_priv);
  } else if (have_kept && uECC_compute_public_key(kept, _pub, uECC_secp192r1()) == 1) {
    memcpy(_priv, kept, sizeof(_priv));
    _have_key = true;
    (void)write_key_file(_priv);   // the copy still has it
    corium::Log::console(PSTR("WebAPI: Key restored from backup"));
  }
  if (!_have_key) return;
  encode_base64(_pub, sizeof(_pub), (uint8_t *)_pub64);
  corium::Log::console(PSTR("WebAPI: Key loaded"));
}

size_t WebAPI::statusJson(char* buf, size_t cap, unsigned long /*now*/) {
  if (_mode == WA_OFF) return 0;
  return writeStatusJson(buf, cap, PSTR("webapi"), lastOk, lastAttempt);
}

// HTTP routes

static const char FORGET_BODY[] PROGMEM =
  "<p>A delete request has been sent to the server. Sharing is now <b>Off</b>.</p>"
  "<p class=muted>Re-enabling sharing later will register a new station; old readings will not be linked.</p>";

// false = a field refused, rest still saved; the pref rows are the rule
static bool save_settings(EGHttpRequest& req, bool& wrote) {
  bool ok = true;
  // arg() reuses its buffer
  const char* m = req.arg(F("m"));
  if (m && !corium::Prefs::put("webapi", EG_K_MODE, m)) ok = false;
  const char* lat = req.arg(FPSTR(EG_K_LAT));
  if (lat && !corium::Prefs::put("webapi", EG_K_LAT, *lat ? lat : "0")) ok = false;
  const char* lon = req.arg(FPSTR(EG_K_LON));
  if (lon && !corium::Prefs::put("webapi", EG_K_LON, *lon ? lon : "0")) ok = false;
  wrote = corium::Prefs::commit();
  return ok;
}

static void send_station(EGHttpResponse& res) {
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

static void send_sharing(EGHttpResponse& res, uint8_t mode) {
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
             corium::Prefs::getString("webapi", EG_K_LAT),
             F("'><label for=lon>Longitude</label>"
               "<input id=lon name=lon type=number step=any min=-180 max=180 placeholder='approximate by IP if blank' value='"));
  res.sendKV(nullptr, corium::Prefs::getString("webapi", EG_K_LON),
             F("'><p style=margin-top:.5em>"));
  res.sendChunk(F(
    "<button type=button onclick=\"window.open('" APP_LOC_URL "/','" APP_LOC_WIN "')\">Find location</button></p>"
    "<button type=submit>Save</button></form>"
    "<script>window.addEventListener('message',function(e){if(e.origin!=='" APP_LOC_URL "')return;var d=e.data;if(!d||d.type!=='" APP_LOC_MSG "')return;var la=document.getElementById('lat');var lo=document.getElementById('lon');if(la&&typeof d.lat==='number')la.value=d.lat;if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"));
}

static void send_advanced(EGHttpResponse& res, uint8_t mode) {
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
}

// POST saves first, both show the page
static void hWebAPI(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const bool posted = req.method() == EGHttpRequest::POST;
  bool wrote = true;
  const bool saved = posted && save_settings(req, wrote);

  corium::WebPortal::beginPage(res, F("ESPGeiger Network"));
  if (!wrote) corium::WebPortal::sendNotSaved(res);
  else if (saved) res.sendChunk(F("<div class='a ak'>Saved.</div>"));
  else if (posted) res.sendChunk(F("<div class='a ae'>A value was out of range and not saved.</div>"));
  res.sendChunk(F(
    "<p class=muted>ESPGeiger can share data with the public station "
    "network so it appears on the global map. The device identity and "
    "location are obscured before publication.</p>"));

  uint8_t mode = (uint8_t)corium::Prefs::getUInt("webapi", EG_K_MODE);
  if (mode > 0) send_station(res);

  send_sharing(res, mode);
  send_advanced(res, mode);

  corium::WebPortal::endPage(res);
}

static void hKeyReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::Log::console(PSTR("WebAPI: Resetting api.key"));
  bool gone = forget_key();
  corium::WebPortal::beginPage(res, F("Key Reset"));
  if (gone) {
    res.sendChunk(F("<p>ESPGeiger Network key cleared. Device is restarting; a new station identity will be created on next connection.</p>"));
    corium::WebPortal::sendRestartCountdown(res);
    corium::WebPortal::requestRestart();
  } else {
    corium::WebPortal::sendNotSaved(res);
  }
  corium::WebPortal::endPage(res);
}

static void hForget(EGHttpRequest& req, EGHttpResponse& res, void*) {
  webapi.forget();
  bool wrote = corium::Prefs::put("webapi", EG_K_MODE, "0") && corium::Prefs::commit();

  corium::WebPortal::beginPage(res, F("Station Forgotten"));
  if (!wrote) corium::WebPortal::sendNotSaved(res);
  res.sendChunk(FPSTR(FORGET_BODY));
  corium::WebPortal::endPage(res);
}

static const corium::MenuEntry WEBAPI_MENU[] PROGMEM = {
  {"/webapi", "ESPGeiger Network"},
  {"", ""}
};
const corium::MenuEntry* WebAPI::menuEntries() { return WEBAPI_MENU; }

static const char R_WEBAPI[] PROGMEM = "/webapi";
static const char R_KEYRESET[] PROGMEM = "/webapikeyreset";
static const char R_FORGET[]   PROGMEM = "/webapiforget";

static const corium::WebRoute ROUTES[] PROGMEM = {
  {R_WEBAPI,   hWebAPI,   EGHttpRequest::GET},
  {R_WEBAPI,   hWebAPI,   EGHttpRequest::POST},
  {R_KEYRESET, hKeyReset, EGHttpRequest::POST},
  {R_FORGET,   hForget,   EGHttpRequest::POST},
};

void WebAPI::registerRoutes(EGHttpServer& http) {
  corium::WebPortal::on(http, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}
#endif
