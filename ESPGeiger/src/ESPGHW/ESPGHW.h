/*
  Counter.h - Geiger Counter class
  
  Copyright (C) 2023 @steadramon

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

#ifndef ESPGHW_H
#define ESPGHW_H
#include <Arduino.h>
#include "../Status.h"

#ifndef GEIGER_PWMPIN
#define GEIGER_PWMPIN 4
#endif

class ESPGeigerHW {
    public:
      ESPGeigerHW();
      void loop();
      void begin();
};

#endif