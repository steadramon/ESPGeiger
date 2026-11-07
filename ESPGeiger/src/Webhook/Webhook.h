/*
  Webhook.h - Webhook class

  Copyright (C) 2025 @steadramon

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
#ifndef WEBHOOK_H
#define WEBHOOK_H
#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include "Corium/System/DeviceInfo.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Net/HttpPoster.h"
#include "Corium/Prefs/Prefs.h"

// s between POSTs
#ifndef WEBHOOK_INTERVAL_MIN
#define WEBHOOK_INTERVAL_MIN 10
#endif
#ifndef WEBHOOK_INTERVAL_MAX
#define WEBHOOK_INTERVAL_MAX 3600
#endif
#ifndef WEBHOOK_INTERVAL
#define WEBHOOK_INTERVAL 60
#endif

class Webhook : public corium::HttpPoster {
  public:
    Webhook();
    const char* name() override { return PSTR("whook"); }
    void onPrefsLoaded() override;
    const corium::PrefGroup* prefsGroup() override;
    corium::LegacyRows legacyAliases() override;  // LEGACY IMPORT (remove in v1.1.0)
    void setInterval(int interval);
    int getInterval() { return _interval_s; }
    const char* cleanHTTP(const char* url);
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* logTag() override { return PSTR("Webhook"); }
    const char* statusKey() override { return PSTR("webhook"); }
    const char* method() const override { return "POST"; }
    uint16_t timeoutS() const override { return 10; }
    size_t replyCap() const override { return 128; }
    PGM_P contentType() const override;
  private:
    uint16_t _interval_s = WEBHOOK_INTERVAL;
};

extern Webhook webhook;

#endif
