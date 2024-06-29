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

void SSD1306Display::page_two_full() {
  ConfigManager &configManager = ConfigManager::getInstance();
  clear();
  setFont(ArialMT_Plain_10);
  drawString(0,5, String(configManager.getHostName()));
  drawString(0,20, PSTR("IP:"));
  drawString(16, 20, WiFi.localIP().toString());
  int uptime_y = 35;
#ifdef ESPGEIGER_HW
  drawString(0, 35, PSTR("HV:"));
  drawString(20, 35, String(status.hvReading.get()));
  uptime_y = 50;
#endif
  drawString(0, uptime_y, configManager.getUptimeString());
}

void SSD1306Display::page_one_clear() {
  clear();
  drawXbm(120, 0, fontWidth, fontHeight, WiFi.status()==WL_CONNECTED?_iconimage_connected:_iconimage_disconnected);
  setFont(DialogInput_plain_12);
  drawString(0,5, PSTR("CPM:"));
  drawString(0,20, PSTR("µSv/h:"));
}
#endif