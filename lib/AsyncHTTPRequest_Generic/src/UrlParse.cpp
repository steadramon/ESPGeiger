/*
  UrlParse.cpp - URL splitting for AsyncHTTPRequest, without String.

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
#include "UrlParse.h"

#include <string.h>
#include <stdlib.h>

static const char HTTP_PREFIX[]  = "HTTP://";
static const char HTTPS_PREFIX[] = "HTTPS://";

static bool prefix_eq_ci(const char* s, const char* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char a = s[i];
    char b = p[i];
    if (a >= 'a' && a <= 'z') a = (char)(a - 32);
    if (b >= 'a' && b <= 'z') b = (char)(b - 32);
    if (a != b)
      return false;
  }
  return true;
}

static int index_of(const char* s, size_t len, char ch, size_t from) {
  for (size_t i = from; i < len; i++) {
    if (s[i] == ch)
      return (int)i;
  }
  return -1;
}

bool eg_parse_url(const char* url, EGUrlParts* out) {
  if (url == NULL || out == NULL)
    return false;

  memset(out, 0, sizeof(*out));
  out->port = 80;

  const size_t len       = strlen(url);
  const size_t http_len  = sizeof(HTTP_PREFIX) - 1;
  const size_t https_len = sizeof(HTTPS_PREFIX) - 1;

  size_t hostBeg = 0;

  if (len >= http_len && prefix_eq_ci(url, HTTP_PREFIX, http_len))
    hostBeg = http_len;
  else if (len >= https_len && prefix_eq_ci(url, HTTPS_PREFIX, https_len))
    return false;

  const int pathBeg = index_of(url, len, '/', hostBeg);
  const int colon   = index_of(url, len, ':', hostBeg);

  int queryBeg = index_of(url, len, '?', 0);

  if (queryBeg < 0)
    queryBeg = (int)len;

  int hostEnd = (int)len;

  if (pathBeg >= 0 && pathBeg < hostEnd)
    hostEnd = pathBeg;

  if (queryBeg < hostEnd)
    hostEnd = queryBeg;

  if (colon > 0 && colon < hostEnd) {
    out->port = (int)strtol(url + colon + 1, NULL, 10);
    hostEnd = colon;
  }

  out->host     = url + hostBeg;
  out->host_len = (size_t)hostEnd - hostBeg;

  if (pathBeg < 0 || pathBeg > queryBeg) {
    out->path     = "/";
    out->path_len = 1;
  } else {
    out->path     = url + pathBeg;
    out->path_len = (size_t)(queryBeg - pathBeg);
  }

  out->query     = url + queryBeg;
  out->query_len = len - (size_t)queryBeg;

  return true;
}
