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
#include "EGModule.h"
#include "AsyncHTTPRequest_Generic.hpp"

// The service supplies the URL and the verdict; the request lifecycle lives
// here, once.
class EGHttpPoster : public EGModule {
  public:
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    size_t status_json(char* buf, size_t cap, unsigned long now) override;
    AsyncHTTPRequest* request = nullptr;

  protected:
    // Return false to skip the cycle. body stays null for a GET.
    virtual bool prepare(char* url, size_t cap, const char** body) = 0;
    virtual bool interpret(const char* reply) = 0;
    virtual const char* log_tag() = 0;
    virtual const char* status_key() { return name(); }
    virtual const char* method() const { return "GET"; }
    virtual uint16_t timeout_s() const { return 30; }
    virtual size_t reply_cap() const { return 64; }
    virtual void add_headers(AsyncHTTPRequest*) {}

    void set_enabled(bool on);

    unsigned long lastPing = 0;
    uint32_t pingIntervalMs = 0;
    bool _send_enabled = false;

  private:
    static constexpr size_t URL_CAP = 320;
    static constexpr size_t REPLY_CAP = 128;
    void post();
    static void httpRequestCb(void* optParm, AsyncHTTPRequest* request, int readyState);
};

#endif
