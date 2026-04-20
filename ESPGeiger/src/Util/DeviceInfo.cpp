/*
  DeviceInfo.cpp - Device identity and utility functions.

  Copyright (C) 2025 @steadramon
*/
#include "DeviceInfo.h"
#ifdef ESP32
#include <esp_chip_info.h>
#include <esp_system.h>
#endif
#ifdef ESP8266
extern "C" {
#include <user_interface.h>
}
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

uint8_t DeviceInfo::resetReason() {
#ifdef ESP8266
  // ESP8266 rst_info::reason values from user_interface.h:
  //   0 = REASON_DEFAULT_RST (power-on)
  //   1 = REASON_WDT_RST (hardware watchdog)
  //   2 = REASON_EXCEPTION_RST
  //   3 = REASON_SOFT_WDT_RST (software watchdog)
  //   4 = REASON_SOFT_RESTART (ESP.restart())
  //   5 = REASON_DEEP_SLEEP_AWAKE
  //   6 = REASON_EXT_SYS_RST (reset pin / button)
  uint32_t r = ESP.getResetInfoPtr()->reason;
  switch (r) {
    case 0: return 1;
    case 6: return 2;
    case 4: return 3;
    case 2: return 4;
    case 1: case 3: return 5;
    case 5: return 7;
    default: return 0;
  }
#elif defined(ESP32)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return 1;
    case ESP_RST_EXT:       return 2;
    case ESP_RST_SW:        return 3;
    case ESP_RST_PANIC:     return 4;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return 5;
    case ESP_RST_BROWNOUT:  return 6;
    case ESP_RST_DEEPSLEEP: return 7;
    default:                return 0;
  }
#else
  return 0;
#endif
}

uint16_t DeviceInfo::featureFlags() {
  uint16_t f = 0;
#ifdef MQTTOUT
  f |= 1 << 0;
#endif
#ifdef RADMONOUT
  f |= 1 << 1;
#endif
#ifdef THINGSPEAKOUT
  f |= 1 << 2;
#endif
#ifdef WEBHOOKOUT
  f |= 1 << 3;
#endif
#ifdef SERIALOUT
  f |= 1 << 4;
#endif
#ifdef GMCOUT
  f |= 1 << 5;
#endif
#ifdef SSD1306_DISPLAY
  f |= 1 << 6;
#endif
#ifdef GEIGER_PUSHBUTTON
  f |= 1 << 7;
#endif
#ifdef GEIGER_SDCARD
  f |= 1 << 8;
#endif
#ifdef GEIGER_NEOPIXEL
  f |= 1 << 9;
#endif
  return f;
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
