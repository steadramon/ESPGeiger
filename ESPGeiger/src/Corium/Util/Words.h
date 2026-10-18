/*
  Words.h - Strings shared across modules, one flash copy each.

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

#ifndef UTIL_WORDS_H
#define UTIL_WORDS_H

#include <Arduino.h>

// PROGMEM, use FPSTR()
extern const char W_OK_TEXT[] PROGMEM;
extern const char W_TRUE[] PROGMEM;
extern const char W_FALSE[] PROGMEM;
extern const char W_APP_NAME[] PROGMEM;

extern const char W_FMT_S[] PROGMEM;
extern const char W_FMT_D[] PROGMEM;
extern const char W_FMT_U[] PROGMEM;

extern const char W_CHECKED[] PROGMEM;
extern const char W_DETAILS_END[] PROGMEM;

#endif
