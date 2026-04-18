/*
  DeviceInfo.cpp - Device identity and utility functions.

  Copyright (C) 2025 @steadramon
*/
#include "DeviceInfo.h"
#ifdef ESP32
#include <esp_chip_info.h>
#endif

#ifndef GEIGER_MODEL
#define GEIGER_MODEL ""
#endif

// Pointers into ConfigManager's buffers - no copies, no extra RAM.
static const char* s_hostname = "";
static const char* s_chipid = "";
static const char* s_useragent = "";
static const char* s_mac = "";

void DeviceInfo::init(const char* hostName, const char* chipId,
                      const char* userAgent, const char* macAddr) {
  s_hostname = hostName;
  s_chipid = chipId;
  s_useragent = userAgent;
  s_mac = macAddr;
}

const char* DeviceInfo::hostname()    { return s_hostname; }
const char* DeviceInfo::chipid()      { return s_chipid; }
const char* DeviceInfo::useragent()   { return s_useragent; }
const char* DeviceInfo::mac()         { return s_mac; }
const char* DeviceInfo::geigermodel() { return GEIGER_MODEL; }

const char* DeviceInfo::chipmodel() {
#ifdef ESP8266
  #ifdef ESPGEIGER_HW
    return "ESPG-HW";
  #elif defined(ESPGEIGER_LT)
    #ifdef GEIGER_SDCARD
    return "ESPG-LOG";
    #else
    return "ESPG-LT";
    #endif
  #else
    return "ESP8266";
  #endif
#elif defined(ESP32)
  #ifdef ESPGEIGER_HW
    return "ESPG32-HW";
  #else
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    switch ((int)chipInfo.model) {
      case 0:  return "ESP8266";
      case (int)esp_chip_model_t::CHIP_ESP32:   return "ESP32";
      case (int)esp_chip_model_t::CHIP_ESP32S2: return "ESP32-S2";
      case (int)esp_chip_model_t::CHIP_ESP32S3: return "ESP32-S3";
      case (int)esp_chip_model_t::CHIP_ESP32C3: return "ESP32-C3";
      case 6:  return "ESP32-H4";
      case 12: return "ESP32-C2";
      case 13: return "ESP32-C6";
      case 16: return "ESP32-H2";
      default: return "ESP32";
    }
  #endif
#else
  return "UNKNOWN";
#endif
}

char* DeviceInfo::uptimeString() {
  static char buf[20];
  time_t uptime = NTP.getUptime();
  uint16_t days = uptime / SECS_PER_DAY;
  uptime %= SECS_PER_DAY;
  uint8_t hours = uptime / SECS_PER_HOUR;
  uptime %= SECS_PER_HOUR;
  uint8_t minutes = uptime / SECS_PER_MIN;
  uint8_t seconds = uptime % SECS_PER_MIN;
  snprintf_P(buf, sizeof(buf), PSTR("%dT%02d:%02d:%02d"), days, hours, minutes, seconds);
  return buf;
}
