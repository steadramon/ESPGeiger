/*
  GeigerInput/Type/TestPulse.h - Class for Test Pulse type counter

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
#include "GeigerInput/GeigerInput.h"
#include "Pcnt.h"

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN 12
#endif

#include "GeigerInput/GeigerInputTest.h"

#ifndef GEIGER_MODEL
#define GEIGER_MODEL "genpulse"
#endif

class GeigerTestPulse : public GeigerInputTest
{
  public:
    GeigerTestPulse();
    void begin();
    void loop();
    void stopForOTA() override;
    void restartAfterOTA() override;
    static uint32_t IRAM_ATTR pulseInterrupt();
    void secondTicker();
#ifdef USE_PCNT
    int collect();
#endif
};
#endif
