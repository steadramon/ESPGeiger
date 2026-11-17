/*
  Words.cpp - Strings shared across modules, one flash copy each.

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

#include "Words.h"

const char W_OK_TEXT[]     PROGMEM = "OK";
const char W_TRUE[]        PROGMEM = "true";
const char W_FALSE[]       PROGMEM = "false";

const char W_FMT_S[]       PROGMEM = "%s";
const char W_FMT_D[]       PROGMEM = "%d";
const char W_FMT_U[]       PROGMEM = "%u";
const char W_FMT_LU[]      PROGMEM = "%lu";
const char W_FMT_LD[]      PROGMEM = "%ld";
const char W_FMT_DOT[]     PROGMEM = "%s.%s";

const char W_CHECKED[]     PROGMEM = " checked";
const char W_DETAILS_END[] PROGMEM = "</details>";
const char W_CONNECTED[]   PROGMEM = "<p class=muted>Connected to <b>%s";
const char W_NONE[]        PROGMEM = "(none)";
const char W_ADMIN[]       PROGMEM = "admin";
const char W_FACTORY_RESET[] PROGMEM = "Factory reset";
const char W_UPDATE[]      PROGMEM = "Update";
const char W_NTP[]         PROGMEM = "NTP";
const char W_BROWSER[]     PROGMEM = "browser";

const char W_REFUSED[]      PROGMEM = "refused";
const char W_BAD_VALUE[]    PROGMEM = "bad value";
const char W_BAD_FORMAT[]   PROGMEM = "bad format";
const char W_TOO_SHORT[]    PROGMEM = "too short";
const char W_TOO_LONG[]     PROGMEM = "too long";
const char W_OOM[]          PROGMEM = "out of memory";
