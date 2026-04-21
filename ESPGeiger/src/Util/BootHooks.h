/*
  BootHooks.h - Setup-time hooks for OLED and pushbutton, stubbed when not built.

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
#ifndef BOOT_HOOKS_H
#define BOOT_HOOKS_H

#include <Arduino.h>

#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif
#ifdef GEIGER_PUSHBUTTON
#include "../PushButton/PushButton.h"
#endif

namespace BootHooks {

  inline void displayWifiDisabled() {
#ifdef SSD1306_DISPLAY
    display.wifiDisabled();
#endif
  }

  inline void displaySetupWifi(const char* host) {
#ifdef SSD1306_DISPLAY
    display.setupWifi(host);
#endif
  }

  inline void displayDisableTimeout() {
#ifdef SSD1306_DISPLAY
    display.enable_oled_timeout = false;
#endif
  }

  inline void displayResetTimeout() {
#ifdef SSD1306_DISPLAY
    display.oled_timeout = millis();
#endif
  }

  // Detect "button held during startup" = user wants offline mode.
  // Blocks until the button is released. Returns true if the hold was seen.
  inline bool checkStartupButtonHold() {
#ifdef GEIGER_PUSHBUTTON
    pushbutton.init();
    if (pushbutton.isPressed()) {
      displayWifiDisabled();
      while (pushbutton.isPressed()) {
        delay(250);
        pushbutton.read();
      }
      delay(750);
      displayDisableTimeout();
      return true;
    }
#endif
    return false;
  }

}

#endif
