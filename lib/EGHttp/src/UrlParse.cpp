/*
  UrlParse.cpp - URL splitting for EGHttpClient, without String.

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

static bool has_space_or_ctl(const char* url) {
  for (const char* c = url; *c; c++)
    if ((unsigned char)*c <= ' ' || *c == 0x7F)
      return true;
  return false;
}

static bool is_alpha_ci(char c) {
  return (c | 0x20) >= 'a' && (c | 0x20) <= 'z';
}

static bool is_scheme_char(char c) {
  return (c >= '0' && c <= '9') || is_alpha_ci(c) ||
         c == '+' || c == '-' || c == '.';
}

// NULL for any scheme but http
static const char* skip_scheme(const char* url) {
  // A scheme is the leading run of scheme characters before "://".
  const char* s = url;
  while (is_scheme_char(*s)) s++;
  if (s > url && is_alpha_ci(*url) &&
      s[0] == ':' && s[1] == '/' && s[2] == '/') {
    if (!is_http(url, (size_t)(s - url)))
      return NULL;
    return s + 3;
  }
  return url;
}

// NULL on userinfo or [IPv6]
static const char* find_host_end(const char* s, const char* netloc_end) {
  const char* host_end = netloc_end;
  for (const char* c = s; c < netloc_end; c++) {
    if (*c == '@' || *c == '[' || *c == ']')
      return NULL;
    if (*c == ':' && host_end == netloc_end)
      host_end = c;
  }
  return host_end;
}

static bool parse_port(const char* p, const char* e, int* port) {
  long v = 0;
  for (const char* c = p; c < e; c++) {
    if (*c < '0' || *c > '9')
      return false;
    v = v * 10 + (*c - '0');
    if (v > 65535)
      return false;
  }
  *port = (int)v;
  return true;
}

static void split_path_query(const char* netloc_end, const char* end, EGUrlParts* out) {
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
}

bool eg_parse_url(const char* url, EGUrlParts* out) {
  if (url == NULL || out == NULL)
    return false;

  memset(out, 0, sizeof(*out));
  out->port = 80;

  if (has_space_or_ctl(url))
    return false;

  const char* s = skip_scheme(url);
  if (s == NULL)
    return false;

  const char* end   = url + strlen(url);
  const char* frag  = strchr(s, '#');
  if (frag) end = frag;
  const char* netloc_end = s;
  while (netloc_end < end && *netloc_end != '/' && *netloc_end != '?') netloc_end++;

  const char* host_end = find_host_end(s, netloc_end);
  if (host_end == NULL || host_end == s)
    return false;

  // empty port keeps 80
  if (host_end < netloc_end && host_end + 1 < netloc_end) {
    if (!parse_port(host_end + 1, netloc_end, &out->port))
      return false;
  }

  out->host     = s;
  out->host_len = (size_t)(host_end - s);

  split_path_query(netloc_end, end, out);
  return true;
}
