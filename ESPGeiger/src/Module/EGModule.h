/*
  EGModule.h - Base class for ESPGeiger modules

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
#ifndef EGMODULE_H
#define EGMODULE_H

#include <Arduino.h>
#include "../Util/FastMillis.h"

// Cross-task field qualifier: volatile on ESP32 (callbacks run on the AsyncTCP
// task), empty on ESP8266 (callbacks run in the main thread).
#ifdef ESP32
#define EG_XTASK_VOLATILE volatile
#else
#define EG_XTASK_VOLATILE
#endif

struct EGPrefGroup;
struct EGLegacyAlias;  // LEGACY IMPORT (remove after v1.0.0)
class EGHttpServer;    // forward decl for registerRoutes override

// Menu entry exposed on the WebPortal home page. Modules return a
// sentinel-terminated array (final entry has path == nullptr).
struct EGMenuEntry {
  const char* path;     // e.g. "/hv"
  const char* label;    // e.g. "HV"
};

// Lower priority values run first during begin() and tick().
// Suggested values:
//   10  - Hardware initialisation (HV, power, display, LEDs, etc.)
//   20  - Infrastructure (NTP, networking services)
//   50  - Default / standard modules (outputs, submissions)
//   90  - Modules that should run last
#define EG_PRIORITY_HARDWARE       10
#define EG_PRIORITY_INFRASTRUCTURE 20
#define EG_PRIORITY_DEFAULT        50
#define EG_PRIORITY_LATE           90

class EGModule {
  public:
    virtual ~EGModule() {}
    virtual const char* name() = 0;
    virtual uint8_t priority() { return EG_PRIORITY_DEFAULT; }
    virtual void pre_wifi() {}   // Called before WiFi autoConnect. No network available.
    virtual void begin() {}      // Called after WiFi autoConnect (or offline mode).
    virtual void loop(unsigned long now) {}
    virtual bool has_loop() { return false; }

    virtual uint16_t loop_interval_ms() { return 0; }
    virtual void s_tick(unsigned long now) {}

    virtual bool has_tick() { return false; }
    virtual bool requires_wifi() { return false; }
    virtual bool requires_ntp() { return false; }
    virtual uint16_t warmup_seconds() { return 10; }

    virtual const EGPrefGroup* prefs_group() { return nullptr; }
    virtual void on_prefs_loaded() {}

    // Override to expose HTTP routes. WebPortal walks EGModuleRegistry at
    // boot and calls this on each registered module - no manual list.
    virtual void registerRoutes(EGHttpServer& http) { (void)http; }

    // Override to contribute entries to the WebPortal home-page launcher.
    // Return a sentinel-terminated array (final entry has path == nullptr).
    virtual const EGMenuEntry* menuEntries() { return nullptr; }
    virtual void on_prefs_saved() { on_prefs_loaded(); }
    virtual uint8_t display_order() { return 50; }  // /egprefs render order - lower first, 0 = hidden
    virtual bool hw_present() { return true; }

    // LEGACY IMPORT (remove after v1.0.0) - sentinel-terminated {nullptr,nullptr}
    virtual const EGLegacyAlias* legacy_aliases() { return nullptr; }
    // Source JSON file for legacy_aliases. Default = /geigerconfig.json.
    virtual const char* legacy_file() { return "/geigerconfig.json"; }

    // Optional: write a "name":{...} fragment to buf for /outputs. Return
    // bytes written, 0 = not emitting (not configured / disabled).
    // Caller handles outer braces and comma separation.
    virtual size_t status_json(char* buf, size_t cap, unsigned long now) { return 0; }

    // Two 1-bit bitfields share one byte; full uint8_t for _pub_ok keeps 0-255 range.
    uint8_t last_ok          : 1;
    uint8_t _publish_pending : 1;
    unsigned long last_attempt_ms = 0;

#ifdef ESP32
    static portMUX_TYPE& _pub_mux() {
      static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
      return m;
    }
#define EGMOD_PUB_LOCK()   portENTER_CRITICAL(&EGModule::_pub_mux())
#define EGMOD_PUB_UNLOCK() portEXIT_CRITICAL(&EGModule::_pub_mux())
#else
#define EGMOD_PUB_LOCK()
#define EGMOD_PUB_UNLOCK()
#endif

    // Publish accounting; bodies in EGModule.cpp.
    void note_publish(bool ok);
    void note_attempt();
    void note_result(bool ok);
    uint8_t take_publish_ok();
    uint8_t take_publish_err();

  private:
    uint8_t _pub_ok  = 0;
    uint8_t _pub_err = 0;

  protected:
    // Standard ok/age shape used by every sender module. Defined in EGModule.cpp.
    static size_t write_status_json(char* buf, size_t cap, const char* key,
                                    bool ok, unsigned long last_attempt_ms,
                                    unsigned long now);
};

#endif
