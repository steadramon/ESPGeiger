/*
  EGHttpPoster.h - periodic HTTP upload, shared by the output modules

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
#ifndef EGHTTPPOSTER_H
#define EGHTTPPOSTER_H

#include <Arduino.h>
#include "App/AppConfig.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Net/EGHttpDriver.h"

#ifndef EGHTTPPOSTER_URL_MAX
#define EGHTTPPOSTER_URL_MAX 256
#endif
#ifndef EGHTTPPOSTER_REPLY_MAX
#define EGHTTPPOSTER_REPLY_MAX 128
#endif

class EGHttpPoster : public EGSender {
  public:
    bool requires_wifi() override { return true; }
    static constexpr uint16_t LOOP_MS = 500;
    void loop(unsigned long now) override;
    size_t status_json(char* buf, size_t cap, unsigned long now) override;
    EGHttpDriver request{this};
    // app's, true = no new post, replies still taken
    static bool (*hold)();

  protected:
    // false skips, body null for GET, else malloc'd and post() frees it
    virtual bool prepare(char* url, size_t cap, const char** body) = 0;
    virtual bool interpret(const char* reply) = 0;
    virtual const char* log_tag() = 0;
    virtual const char* status_key() { return name(); }
    virtual const char* method() const { return "GET"; }
    virtual uint16_t timeout_s() const { return 30; }
    virtual size_t reply_cap() const { return 64; }
    // else logged as an error
    virtual bool ok_code(int code) const { return code == 200; }
    // a body's type, sent as Accept too
    virtual PGM_P content_type() const { return nullptr; }

    void set_enabled(bool on);
    // n from snprintf, a cut url is logged and not sent
    bool url_fits(int n, size_t cap);

    unsigned long lastPing = 0;
    uint32_t pingIntervalMs = 0;
    bool _send_enabled = false;

  private:
    static constexpr size_t URL_CAP = EGHTTPPOSTER_URL_MAX;
    static constexpr size_t REPLY_CAP = EGHTTPPOSTER_REPLY_MAX;
    bool slot_due(unsigned long now);
    void post();
    void send(const char* url, const char* body);
    void take_reply();
};

#endif
