/*
  EGHttpStrings.cpp - HTTP content types and header words, one flash copy each.

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

#include "EGHttpStrings.h"

const char HTTP_CT_PLAIN[]   PROGMEM = "text/plain";
const char HTTP_CT_HTML[]    PROGMEM = "text/html";
const char HTTP_CT_JSON[]    PROGMEM = "application/json";
const char HTTP_CT_JS[]      PROGMEM = "application/javascript";

const char HTTP_H_CACHE[]    PROGMEM = "Cache-Control";
const char HTTP_H_CTYPE[]    PROGMEM = "Content-Type";
const char HTTP_H_UA[]       PROGMEM = "User-Agent";

const char HTTP_CACHE_FOREVER[] PROGMEM = "public, max-age=31536000, immutable";
