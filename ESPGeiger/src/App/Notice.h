/*
  Notice.h - One short line on the OLED till the button is pressed.

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
#ifndef APP_NOTICE_H
#define APP_NOTICE_H

#include "App/AppConfig.h"

// Takes a PROGMEM string and the latest one wins. Loop only, Display.cpp holds it
namespace Notice {
#ifdef APP_NOTICE
  void show(const char* msg);
  void end(const char* msg);   // only if it's still up
  const char* shown();
#else
  inline void show(const char*) {}
  inline void end(const char*) {}
#endif
}

#endif
