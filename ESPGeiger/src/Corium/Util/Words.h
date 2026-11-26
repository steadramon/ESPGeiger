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

#ifndef CORIUM_WORDS_H
#define CORIUM_WORDS_H

#include <Arduino.h>

// PROGMEM, use FPSTR()
extern const char W_OK_TEXT[] PROGMEM;
extern const char W_TRUE[] PROGMEM;
extern const char W_FALSE[] PROGMEM;

extern const char W_FMT_S[] PROGMEM;
extern const char W_FMT_D[] PROGMEM;
extern const char W_FMT_U[] PROGMEM;
extern const char W_FMT_LU[] PROGMEM;
extern const char W_FMT_LD[] PROGMEM;
extern const char W_FMT_DOT[] PROGMEM;

extern const char W_CHECKED[] PROGMEM;
extern const char W_DETAILS_END[] PROGMEM;
extern const char W_CONNECTED[] PROGMEM;
extern const char W_NONE[] PROGMEM;
extern const char W_ADMIN[] PROGMEM;
extern const char W_FACTORY_RESET[] PROGMEM;
extern const char W_UPDATE[] PROGMEM;
extern const char W_NTP[] PROGMEM;
extern const char W_BROWSER[] PROGMEM;

// reasons
extern const char W_REFUSED[] PROGMEM;
extern const char W_BAD_VALUE[] PROGMEM;
extern const char W_BAD_FORMAT[] PROGMEM;
extern const char W_TOO_SHORT[] PROGMEM;
extern const char W_TOO_LONG[] PROGMEM;
extern const char W_OOM[] PROGMEM;

#endif
