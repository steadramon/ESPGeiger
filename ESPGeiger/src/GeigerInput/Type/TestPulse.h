/*
  GeigerInput/Type/TestPulse.cpp - Class for Test Pulse type counter
  
  Copyright (C) 2024 @steadramon

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
#ifndef GEIGERTESTPLS_H
#define GEIGERTESTPLS_H

#include <Arduino.h>

#ifdef ESP32
#ifndef IGNORE_PCNT
#define USE_PCNT
#endif
#endif

#ifdef USE_PCNT
extern "C" {
  #include "soc/pcnt_struct.h"
}
#include <driver/pcnt.h>
#define PCNT_HIGH_LIMIT 32767  // largest +ve value for int16_t.
#define PCNT_LOW_LIMIT  0

#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0
#endif

#ifndef GEIGER_DEBOUNCE
#define GEIGER_DEBOUNCE 500
#endif

#ifndef GEIGER_MODEL
#define GEIGER_MODEL "genpulse"
#endif

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN 12
#endif

#include "../GeigerInput.h"

static bool _pulse_send = false;

class GeigerTestPulse : public GeigerInput
{
  public:
    GeigerTestPulse();
    void begin();
    void loop();
    static void IRAM_ATTR pulseInterrupt();
    void secondticker();
#ifdef USE_PCNT && ESP32
    int collect();
#endif
  private:
    bool _bool_pulse_state = false;
    int _current_selection = 0;
};
#endif