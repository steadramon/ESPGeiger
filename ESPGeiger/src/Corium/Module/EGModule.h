/*
  EGModule.h - Base class for modules

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
#include "App/AppConfig.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Measure/EGMeasure.h"

class EGHttpServer;
class EGSender;

// PROGMEM, ends with an empty path
struct EGMenuEntry {
  char path[12];
  char label[20];
};

// lower runs first
#define EG_PRIORITY_HARDWARE       10
#define EG_PRIORITY_INFRASTRUCTURE 20
#define EG_PRIORITY_DEFAULT        50
#define EG_PRIORITY_LATE           90

class EGModule {
  public:
    virtual const char* name() = 0;   // flash or SRAM, read with _P
    virtual uint8_t priority() { return EG_PRIORITY_DEFAULT; }
    virtual void pre_wifi() {}   // no network yet
    virtual void begin() {}      // after wifi, or offline
    // runs once every() / due_at() / post() asks
    virtual void loop(unsigned long now) {}

    virtual bool requires_wifi() { return false; }
    virtual bool requires_ntp() { return false; }
    // safe mode keeps these
    virtual bool core_module() { return false; }
    // loops during OTA
    virtual bool runs_during_ota() { return false; }

    virtual const EGPrefGroup* prefs_group() { return nullptr; }
    virtual const EGPrefShow* prefs_show() { return nullptr; }   // /param rows by another pref
    virtual void on_prefs_loaded() {}

    virtual void registerRoutes(EGHttpServer& http) { (void)http; }

    virtual const EGMenuEntry* menuEntries() { return nullptr; }
    virtual void on_prefs_saved() { on_prefs_loaded(); }
    virtual uint8_t display_order() { return 50; }  // lower first, 0 = hidden
    virtual bool hw_present() { return true; }

    // LEGACY IMPORT (remove in v1.1.0) - PROGMEM table ending with {}
    virtual EGLegacyRows legacy_aliases() { return EGLegacyRows(); }
#ifdef EGPREFS_LEGACY_FILE
    virtual const char* legacy_file() { return EGPREFS_LEGACY_FILE; }
#else
    virtual const char* legacy_file() { return nullptr; }   // none
#endif

    virtual EGMeasureRows measurements() { return EGMeasureRows(); }

    // /outputs fragment, 0 = not sending
    virtual size_t status_json(char* buf, size_t cap, unsigned long now) { return 0; }
    virtual EGSender* sender() { return nullptr; }   // nullptr = not a sender

    static constexpr uint8_t TOPIC_SENT = 1;   // a send went out

  private:
    friend class EGModuleRegistry;
    uint8_t _slot = 0xFF;   // registry's, 0xFF = not added

  protected:
    // statics, never deleted
    ~EGModule() = default;
};

// a module that sends: last result, counts for the Activity log
class EGSender : public EGModule {
  public:
    EGSender* sender() override { return this; }

    void note_publish(bool ok);
    void note_attempt();
    void note_result(bool ok);
    uint8_t take_publish_ok();
    uint8_t take_publish_err();

    // share a byte, set via note_attempt() / note_result() only
    uint8_t last_ok          : 1;
    uint8_t _publish_pending : 1;
  private:
    uint8_t _pub_ok  = 0;   // in the base's tail
    uint8_t _pub_err = 0;
  public:
    unsigned long last_attempt_ms = 0;

  protected:
    ~EGSender() = default;
    static size_t write_status_json(char* buf, size_t cap, const char* key,
                                    bool ok, unsigned long last_attempt_ms,
                                    unsigned long now);
};

#endif
