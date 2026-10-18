/*
  wapi_fakes.h - stand-ins for what WebAPI.cpp reads besides its own state.

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

#ifndef TEST_WAPI_FAKES_H
#define TEST_WAPI_FAKES_H

#define WEBAPIOUT
#define GIT_VERSION     "abc1234"
#define RELEASE_VERSION "0.13.0"
#define BUILD_ENV       "esp8266_pulse"

#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <EGHttpServer.h>
#include "Corium/Module/EGModule.h"

struct FakeWapi {
  bool     warm, test_mode;
  float    cpm, usv;
  bool     env;
  float    t, h, p;
  uint32_t uptime, heap, lps, tick_max;
  int16_t  rssi;
  float    lat, lon;
  uint8_t  reset_reason;
  bool     exc, snap;
  uint32_t epc1, excvaddr, exccause, epc2, epc3, depc;
  uint32_t hs_off, wake;
  uint8_t  mode;       // webapi.mode
  int32_t  interval;   // last set_loop_interval
};
extern FakeWapi wapi;
extern time_t fake_now;

#define GEIGERINPUT_H
#define GEIGER_TYPE 0
#define GEIGER_IS_TEST(t) (wapi.test_mode)

#define ENVSENSOR_H
class EnvSensor {
  public:
    bool  present() const  { return wapi.env; }
    float tempC() const    { return wapi.t; }
    float humidity() const { return wapi.h; }
    float pressure() const { return wapi.p; }
};
extern EnvSensor envsensor;

#define DEVICEINFO_H
namespace DeviceInfo {
  const char* chipid();
  const char* chipmodel();
  const char* useragent();
  uint8_t  resetReason();
  bool resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                uint32_t* epc2 = nullptr, uint32_t* epc3 = nullptr, uint32_t* depc = nullptr);
  unsigned long uptime();
  uint32_t freeHeap();
  uint32_t largestFreeBlockLow();
  void     largestFreeBlockLowReset();
}

#define UTIL_WIFI_H
namespace Wifi {
  extern int16_t rssi;
}

#define WEBPORTAL_H
class WebPortal {
  public:
    static void sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title,
                             const char* inlineSub = nullptr);
    static void sendPageTail(EGHttpResponse& res);
    static void requestRestart(uint32_t delayMs = 1500);
    static void sendRestartCountdown(EGHttpResponse& res);
};

#define LEDSIGNAL_H
namespace LedSignal {
  inline void activity() {}
}

#define GRNG_H
class GRNG {
  public:
    static uint32_t stir() { return 0; }
    static void extract(uint8_t* out, size_t n) { memset(out, 0x5a, n); }
};

void optimistic_yield(uint32_t);

#endif
