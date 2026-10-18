/*
  ClickHooks.h - What runs on each click, stubbed when not built.

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
#ifndef CLICK_HOOKS_H
#define CLICK_HOOKS_H

#include <Arduino.h>
#include "Corium/System/FastMillis.h"
#include "Corium/Output/LedSignal.h"
#include "GRNG/GRNG.h"
#include "UdpBlip/UdpBlip.h"
#ifdef AUDIO_TICK
#include "AudioTick/AudioTick.h"
#endif
#ifdef PULSE_OUT
#include "PulseOut/PulseOut.h"
#endif

namespace ClickHooks {

  inline void click(uint32_t blip_us, bool led) {
    GRNG::mix(blip_us ^ ESP.getCycleCount());
    if (led) LedSignal::click();
    unsigned long now_ms = fast_millis();
    udpblip.notifyClick(now_ms);
#ifdef AUDIO_TICK
    audiotick.notifyClick(now_ms);
#endif
#ifdef PULSE_OUT
    pulseout.notifyClick(now_ms);
#endif
  }

  // udp receiver, no echo
  inline void rx_click(bool led) {
    if (led) LedSignal::click();
#if defined(AUDIO_TICK) || defined(PULSE_OUT)
    unsigned long now_ms = fast_millis();
#endif
#ifdef AUDIO_TICK
    audiotick.notifyClick(now_ms);
#endif
#ifdef PULSE_OUT
    pulseout.notifyClick(now_ms);
#endif
  }

}

#endif
