/*
  GeigerInput/Type/Test.h - Class for Test type counter
  
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
#ifndef GEIGERTEST_H
#define GEIGERTEST_H

#include <Arduino.h>
#include "../GeigerInput.h"

#ifndef GEIGER_MODEL
#define GEIGER_MODEL "test"
#endif

#ifdef ESP32
static hw_timer_t * hwtimer = NULL;
#endif

class GeigerTest : public GeigerInput
{
  public:
    GeigerTest();
    void begin();
#ifdef ESP32
    int collect();
#endif
};
#endif