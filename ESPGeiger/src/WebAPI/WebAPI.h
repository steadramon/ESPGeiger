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
#include "AsyncHTTPRequest_Generic.hpp"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/Globals.h"
#include "../Counter/Counter.h"

#define WEBAPI_INTERVAL      60

#ifndef WEBAPI_STATION_URL
#define WEBAPI_STATION_URL "https://stations.espgeiger.com/station/%u"
#endif
#define WEBAPI_HANDSHAKE_MS  (1UL * 60UL * 60UL * 1000UL)  // 1 hour
// Cadence of the health snapshot piggyback on the regular post.
#define WEBAPI_HEALTH_EVERY  15

extern Counter gcounter;

class WebAPI : public EGModule {
  public:
    WebAPI();
    const char* name() override { return "wapi"; }
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

    AsyncHTTPRequest request;

    uint32_t getStationId() const { return station_id; }
    // Signed server-side delete; local key + station_id stay intact.
    void forget();

  private:
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
    static void httpHandshakeCb(void *optParm, AsyncHTTPRequest *request, int readyState);
    static void httpForgetCb(void *optParm, AsyncHTTPRequest *request, int readyState);
    void doHandshake();
    // censusOnly: respect heartbeat-mode opt-out by omitting radiation fields.
    void postMeasurement(bool censusOnly = false);
    void loadConfig();
    void saveConfig();

    uint32_t station_id = 0;
    uint8_t priv_k[24] = {0};
    uint8_t pub_k[48] = {0};
    char pub_k_64[65] = "";  // base64 of pub_k (no padding) + null
    static constexpr uint32_t pingIntervalMs = (uint32_t)WEBAPI_INTERVAL * 1000UL;
    unsigned long lastPing = 0;
    unsigned long lastHandshake = 0;
    uint8_t healthPostCounter = 0;
    // 0 = off, 1 = heartbeat only, 2 = heartbeat + CPM.
    uint8_t _mode = 2;
    // Doubles on each handshake failure, capped at 5min, reset on success.
    uint32_t _hs_backoff_ms = 30000UL;
    // Set after the first handshake that successfully reported the
    // boot-time exception details. Stops us re-sending the same crash
    // info on every subsequent hourly handshake.
    bool _exc_sent = false;
};

extern WebAPI webapi;

#endif
#endif
