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
#include "../Logger/Logger.h"
#include "../ConfigManager/ConfigManager.h"

SSD1306Display::SSD1306Display(uint8_t _addr, uint8_t _sda, uint8_t _scl)
 : SSD1306Wire(_addr, _sda, _scl) {
  cx = 0;
  cy = 0;
}

void SSD1306Display::loop(unsigned long now) {
    if (now - _last_update < 500) {
      return;
    }
    if (status.oled_page > 3) {
      status.oled_page = 1;
    }
#ifdef GEIGER_PUSHBUTTON
    if ((status.enable_oled_timeout) && (_lcd_timeout > 0) && (now - status.oled_timeout > _lcd_timeout)) {
      if (status.oled_on) {
        displayOff();
        status.oled_on = false;
      }
      return;
    } else {
      if (status.oled_on == false) {
        displayOn();
        status.oled_page=1;
        status.oled_on = true;
        status.oled_last_update = now-20000;
      }
    }
#else
    if (isScreenOnTime()) {
      if (status.oled_on == false) {
        displayOn();
        status.oled_page = 1;
        status.oled_on = true;
        status.oled_last_update = now;
      }
    } else {
      if (status.oled_on) {
        displayOff();
        status.oled_on = false;
      }
      return;
    }
#endif
    if (status.oled_page == 1) {
      if ((now - status.oled_last_update >= 10000) || (status.oled_last_update == 0)) {
        status.oled_last_update = now;
        page_one_clear();
      }
      if ((now % 1000 >= 500) || (status.oled_last_update == now)) {
        page_one_values(now);
      }
      if ((now % 1000 < 500) || (status.oled_last_update == now)) {
        page_one_graph();
      }
    } else if (status.oled_page == 2) {
      if ((now - status.oled_last_update >= 1000) || (status.oled_last_update == 0)) {
        status.oled_last_update = now;
        page_two_full();
      }
    } else if (status.oled_page == 3) {
      if ((now - status.oled_last_update >= 10000) || (status.oled_last_update == 0)) {
        status.oled_last_update = now;
        page_three_full();
      }
    }
    display();
    _last_update = now;

  }

  bool SSD1306Display::isScreenOnTime() {
    ConfigManager &configManager = ConfigManager::getInstance();
    // 1. Parse the input time strings
    const char* screen_on = configManager.getParamValueFromID("oledOn");
    const char* screen_off = configManager.getParamValueFromID("oledOff");
    ParsedTime on_time = configManager.parseTime(screen_on);
    ParsedTime off_time = configManager.parseTime(screen_off);

    if (!on_time.isValid || !off_time.isValid) {
        return true;
    }
    time_t currentTime = time (NULL);

    // 2. Get current time components
    struct tm *timeinfo = localtime(&currentTime);

    if (timeinfo == NULL) {
        return true;
    }

    int current_hour = timeinfo->tm_hour;
    int current_min = timeinfo->tm_min;

    int current_time_in_mins = current_hour * 60 + current_min;
    int on_time_in_mins = on_time.hour * 60 + on_time.minute;
    int off_time_in_mins = off_time.hour * 60 + off_time.minute;

    if (on_time_in_mins < off_time_in_mins) {
        return (current_time_in_mins >= on_time_in_mins &&
                current_time_in_mins < off_time_in_mins);
    } else {
        return (current_time_in_mins >= on_time_in_mins ||
                current_time_in_mins < off_time_in_mins);
    }
  }

void SSD1306Display::page_two_full() {
  ConfigManager &configManager = ConfigManager::getInstance();
  clear();
  setFont(ArialMT_Plain_10);
  drawString(0, 2, String(configManager.getHostName()));
  drawString(0, 17, PSTR("IP:"));
  drawString(16, 17, WiFi.localIP().toString());
  int uptime_y = 32;
#ifdef ESPGEIGER_HW
  drawString(0, uptime_y, PSTR("HV:"));
  drawString(20, uptime_y, String(status.hvReading.get()));
  uptime_y = 47;
#endif
  drawString(0, uptime_y, configManager.getUptimeString());
}

void SSD1306Display::page_one_clear() {
  clear();
  setFont(DialogInput_plain_12);
  drawString(0,5, PSTR("CPM:"));
  drawString(0,20, PSTR("µSv/h:"));
  if (status.wifi_disabled) {
    return;
  }
  drawXbm(120, 0, fontWidth, fontHeight, WiFi.status()==WL_CONNECTED?_iconimage_connected:_iconimage_disconnected);
}
#endif