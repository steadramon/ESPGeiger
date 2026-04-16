/*
  GeigerInput/Type/Pulse.h - Class for Pulse type counter
  
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
#ifndef GEIGERINPUTPLS_H
#define GEIGERINPUTPLS_H

#include <Arduino.h>
#include "../GeigerInput.h"

#ifdef USE_PCNT
extern "C" {
  #include "soc/pcnt_struct.h"
}
#include <driver/pcnt.h>
#define PCNT_HIGH_LIMIT 32767  // largest +ve value for int16_t.
#define PCNT_LOW_LIMIT  0

#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0
// Pull-mode encoding shared with the web portal config (pcntPull):
//   0 = floating / no pull
//   1 = pull-up  (default — matches active-low modules with external pullup)
//   2 = pull-down (active-high modules without own idle pull)
// The compile-time flags set the *default* visible in the portal; users can
// override per-device from the web form.
#define PCNT_PULL_FLOATING 0
#define PCNT_PULL_UP       1
#define PCNT_PULL_DOWN     2
#if defined(PCNT_FLOATING_PIN)
#define PCNT_PIN_PULL_DEFAULT PCNT_PULL_FLOATING
#elif defined(PCNT_PULLDOWN_PIN)
#define PCNT_PIN_PULL_DEFAULT PCNT_PULL_DOWN
#else
#define PCNT_PIN_PULL_DEFAULT PCNT_PULL_UP
#endif
#endif

#ifndef GEIGER_MODEL
#define GEIGER_MODEL "genpulse"
#endif

class GeigerPulse : public GeigerInput
{
  public:
    GeigerPulse();
    void begin();
    bool has_pcnt() override {
#ifdef USE_PCNT
      return true;
#else
      return false;
#endif
    }
#ifdef USE_PCNT
    int collect();
    void set_pcnt_filter(int val);
    void apply_pcnt_filter();
    void set_pin_pull(int mode);   // 0=floating, 1=up, 2=down
  private:
    int _pcnt_filter = 0;
    int _pin_pull = PCNT_PIN_PULL_DEFAULT;
#endif
};
#endif
