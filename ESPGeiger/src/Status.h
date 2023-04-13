/*
  Status.h - Status class
  
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

#ifndef Status_h
#define Status_h

#include <Smoothed.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

struct Status {
  const char thingName[11] = "ESPGeiger";
  const char* version = RELEASE_VERSION;
  const char* git_version = GIT_VERSION;
  bool mqtt_connected = false;
  Smoothed <float> geigerTicks;
  Smoothed <float> geigerTicks5;
  Smoothed <float> geigerTicks15;
  long start = 0;
};

#endif