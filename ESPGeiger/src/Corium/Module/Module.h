/*
  Module.h - Base class for modules

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
#ifndef CORIUM_MODULE_H
#define CORIUM_MODULE_H

#include <Arduino.h>
#include "App/AppConfig.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Measure/Measure.h"

class EGHttpServer;
namespace corium {

class Sender;

// PROGMEM table, ends with an empty path
struct MenuEntry {
  char path[12];
  char label[20];
};

// Lower runs first
#define CORIUM_PRIORITY_HARDWARE       10
#define CORIUM_PRIORITY_INFRASTRUCTURE 20
#define CORIUM_PRIORITY_DEFAULT        50
#define CORIUM_PRIORITY_LATE           90

class Module {
  public:
    virtual const char* name() = 0;   // flash or SRAM, read with _P
    virtual uint8_t priority() { return CORIUM_PRIORITY_DEFAULT; }
    virtual void preWifi() {}   // No network yet
    virtual void begin() {}      // after wifi, or offline
    // runs when every(), dueAt() or post() asks for it
    virtual void loop(unsigned long now) {}

    virtual bool requiresWifi() { return false; }
    virtual bool requiresNtp() { return false; }
    // Safe mode still runs these
    virtual bool coreModule() { return false; }
    // Keeps looping during OTA
    virtual bool runsDuringOta() { return false; }

    virtual const PrefGroup* prefsGroup() { return nullptr; }
    virtual const PrefShow* prefsShow() { return nullptr; }   // /param rows shown or hidden by another pref
    virtual void onPrefsLoaded() {}

    virtual void registerRoutes(EGHttpServer& http) { (void)http; }

    virtual const MenuEntry* menuEntries() { return nullptr; }
    virtual void onPrefsSaved() { onPrefsLoaded(); }
    virtual uint8_t displayOrder() { return 50; }  // lower first, 0 = hidden
    virtual uint16_t stallMs() { return 0; }   // a loop() over this is a stall, 0 = CORIUM_STALL_US
    virtual bool hwPresent() { return true; }

    // For the legacy import, PROGMEM rows ending with {}
    virtual LegacyRows legacyAliases() { return LegacyRows(); }
#ifdef CORIUM_PREFS_LEGACY_FILE
    virtual const char* legacyFile() { return CORIUM_PREFS_LEGACY_FILE; }
#else
    virtual const char* legacyFile() { return nullptr; }
#endif

    virtual MeasureRows measurements() { return MeasureRows(); }

    // /outputs fragment, 0 = not sending
    virtual size_t statusJson(char* buf, size_t cap) { return 0; }
    virtual Sender* sender() { return nullptr; }

    static constexpr uint8_t TOPIC_SENT = 1;   // A send went out

  private:
    friend class Registry;
    uint8_t _slot = 0xFF;   // set by the registry, 0xFF = not added

  protected:
    // Modules are statics, never deleted
    ~Module() = default;
};

// A module that sends. Keeps the last result and the counts for the Activity log
class Sender : public Module {
  public:
    Sender* sender() override { return this; }

    void notePublish(bool ok);
    void noteAttempt();
    void noteResult(bool ok);
    uint8_t takePublishOk();
    uint8_t takePublishErr();
    bool pending() const { return _publish_pending; }   // Sent, no reply yet

    // Both share a byte, only set via noteAttempt() / noteResult()
    uint8_t lastOk           : 1;
    uint8_t _publish_pending : 1;
  private:
    uint8_t _pub_ok  = 0;   // packs into the tail of the base class
    uint8_t _pub_err = 0;
  public:
    unsigned long lastAttempt = 0;   // uptime s, 0 = none yet

  protected:
    ~Sender() = default;
    static size_t writeStatusJson(char* buf, size_t cap, const char* key,
                                  bool ok, unsigned long lastAttempt);
};

}  // namespace corium

#endif
