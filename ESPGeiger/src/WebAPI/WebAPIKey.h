/*
  WebAPIKey.h - the station key's signing and reset, shared by the WebAPI files.

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

#ifndef WEBAPIKEY_H
#define WEBAPIKEY_H

#include <stdint.h>
#include <stddef.h>
#include <EGBase64.h>
#include "WebAPI.h"

#ifndef WEBAPI_URL
#define WEBAPI_URL "http://api.espgeiger.com"
#endif

#define WEBAPI_SIG_B64 (EGB64_LEN(WEBAPI_SIG_LEN) + 1)
#define ATT_MAC 6   // efuse mac bytes

bool forget_key();

#endif
