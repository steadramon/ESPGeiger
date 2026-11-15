/*
  HttpPoster.h - periodic HTTP upload, shared by the output modules

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
#ifndef CORIUM_HTTPPOSTER_H
#define CORIUM_HTTPPOSTER_H

#include <Arduino.h>
#include "App/AppConfig.h"
#include "Corium/Module/Module.h"
#include "Corium/Net/HttpDriver.h"
#include "Corium/Util/Attr.h"

#ifndef CORIUM_HTTPPOSTER_URL_MAX
#define CORIUM_HTTPPOSTER_URL_MAX 256
#endif
#ifndef CORIUM_HTTPPOSTER_REPLY_MAX
#define CORIUM_HTTPPOSTER_REPLY_MAX 128
#endif
namespace corium {

// a Sender that posts every _every_ms on its send slot, one request at a time
class HttpPoster : public Sender {
  public:
    bool requiresWifi() override { return true; }
    static constexpr uint16_t LOOP_MS = 500;
    void loop(unsigned long now) override;
    size_t statusJson(char* buf, size_t cap) override;
    void onPrefsLoaded() override;   // on = the group's enableKey
    void onPrefsSaved() override;
    HttpDriver request{this};
    // app's, true = no new post, replies still taken
    static bool (*hold)();

  protected:
    // false skips, body null for GET, else malloc'd and post() frees it
    virtual bool prepare(char* url, size_t cap, const char** body) = 0;
    // reply body, NUL ended, true = a good send
    virtual bool interpret(const char* reply) = 0;
    virtual const char* logTag() = 0;
    virtual const char* statusKey() { return name(); }
    virtual const char* method() const { return "GET"; }
    virtual uint16_t timeoutS() const { return 30; }
    virtual size_t replyCap() const { return 64; }   // bytes kept, REPLY_CAP max
    // else logged as an error
    virtual bool okCode(int code) const { return code == 200; }
    // a body's type, sent as Accept too
    virtual PGM_P contentType() const { return nullptr; }

    void setEnabled(bool on);
    // _every_ms from secs, secs back
    uint16_t setIntervalS(int s);
    // n from snprintf, a cut url is logged and not sent
    bool urlFits(int n, size_t cap);
    // true = v empty, not sent; logged once till the prefs change
    CORIUM_NODISCARD bool unset(const char* v, PGM_P label);

    uint32_t _ping_ms = 0;
    uint32_t _every_ms = 0;
    bool _send_enabled = false;

  private:
    static constexpr size_t URL_CAP = CORIUM_HTTPPOSTER_URL_MAX;
    static constexpr size_t REPLY_CAP = CORIUM_HTTPPOSTER_REPLY_MAX;
    bool _told = false;   // once till the prefs change
    uint8_t _fails = 0;   // in a row
    uint8_t _skip = 0;    // slots left to sit out
    void result(bool ok);
    void backoff(bool ok);
    bool slot_due(unsigned long now);
    void post();
    void send(const char* url, const char* body);
    void take_reply();
};

}  // namespace corium

#endif
