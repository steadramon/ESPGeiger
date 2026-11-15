/*
  BootHooks.h - setup hooks for OLED and pushbutton

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
#include "Corium/System/BootButton.h"
#include "Corium/System/FastMillis.h"
#include "App/LedPatterns.h"

#ifdef SSD1306_DISPLAY
#include "Display/Display.h"
#endif
#ifdef GEIGER_PUSHBUTTON
#include "PushButton/PushButton.h"
#endif
#ifdef AUDIO_TICK
#include "AudioTick/AudioTick.h"
#include "Voice/Voice.h"
#endif
#ifdef GEIGER_BUZZER
#include "Sound/Sound.h"
#endif
#ifdef ALERT_OUT
#include "AlertOut/AlertOut.h"
#endif
#ifdef PULSE_OUT
#include "PulseOut/PulseOut.h"
#endif
#ifdef GEIGER_NEOPIXEL
#include "NeoPixel/NeoPixel.h"
#endif

namespace BootHooks {

  // under 5s offline, 5s+ full reset (keeps /api.key)
  using ButtonHold = corium::BootButton::Hold;

  inline void displayWifiDisabled() {
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
    display.offline();
#endif
  }

  inline void displaySetupWifi(const char* host) {
#ifdef AUDIO_TICK
    // I2S not up yet
    audiotick.begin();
    voice.silence(1000);
    voice.speakWord("setup_wifi");
#endif
#ifdef SSD1306_DISPLAY
    display.setup_wifi(host);
#endif
#ifdef GEIGER_BUZZER
    sound.setup_portal();
#endif
  }

  // after prefs, before the join: a crash or restart left them as they were
  inline void parkOutputs(bool safe) {
#ifdef ALERT_OUT
    alertout.park();
#endif
#ifdef PULSE_OUT
    pulseout.park();
#endif
#ifdef GEIGER_NEOPIXEL
    if (safe) neopixel.park();
#endif
#ifdef GEIGER_BUZZER
    sound.park();
#endif
    (void)safe;
  }

  inline void displaySafeMode() {
#ifdef SSD1306_DISPLAY
    display.backend()->safe_mode();
#endif
  }

  inline void displayDisableTimeout() {
#ifdef SSD1306_DISPLAY
    display.keep_on();
#endif
  }

  inline void displayResetTimeout() {
#ifdef SSD1306_DISPLAY
    display.wake(corium::fastMillis());
#endif
  }

  // 0 = armed
  inline void displayWipe(int s) {
    led_hold_tick();
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
    display.wipe(s);
#endif
  }

  // Blocks until released. safe mode: full reset only, never offline
  inline ButtonHold checkStartupButtonHold(bool safe = false) {
#ifdef GEIGER_PUSHBUTTON
    pushbutton.init();
    ButtonHold hold = corium::BootButton::check([] { return pushbutton.isPressed(); },
                                        safe ? +[] {} : &displayWifiDisabled, displayWipe);
    if (safe && hold == ButtonHold::OFFLINE) {
      Serial.println(F("BOOT: safe mode - offline ignored"));
      hold = ButtonHold::NONE;
    }
    if (hold != ButtonHold::NONE) displayDisableTimeout();
    return hold;
#else
    return ButtonHold::NONE;
#endif
  }

}

#endif
