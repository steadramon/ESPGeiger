/*
  up_fakes.h - stand-ins for what the uploaders read besides their own prefs.

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

// include first

#ifndef TEST_UP_FAKES_H
#define TEST_UP_FAKES_H

#define RADMONOUT
#define THINGSPEAKOUT
#define GMCOUT
#define WEBHOOKOUT

#include <Arduino.h>
#include <math.h>
#include <CircularBuffer.hpp>
#include <EGEscape.h>
#include "Corium/Module/EGModule.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Util/StringUtil.h"

struct FakeReading {
  bool  warm;
  float cpmf_stable, cpm5f, cpm15f;
  float usv, usv5, usv_stable;
};
extern FakeReading reading;
extern bool test_mode;

struct FakeMore {
  float cpmf, cps, ratio;
  int   last_cps;
  bool  tube, sat, warn, alert;
  bool  missing;
  uint32_t blip;
  bool  ser_ok;
};
extern FakeMore more;

#define GEIGERINPUT_H
class GeigerInput {
  public:
    EGMeasureRows measurements() { return EGMeasureRows(); }
};

#define COUNTER_H
#define GEIGER_TYPE 0
#define GEIGER_IS_TEST(t) (test_mode)
class Counter {
  public:
    bool  is_warm() const          { return reading.warm; }
    int   get_cpm()                { return (int)roundf(more.cpmf); }
    int   get_cpm5()               { return (int)roundf(reading.cpm5f); }
    int   get_cpm15()              { return (int)roundf(reading.cpm15f); }
    int   get_cpm_stable() const   { return (int)roundf(reading.cpmf_stable); }
    float get_cpmf_stable() const  { return reading.cpmf_stable; }
    float get_cpm5f()              { return reading.cpm5f; }
    float get_cpm15f()             { return reading.cpm15f; }
    float get_usv()                { return reading.usv; }
    float get_usv5()               { return reading.usv5; }
    float get_usv_stable() const   { return reading.usv_stable; }

    float get_cpmf()               { return more.cpmf; }
    float get_cps()                { return more.cps; }
    int   get_last_cps() const     { return more.last_cps; }
    float get_ratio() const        { return more.ratio; }
    bool  get_tube_alive()         { return more.tube; }
    bool  get_saturated()          { return more.sat; }
    bool  is_warning()             { return more.warn; }
    bool  is_alert()               { return more.alert; }
    uint64_t get_lifetime_clicks_total() const { return total_clicks; }
    float    get_cps_live()        { return 0; }
    uint16_t get_cps_n()           { return 0; }
    uint32_t get_cps_win_us()      { return 0; }
    GeigerInput* input()           { return nullptr; }
    bool  counts_missing()         { return more.missing; }
    bool  is_healthy() const       { return more.ser_ok; }
    unsigned long last_blip()      { return more.blip; }
    unsigned long total_clicks = 0;
    CircularBuffer<int, 45> cpm_history;
};
extern Counter gcounter;

struct FakeEnv {
  bool  present;
  float t, h, p;
};
extern FakeEnv env;

#define ENVSENSOR_H
class EnvSensor {
  public:
    bool  present() const  { return env.present; }
    float tempC() const    { return env.t; }
    float humidity() const { return env.h; }
    float pressure() const { return env.p; }
    uint8_t unit() const   { return 0; }
};
extern EnvSensor envsensor;

#define DEVICEINFO_H
namespace DeviceInfo {
  const char* chipid();
  const char* hostname();
  const char* friendlyName();
  unsigned long uptime();
  uint32_t freeHeap();
  uint8_t  heapFrag();
  uint32_t largestFreeBlock();
  uint32_t largestFreeBlockLow();
}

#define UTIL_WIFI_H
namespace Wifi {
  extern int16_t rssi;
}

#define EGHTTPCLIENT_H
class EGHttpClient {
  public:
    template <class N, class V> void setReqHeader(N, V) {}
};

#define EGHTTPPOSTER_H
class EGHttpPoster : public EGModule {
  public:
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    EGHttpClient* request = nullptr;

  protected:
    virtual bool prepare(char* url, size_t cap, const char** body) = 0;
    virtual bool interpret(const char* reply) = 0;
    virtual const char* log_tag() = 0;
    virtual const char* status_key() { return name(); }
    virtual const char* method() const { return "GET"; }
    virtual uint16_t timeout_s() const { return 30; }
    virtual size_t reply_cap() const { return 64; }
    virtual void add_headers(EGHttpClient*) {}

    void set_enabled(bool on);
    bool url_fits(int n, size_t cap);

    unsigned long lastPing = 0;
    uint32_t pingIntervalMs = 0;
    bool _send_enabled = false;
};

namespace FakePrefs {
  void set(const char* group, const char* key, const char* value);
  void reset();
}

#endif
