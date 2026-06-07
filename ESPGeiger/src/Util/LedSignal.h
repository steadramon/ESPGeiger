/*
  LedSignal.h - Application LED patterns over EGLed.

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
#ifndef _LEDSIGNAL_H
#define _LEDSIGNAL_H

#include <stdint.h>

namespace LedSignal {

  // Lifecycle. begin() runs once from setup(), poll() runs from msTickerCB
  // every 1 ms, off() turns the onboard LED off at the end of setup.
  void begin();
  void poll();
  void off();

  // Per-pulse click feedback. Caller is responsible for the master
  // enable and quiet-hours gates.
  void click();

  // Network-post-in-flight indicator.
  void activity();

  // Push-button feedback.
  void shortPressAck();
  void displayEnabled();
  void displayDisabled();

  void setBrightness(uint8_t level);

}

#endif
