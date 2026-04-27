/*
  OLEDDisplay.cpp - OLEDDisplay connection class

  Copyright (C) 2023 @steadramon

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
#ifdef SSD1306_DISPLAY
#include "OLEDDisplay.h"
#include "fonts.h"
#include "logo.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/DeviceInfo.h"
#include "../Util/StringUtil.h"
#include "../Util/Wifi.h"
#include "../GRNG/GRNG.h"
#ifdef ESPG_HV_ADC
#include "../ESPGHW/ESPGHW.h"
#endif

extern uint8_t send_indicator;

#define _OLED_STR(x) #x
#define OLED_STR(x) _OLED_STR(x)

// -1 defers Wire.begin to setup() once prefs have loaded.
SSD1306Display display = SSD1306Display(OLED_ADDR, -1, -1);
EG_REGISTER_MODULE(display)

static const EGPref OLED_PREF_ITEMS[] = {
  {"brightness", "Brightness", "",      "25", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#ifdef GEIGER_PUSHBUTTON
  {"timeout",    "Timeout",    "sec, 0=use On/Off Time below", "120", nullptr, 0, 99999, 0, EGP_UINT, 0},
#endif
  {"on_time",    "On Time",    "Display on (blank = always on)",  "06:00", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
  {"off_time",   "Off Time",   "Display off (blank = always on)", "22:00", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
#ifndef OLED_PINS_BLOCKED
  {"sda",        "I2C SDA Pin","Reboot to apply", OLED_STR(OLED_SDA), nullptr, 0, 39, 0, EGP_UINT, 0},
  {"scl",        "I2C SCL Pin","Reboot to apply", OLED_STR(OLED_SCL), nullptr, 0, 39, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup OLED_PREF_GROUP = {
  "display", "Display", 1,
  OLED_PREF_ITEMS,
  sizeof(OLED_PREF_ITEMS) / sizeof(OLED_PREF_ITEMS[0]),
};

const EGPrefGroup* SSD1306Display::prefs_group() { return &OLED_PREF_GROUP; }

void SSD1306Display::on_prefs_loaded() {
  setBrightness((uint8_t)EGPrefs::getUInt("display", "brightness"));
#ifdef GEIGER_PUSHBUTTON
  setTimeout((int)EGPrefs::getUInt("display", "timeout"));
#endif
#ifndef OLED_PINS_BLOCKED
  _pin_sda = (uint8_t)EGPrefs::getUInt("display", "sda");
  _pin_scl = (uint8_t)EGPrefs::getUInt("display", "scl");
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias OLED_LEGACY[] = {
  {"dispBrightness", "brightness"},
#ifdef GEIGER_PUSHBUTTON
  {"dispTimeout", "timeout"},
#endif
  {"oledOn",  "on_time"},
  {"oledOff", "off_time"},
  {nullptr, nullptr},
};
const EGLegacyAlias* SSD1306Display::legacy_aliases() { return OLED_LEGACY; }
// === END LEGACY IMPORT ===

SSD1306Display::SSD1306Display(uint8_t _addr, int _sda, int _scl)
 : SSD1306Wire(_addr, _sda, _scl) {
  cx = 0;
  cy = 0;
}

#ifndef OLED_PINS_BLOCKED
void SSD1306Display::on_prefs_saved() {
  uint8_t sda = (uint8_t)EGPrefs::getUInt("display", "sda");
  uint8_t scl = (uint8_t)EGPrefs::getUInt("display", "scl");
  bool need_reboot = (sda != _pin_sda) || (scl != _pin_scl);
  on_prefs_loaded();
  if (need_reboot) EGPrefs::request_restart();
}
#endif

void SSD1306Display::loop(unsigned long now) {
    if (oled_page > OLED_PAGES) {
      oled_page = 1;
    }
    if (oled_page != _last_page) {
      EGModuleRegistry::set_loop_interval(this, oled_page == 4 ? 50 : 250);
      _last_page = oled_page;
    }
#ifdef GEIGER_PUSHBUTTON
    static constexpr unsigned long OLED_TAP_WAKE_MS = 30000UL;
    bool should_be_on;
    if (!enable_oled_timeout) {
      should_be_on = true;
    } else if (_lcd_timeout > 0) {
      should_be_on = ((now - oled_timeout) / 1000 <= (unsigned long)_lcd_timeout);
    } else {
      bool recent_tap = (oled_timeout != 0) && ((now - oled_timeout) < OLED_TAP_WAKE_MS);
      should_be_on = isScreenOnTime(now) || recent_tap;
    }
    if (should_be_on) {
      if (!oled_on) {
        displayOn();
        oled_page = 1;
        oled_on = true;
        oled_last_update = now - 20000;
      }
    } else {
      if (oled_on) {
        displayOff();
        oled_on = false;
      }
      return;
    }
#else
    if (isScreenOnTime(now)) {
      if (oled_on == false) {
        displayOn();
        oled_page = 1;
        oled_on = true;
        oled_last_update = now;
      }
    } else {
      if (oled_on) {
        displayOff();
        oled_on = false;
      }
      return;
    }
#endif
    if (oled_page == 1) {
      if ((now - oled_last_update >= 10000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_one_clear();
      }
      bool half = (now >> 9) & 1;
      if (half || (oled_last_update == now)) {
        page_one_values(now);
      }
      if (!half || (oled_last_update == now)) {
        page_one_graph();
      }
    } else if (oled_page == 2) {
      if ((now - oled_last_update >= 1000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_two_full();
      }
    } else if (oled_page == 3) {
      if ((now - oled_last_update >= 10000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_three_full();
      }
    } else if (oled_page == 4) {
      if (oled_last_update == 0) _page4_num_last = 0;
      oled_last_update = now;
      page_four_static();
    }
    display();

  }

  bool SSD1306Display::isScreenOnTime(unsigned long now) {
    static int16_t cached_on_mins = -1;
    static int16_t cached_off_mins = -1;

    static uint8_t tz_cnt = 0;
    if (cached_on_mins == -1 || ++tz_cnt >= 120) {
      tz_cnt = 0;
      ParsedTime on_time  = parseTime(EGPrefs::getString("display", "on_time"));
      ParsedTime off_time = parseTime(EGPrefs::getString("display", "off_time"));
      if (!on_time.isValid || !off_time.isValid) { cached_on_mins = -2; return true; }
      cached_on_mins = on_time.hour * 60 + on_time.minute;
      cached_off_mins = off_time.hour * 60 + off_time.minute;
    }

    if (cached_on_mins == -2) return true;

    time_t currentTime = time(NULL);
    struct tm *timeinfo = localtime(&currentTime);
    if (!timeinfo) return true;

    int now_mins = timeinfo->tm_hour * 60 + timeinfo->tm_min;
    if (cached_on_mins < cached_off_mins)
      return (now_mins >= cached_on_mins && now_mins < cached_off_mins);
    else
      return (now_mins >= cached_on_mins || now_mins < cached_off_mins);
  }

void SSD1306Display::page_two_full() {
  clear();
  setFont(ArialMT_Plain_10);
  drawString(0, 2, DeviceInfo::hostname());
  drawString(0, 17, PSTR("IP:"));
  drawString(16, 17, Wifi::ip);
  int uptime_y = 32;
#ifdef ESPG_HV_ADC
  drawString(0, uptime_y, PSTR("HV:"));
  char hvBuf[12];
  format_f(hvBuf, sizeof(hvBuf), hardware.hvReading.get());
  drawString(20, uptime_y, hvBuf);
  uptime_y = 47;
#endif
  drawString(0, uptime_y, DeviceInfo::uptimeString());
}

void SSD1306Display::page_one_clear() {
  clear();
  setFont(DialogInput_plain_12);
  drawString(0,5, PSTR("CPM:"));
  drawString(0,20, PSTR("µSv/h:"));
  if (Wifi::disabled) {
    return;
  }
  drawXbm(120, 0, fontWidth, fontHeight, Wifi::connected?_iconimage_connected:_iconimage_disconnected);
}

void SSD1306Display::setup() {
  Wire.begin(_pin_sda, _pin_scl);
  SSD1306Wire::init();
#if OLED_FLIP
  flipScreenVertically();
#endif
  setBrightness(64);
  setFont(ArialMT_Plain_10);
  fontWidth = 8;
  fontHeight = 16;
  clear();
  drawXbm(0, 0, 51, 51, ESPLogo);
  drawString(55, 10, PSTR("ESPGeiger"));
  drawString(55, 24, RELEASE_VERSION);
  drawString(55, 42, PSTR("Connecting .."));

  display();
  setFont(ArialMT_Plain_16);
}

void SSD1306Display::onButtonTap(unsigned long now) {
  static unsigned long s_last_tap = 0;
  static uint8_t s_tap_count = 0;
  s_tap_count = (now - s_last_tap < 400) ? (s_tap_count + 1) : 1;
  s_last_tap = now;

  oled_timeout = now;
  if (!oled_on) {
    oled_page = 1;
    displayOn();
    oled_on = true;
  } else if (s_tap_count >= 5) {
    oled_page = 4;            // hidden gesture: 5 rapid taps
    s_tap_count = 0;
  } else {
    oled_page++;
    if (oled_page > 3) oled_page = 1;   // normal rotation skips page 4
  }
  oled_last_update = 0;
  loop(now);
}

void SSD1306Display::setupWifi(const char* s) {
  clear();
  setFont(DialogInput_plain_12);
  drawString(0, 10, PSTR("Setup - Connect to"));
  drawString(0, 24, PSTR("WiFi -"));
  drawString(0, 38, s);
  display();
}

void SSD1306Display::wifiDisabled() {
  setFont(ArialMT_Plain_10);
  fontWidth = 8;
  fontHeight = 16;
  clear();
  drawXbm(0, 0, 51, 51, ESPLogo);
  drawString(55, 10, PSTR("ESPGeiger"));
  drawString(55, 24, RELEASE_VERSION);
  drawString(55, 42, PSTR("Offline mode"));

  display();
  setFont(ArialMT_Plain_16);
}

void SSD1306Display::page_one_graph() {
  setColor(BLACK);
  fillRect(0, 35, OLED_WIDTH, 29);
  setColor(WHITE);

  drawLine(0, 63, 90, 63);
  drawLine(90, 35, 90, 63);
  if (gcounter.cpm_history.size() > 0) {
    int histSize = gcounter.cpm_history.size();
    int maxValue = gcounter.cpm_history[0];
    int minValue = histSize > 1 ? gcounter.cpm_history[0]:0;
    for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
      maxValue = gcounter.cpm_history[i] > maxValue ? gcounter.cpm_history[i] : maxValue;
      minValue = gcounter.cpm_history[i] < minValue ? gcounter.cpm_history[i] : minValue;
    }

    if (minValue > 1) {
      minValue = (int)(minValue * 0.9);
    }
    int x_start = 0;
    if (gcounter.cpm_history.capacity != histSize) {
      x_start = (2 * (gcounter.cpm_history.capacity - histSize));
    }

    char graphBuf[8];
    if (maxValue == 0) {
      setFont(Open_Sans_Digits_10);
      snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), minValue);
      drawString(93,55, graphBuf);
      return;
    }

    for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
      int location = ((map((long)gcounter.cpm_history[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;
      drawRect(x_start + i * 2, location, 2, (63 - location));
    }
    setFont(Open_Sans_Digits_10);
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), minValue);
    drawString(93,55, graphBuf);
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), maxValue);
    drawString(93,35, graphBuf);
  }
}

void SSD1306Display::page_one_values(unsigned long now) {
  setFont(DialogInput_Digits_17);
  setColor(BLACK);
  fillRect(45, 0, 72, 32);
  setColor(WHITE);
  char oledBuf[16];
  snprintf_P(oledBuf, sizeof(oledBuf), PSTR("%d"), gcounter.get_cpm());
  drawString(45,0, oledBuf);
  setFont(DialogInput_plain_12);
  format_f(oledBuf, sizeof(oledBuf), gcounter.get_usv());
  drawString(45,20, oledBuf);
  if (gcounter.cpm_history.capacity != gcounter.cpm_history.size()) {
    drawString(98,2, PSTR("W") );
  }
  if (send_indicator) {
    drawXbm(110, 0, fontWidth, fontHeight, _iconimage_remotext);
    send_indicator--;
  }
}

void SSD1306Display::page_three_full() {
  clear();
  setFont(ArialMT_Plain_10);
  static char versionString[32] = "";
  if (versionString[0] == '\0') {
    snprintf_P(versionString, sizeof(versionString), PSTR("%S / %S"), RELEASE_VERSION, GIT_VERSION);
  }
  drawString(0, 2, GEIGER_MODEL);
  drawString(0, 17, versionString);
  drawString(0, 32, PSTR(__DATE__ " " __TIME__));
  drawString(0, 47, PSTR("@ steadramon"));
}

void SSD1306Display::page_four_static() {
  unsigned long now = millis();
  if (_page4_num_last == 0 || now - _page4_num_last >= 1000) {
    _page4_num_last = now;
    uint16_t num;
    GRNG::extract((uint8_t*)&num, sizeof(num));
    num %= 1001;
    snprintf_P(_page4_num, sizeof(_page4_num), PSTR("%u"), num);
  }
  uint8_t noise[1024];
  GRNG::extract(noise, sizeof(noise));
  clear();
  drawXbm(0, 0, 128, 64, noise);
  setColor(BLACK);
  fillRect(41, 19, 44, 24);
  setColor(WHITE);
  setFont(DialogInput_Digits_17);
  drawString(63 - (int)strlen(_page4_num) * 5, 21, _page4_num);
}

void SSD1306Display::showOTABanner() {
  clear();
  setTextAlignment(TEXT_ALIGN_CENTER);
  setFont(ArialMT_Plain_16);
  drawString(OLED_WIDTH / 2, (OLED_HEIGHT / 2) - 12, "Update");
  drawString(OLED_WIDTH / 2, (OLED_HEIGHT / 2) + 8,  "in progress...");
  displayOn();
  oled_on = true;
  display();
  setTextAlignment(TEXT_ALIGN_LEFT);
}
#endif
