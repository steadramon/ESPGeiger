/*
  UrlParse.h - URL splitting for AsyncHTTPRequest, without String.

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
#ifndef EG_URLPARSE_H
#define EG_URLPARSE_H

#include <stddef.h>

struct EGUrlParts {
  const char* host;
  size_t      host_len;
  const char* path;
  size_t      path_len;
  const char* query;
  size_t      query_len;
  int         port;
};

bool eg_parse_url(const char* url, EGUrlParts* out);

#endif
