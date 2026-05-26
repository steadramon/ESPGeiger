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
#include "FastMillis.h"

#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif
#ifdef GEIGER_PUSHBUTTON
#include "../PushButton/PushButton.h"
#endif

namespace BootHooks {

  // Hold ladder: released <5s = OFFLINE, held >=5s = FULL_RESET
  // (wipes prefs/WiFi but keeps /api.key so station ID survives).
  enum class ButtonHold : uint8_t { NONE = 0, OFFLINE = 1, FULL_RESET = 2 };

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
    display.oled_timeout = fast_millis();
#endif
  }

  inline void displayWipeCountdown(int s) {
#ifdef SSD1306_DISPLAY
    display.wipeCountdown(s);
#endif
  }

  inline void displayWipeReady() {
#ifdef SSD1306_DISPLAY
    display.wipeReady();
#endif
  }

  // Blocks until button released. OLED counts down; serial logs transitions.
  inline ButtonHold checkStartupButtonHold() {
#ifdef GEIGER_PUSHBUTTON
    pushbutton.init();
    if (!pushbutton.isPressed()) return ButtonHold::NONE;

    Serial.println(F("BOOT: button held - offline mode, hold 5s for factory reset"));
    displayWifiDisabled();
    uint32_t start = fast_millis();
    int last_shown = 999;
    bool wipe = false;

    while (pushbutton.isPressed()) {
      uint32_t held_ms = fast_millis() - start;
      int remain = 5 - (int)(held_ms / 1000);
      if (remain != last_shown) {
        last_shown = remain;
        if (remain >= 1) {
          displayWipeCountdown(remain);
        } else if (!wipe) {
          wipe = true;
          displayWipeReady();
          Serial.println(F("BOOT: factory reset armed"));
        }
      }
      delay(50);
    }
    delay(500);
    displayDisableTimeout();
    return wipe ? ButtonHold::FULL_RESET : ButtonHold::OFFLINE;
#else
    return ButtonHold::NONE;
#endif
  }

}

#endif
