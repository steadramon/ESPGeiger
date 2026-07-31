/*
  EGHttpStrings.h - HTTP content types and header words, one flash copy each.

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

#ifndef EGHTTP_STRINGS_H
#define EGHTTP_STRINGS_H

#include <Arduino.h>

extern const char HTTP_CT_PLAIN[] PROGMEM;
extern const char HTTP_CT_HTML[] PROGMEM;
extern const char HTTP_CT_JSON[] PROGMEM;
extern const char HTTP_CT_JS[] PROGMEM;

extern const char HTTP_H_CACHE[] PROGMEM;
extern const char HTTP_H_CTYPE[] PROGMEM;
extern const char HTTP_H_UA[] PROGMEM;

extern const char HTTP_CACHE_FOREVER[] PROGMEM;

#endif
