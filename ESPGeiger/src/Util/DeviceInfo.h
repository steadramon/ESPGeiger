/*
  DeviceInfo.h - Device identity and utility functions.
  Computed once at boot by ConfigManager, readable from anywhere
  without pulling in the WiFiManager dependency chain.

  Copyright (C) 2025 @steadramon
*/
#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <Arduino.h>
#include <ESPNtpClient.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
#endif

#ifndef THING_NAME
#define THING_NAME "ESPGeiger"
#endif

namespace DeviceInfo {
  void init(const char* hostName, const char* chipId,
            const char* userAgent, const char* macAddr);

  const char* hostname();
  const char* chipid();
  const char* useragent();
  const char* chipmodel();
  const char* geigermodel();
  const char* mac();

  inline unsigned long uptime() { return NTP.getUptime(); }

  char* uptimeString();
}

#endif
