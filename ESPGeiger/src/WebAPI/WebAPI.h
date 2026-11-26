/*
  WebAPI.h - Signed-submission remote API client.

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
#ifndef WEBAPI_H
#define WEBAPI_H
#ifdef WEBAPIOUT

#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Net/HttpDriver.h"
#include <EGBase64.h>
#include "Corium/Module/Module.h"
#include "Corium/Prefs/Prefs.h"
#include "App/AppConfig.h"
#include "GeigerInput/GeigerInput.h"

// s between posts
#define WEBAPI_INTERVAL      60

#ifndef WEBAPI_STATION_URL
#define WEBAPI_STATION_URL "https://stations.espgeiger.com/station/%u"
#endif
// Handshake hourly. A failed one backs off 30 s, doubling up to 5 min
#define WEBAPI_HANDSHAKE_MS  (1UL * 60UL * 60UL * 1000UL)
#define WEBAPI_BACKOFF_MS    30000UL
#define WEBAPI_BACKOFF_MAX   (5UL * 60UL * 1000UL)
// Health fields go on one post in 15
#define WEBAPI_HEALTH_EVERY  15
// posts skipped after a 429
#define WEBAPI_429_HOLD      5

// secp192r1, public key and signature are 2x
#define WEBAPI_PRIV_LEN      24
#define WEBAPI_SIG_LEN       (2 * WEBAPI_PRIV_LEN)

#ifdef ESPGEIGER_ATTEST
static constexpr size_t ATT_NONCE = 32;
#endif

// ESPGeiger Network: signed msgpack handshake, posts and forget
class WebAPI : public corium::Sender {
  public:
    WebAPI();
    const char* name() override { return PSTR("wapi"); }
    bool requiresWifi() override { return true; }
    bool requiresNtp() override { return true; }
    static constexpr uint16_t LOOP_MS = 1000;
    static constexpr uint16_t REPLAN_MS = 100;
    void begin() override;
    void loop(unsigned long now) override;
    const corium::PrefGroup* prefsGroup() override;
    uint8_t displayOrder() override { return 0; }  // managed via /webapi page
    uint16_t stallMs() override { return 250; }   // the sign, ~180 ms on 8266
    void onPrefsLoaded() override;
    void registerRoutes(EGHttpServer& http) override;
    const corium::MenuEntry* menuEntries() override;
    size_t statusJson(char* buf, size_t cap) override;

    corium::HttpDriver request;

    uint32_t getStationId() const { return _station; }
    void forget();   // Sent when idle, even after it's turned off, and kept over a restart

  private:
    bool _forget = false;
    uint32_t _forget_at = 0;   // the next try
    void sendForget();
    bool _gone = false;   // got a 410, stays gone until a save or restart
    uint8_t _hold = 0;   // posts to skip
    enum Sent : uint8_t { SENT_NONE, SENT_HS, SENT_POST, SENT_FORGET, SENT_ATT_CH, SENT_ATT, SENT_ATT_BLOB };
    uint8_t _sent = SENT_NONE;   // what the reply is for
    corium::HttpDriver::Sent send_signed(uint8_t kind, PGM_P url, const uint8_t* body, size_t len,
                                   const char* sig);
    void take_reply();
    void onHandshake(int code, const uint8_t* r, size_t n);
    bool replyError(int code, const uint8_t* r, size_t n, bool hs);
    void onPost(int code, const uint8_t* r, size_t n);
    void onForget(int code, const uint8_t* r, size_t n);
    void doHandshake();
    bool offStep();
    void pingIfDue(unsigned long now);
    unsigned long wakeTarget();
    void postMeasurement(bool census = false);
    void loadConfig();
    bool readKeyFile();
    bool saveConfig();

    uint32_t _station = 0;
    bool _have_key = false;   // a key can start with 0
    uint8_t _priv[WEBAPI_PRIV_LEN] = {};
    uint8_t _pub[2 * WEBAPI_PRIV_LEN] = {};
    char _pub64[EGB64_LEN(2 * WEBAPI_PRIV_LEN) + 1] = {};  // base64, no padding
    static constexpr uint32_t PING_MS = (uint32_t)WEBAPI_INTERVAL * 1000UL;
    uint32_t _ping_ms = 0;
    uint32_t _hs_at = 0;   // 0 = not set
    uint32_t _hs_backoff_ms = WEBAPI_BACKOFF_MS;
    void handshakeAt(uint32_t at);
    // claimed once, reused
    static constexpr uint32_t SLOT_UNSET = 0xFFFFFFFFu;
    uint32_t _hs_off = SLOT_UNSET;
    uint32_t _ping_off = SLOT_UNSET;
    uint8_t _health_n = 0;
    enum : uint8_t { WA_OFF, WA_HEARTBEAT, WA_CPM };
    uint8_t _mode = WA_CPM;
    void backoffHandshake();
    uint32_t backoff();
    bool _exc_sent = false;
#ifdef ESPGEIGER_ATTEST
    // WAIT = after the next good handshake, FETCH = burned but the blob is gone
    enum Att : uint8_t { ATT_WAIT, ATT_DUE, ATT_SIGN, ATT_FETCH, ATT_DONE };
    uint8_t _att = ATT_WAIT;
    uint8_t _nonce[ATT_NONCE];   // the challenge, signed and sent from loop()
    bool takeSign();
    void attestIfDue(bool sign);
    void sendAttest(const uint8_t* nonce);
    void onChallenge(int code, const uint8_t* r, size_t n);
    void onAttest(int code);
    void takeBlob();
    void onBlob(int code, const uint8_t* r, size_t n);
#endif
};

extern WebAPI webapi;

#endif
#endif
