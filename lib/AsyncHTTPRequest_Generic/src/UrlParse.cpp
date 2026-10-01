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

// http URLs split as CPython urllib.parse.urlsplit does, with .hostname and
// .port rules. No scheme means http. Rejected: other schemes, an empty host,
// userinfo and [IPv6] (the client cannot honour them), a port that .port
// would refuse, and any space or control byte (urlsplit strips tab and
// CR/LF; here they could reach the request line).

static bool is_http(const char* s, size_t n) {
  return n == 4 &&
         (s[0] | 0x20) == 'h' && (s[1] | 0x20) == 't' &&
         (s[2] | 0x20) == 't' && (s[3] | 0x20) == 'p';
}

bool eg_parse_url(const char* url, EGUrlParts* out) {
  if (url == NULL || out == NULL)
    return false;

  memset(out, 0, sizeof(*out));
  out->port = 80;

  for (const char* c = url; *c; c++)
    if ((unsigned char)*c <= ' ' || *c == 0x7F)
      return false;

  // A scheme is the leading run of scheme characters before "://".
  const char* s = url;
  while ((*s >= '0' && *s <= '9') || ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'z') ||
         *s == '+' || *s == '-' || *s == '.') s++;
  if (s > url && ((*url | 0x20) >= 'a' && (*url | 0x20) <= 'z') &&
      s[0] == ':' && s[1] == '/' && s[2] == '/') {
    if (!is_http(url, (size_t)(s - url)))
      return false;
    s += 3;
  } else {
    s = url;
  }

  const char* end   = url + strlen(url);
  const char* frag  = strchr(s, '#');
  if (frag) end = frag;
  const char* netloc_end = s;
  while (netloc_end < end && *netloc_end != '/' && *netloc_end != '?') netloc_end++;

  const char* host_end = netloc_end;
  for (const char* c = s; c < netloc_end; c++) {
    if (*c == '@' || *c == '[' || *c == ']')
      return false;
    if (*c == ':' && host_end == netloc_end)
      host_end = c;
  }
  if (host_end == s)
    return false;

  if (host_end < netloc_end && host_end + 1 < netloc_end) {
    long port = 0;
    for (const char* c = host_end + 1; c < netloc_end; c++) {
      if (*c < '0' || *c > '9')
        return false;
      port = port * 10 + (*c - '0');
      if (port > 65535)
        return false;
    }
    out->port = (int)port;
  }

  out->host     = s;
  out->host_len = (size_t)(host_end - s);

  const char* q = netloc_end;
  while (q < end && *q != '?') q++;
  if (q == netloc_end) {
    out->path     = "/";
    out->path_len = 1;
  } else {
    out->path     = netloc_end;
    out->path_len = (size_t)(q - netloc_end);
  }

  if (end - q > 1) {
    out->query     = q;
    out->query_len = (size_t)(end - q);
  } else {
    out->query     = end;
    out->query_len = 0;
  }

  return true;
}
