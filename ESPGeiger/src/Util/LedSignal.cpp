/*
  LedSignal.cpp - see header.

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
#include "LedSignal.h"
#include <EGLed.h>
#include "FastMillis.h"
#include "Globals.h"

namespace LedSignal {

  static EGLed s_onboard(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON == LOW);

#ifdef GEIGER_BLIPLED
  static EGLed s_external(GEIGER_BLIPLED);
#endif

  void begin() {
    EGLed::setClock(fast_millis);
  }

  void poll() {
    s_onboard.update();
#ifdef GEIGER_BLIPLED
    s_external.update();
#endif
  }

  void off() {
    s_onboard.off();
  }

  void click() {
#ifndef DISABLE_INTERNAL_BLIP
    s_onboard.blink(20, 20);
#endif
#ifdef GEIGER_BLIPLED
    if (!s_external.isRunning()) s_external.pulse(2);
#endif
  }

  void activity() {
    s_onboard.blink(500, 500);
  }

  void shortPressAck() {
    s_onboard.pulse(200);
  }

  void displayEnabled() {
#ifdef GEIGER_BLIPLED
    s_external.blinkN(200, 100, 2);
#endif
  }

  void displayDisabled() {
#ifdef GEIGER_BLIPLED
    s_external.pulse(200);
#endif
  }

  void setBrightness(uint8_t level) {
    s_onboard.setBrightness(level);
  }

}
