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
#include <Arduino.h>
#include "../Util/Globals.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGHttpPoster.h"
#include "../Prefs/EGPrefs.h"


extern Counter gcounter;

#ifndef WEBHOOK_INTERVAL_MIN
#define WEBHOOK_INTERVAL_MIN 10
#endif
#ifndef WEBHOOK_INTERVAL_MAX
#define WEBHOOK_INTERVAL_MAX 3600
#endif
#ifndef WEBHOOK_INTERVAL
#define WEBHOOK_INTERVAL 60
#endif

class Webhook : public EGHttpPoster {
  public:
    Webhook();
    const char* name() override { return "whook"; }
    void on_prefs_loaded() override;
    const EGPrefGroup* prefs_group() override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void setInterval(int interval);
    int getInterval() { return pingInterval; }
    const char* cleanHTTP(const char* url);
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* log_tag() override { return "Webhook"; }
    const char* status_key() override { return "webhook"; }
    const char* method() const override { return "POST"; }
    uint16_t timeout_s() const override { return 10; }
    size_t reply_cap() const override { return 128; }
    void add_headers(AsyncHTTPRequest* r) override;
  private:
    uint16_t pingInterval = WEBHOOK_INTERVAL;
};

extern Webhook webhook;

#endif
