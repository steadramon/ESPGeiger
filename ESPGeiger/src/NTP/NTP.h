/*
  NTP.h - NTP class

  Copyright (C) 2023 @steadramon

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
#ifndef _NTP_h
#define _NTP_h
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "timezones.h"
#include "../Util/Globals.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef SECS_PER_MIN
constexpr time_t SECS_PER_MIN  = 60UL;
constexpr time_t SECS_PER_HOUR = 3600UL;
constexpr time_t SECS_PER_DAY  = SECS_PER_HOUR * 24UL;
#endif

#ifndef NTP_TZ
#  define NTP_TZ "Etc/UTC"
#endif


class NTP_Client : public EGModule {
  public:
  static NTP_Client &getInstance()
  {
    static NTP_Client instance;
    return instance;
  }
    NTP_Client();
    const char* name() override { return "ntp"; }
    uint8_t priority() override { return EG_PRIORITY_INFRASTRUCTURE; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 60000; }
    void loop(unsigned long now) override;
    uint16_t warmup_seconds() override { return 0; }
    void begin() override { setup(); }
    void setup();
    const EGPrefGroup* prefs_group() override;
    void on_prefs_saved() override;  // reconfigures NTP client live
    void registerRoutes(EGHttpServer& http) override;
    uint8_t display_order() override { return 0; }  // managed via /network

    // Render the timezone + server form chunk-by-chunk into a chunked
    // response. Caller owns the surrounding <details>/page chrome and is
    // responsible for triggering /ntpjs (lazy, on details-open). Used by
    // WebPortal /network so all networking config lives in one place.
    void renderInlineForm(class EGHttpResponse& res);
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    const char* legacy_file() override { return "/ntp.json"; }  // LEGACY IMPORT

    const char* get_server() { return EGPrefs::getString("ntp", "server"); }
    const char* get_tz()     { return EGPrefs::getString("ntp", "tz"); }

    // Sticky: true once we ever sync (kept for boot_epoch validity).
    bool synced = false;
    unsigned long boot_epoch = 0;
    // Cached UTC offset (minutes). Refreshed in the sync callback (initial +
    // SDK auto-resync ~hourly) so the ~400us mktime(isdst=-1) cost stays off
    // the tick path. Consumed by /cs for browser log-timestamp display.
    int16_t tz_offset_min = 0;
    void refresh_tz_offset_min();

    // Fast local time via cheap gmtime + cached offset. False if !synced.
    bool localTm(struct tm* out) const {
      if (!synced) return false;
      time_t local = time(NULL) + (time_t)tz_offset_min * 60;
      gmtime_r(&local, out);
      return true;
    }

    // millis() at most-recent sync (every callback, not just first).
    // 0 until first sync. Use isFresh() for "is the time still trustworthy?"
    unsigned long last_sync_ms = 0;
    // Default 25h covers the SDK's ~hourly re-sync with headroom.
    bool isFresh(uint32_t maxAgeMs = 25UL * 3600UL * 1000UL) const {
      return last_sync_ms && (millis() - last_sync_ms) < maxAgeMs;
    }

    // Inlined ex-ESPNtpClient uptime tracker. millis() rollover correction
    // every 49.7d. Single source of truth for "seconds since boot".
    time_t getUptime() {
      unsigned long now_ms = ::millis();
      if (_uptime * 1000UL > now_ms) _rolloverMillis++;
      _uptime = now_ms / 1000UL + (unsigned long)_rolloverMillis * 4294967UL;
      return _uptime;
    }
    int64_t getMillisEpoch() {
      timeval t;
      gettimeofday(&t, nullptr);
      return (int64_t)t.tv_sec * 1000L + t.tv_usec / 1000L;
    }
  private:
    unsigned long _uptime = 0;
    uint16_t _rolloverMillis = 0;
};

extern NTP_Client ntpclient;

#endif
