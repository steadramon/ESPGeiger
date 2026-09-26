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

#include <Arduino.h>
#include <EGHttpClient.h>
#include <EGBase64.h>
#include "../Corium/Module/EGModule.h"
#include "../Corium/Prefs/EGPrefs.h"
#include "../Corium/System/Globals.h"
#include "../GeigerInput/GeigerInput.h"

#define WEBAPI_INTERVAL      60

#ifndef WEBAPI_STATION_URL
#define WEBAPI_STATION_URL "https://stations.espgeiger.com/station/%u"
#endif
#define WEBAPI_HANDSHAKE_MS  (1UL * 60UL * 60UL * 1000UL)
#define WEBAPI_BACKOFF_MS    30000UL
#define WEBAPI_BACKOFF_MAX   (5UL * 60UL * 1000UL)
#define WEBAPI_HEALTH_EVERY  15

// secp192r1, public key and signature are 2x
#define WEBAPI_PRIV_LEN      24
#define WEBAPI_SIG_LEN       (2 * WEBAPI_PRIV_LEN)

class WebAPI : public EGModule {
  public:
    WebAPI();
    const char* name() override { return PSTR("wapi"); }
    bool requires_wifi() override { return true; }
    bool requires_ntp() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 1000; }
    void begin() override;
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    uint8_t display_order() override { return 0; }  // managed via /webapi page
    void on_prefs_loaded() override;
    void registerRoutes(EGHttpServer& http) override;
    const EGMenuEntry* menuEntries() override;
    size_t status_json(char* buf, size_t cap, unsigned long now) override;

    EGHttpClient request;

    uint32_t getStationId() const { return station_id; }
    void forget() { _forget = true; }   // sent when idle, even once off

  private:
    bool _forget = false;
    void sendForget();
    enum Sent : uint8_t { SENT_NONE, SENT_HS, SENT_POST, SENT_FORGET };
    uint8_t _sent = SENT_NONE;   // what the reply is for
    void take_reply();
    void onHandshake();
    void onPost();
    void onForget();
    void doHandshake();
    void pingIfDue(unsigned long now);
    unsigned long wakeTarget();
    void postMeasurement(bool censusOnly = false);
    void loadConfig();
    void saveConfig();

    EG_XTASK_VOLATILE uint32_t station_id = 0;
    bool _have_key = false;   // a key can start with 0
    uint8_t priv_k[WEBAPI_PRIV_LEN] = {0};
    uint8_t pub_k[2 * WEBAPI_PRIV_LEN] = {0};
    char pub_k_64[EGB64_LEN(2 * WEBAPI_PRIV_LEN) + 1] = "";  // base64, no padding
    static constexpr uint32_t pingIntervalMs = (uint32_t)WEBAPI_INTERVAL * 1000UL;
    unsigned long lastPing = 0;
    EG_XTASK_VOLATILE uint32_t nextHandshake = 0;   // 0 = not set
    EG_XTASK_VOLATILE uint32_t _hs_backoff_ms = WEBAPI_BACKOFF_MS;
    void handshakeAt(uint32_t at);
    // claimed once, reused
    uint32_t _hs_off = 0xFFFFFFFFu;
    uint32_t _ping_off = 0xFFFFFFFFu;
    uint8_t healthPostCounter = 0;
    uint8_t _mode = 2;   // 0 off, 1 heartbeat, 2 + CPM
    void backoffHandshake();
    EG_XTASK_VOLATILE bool _exc_sent = false;
};

extern WebAPI webapi;

#endif
#endif
