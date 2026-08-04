/*
  RespParse.cpp - HTTP response line parsing for EGHttpClient, without String.

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
#include "RespParse.h"

#include <string.h>

// RFC 9112, rejected never repaired

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static bool is_ows(char c) {
  return c == ' ' || c == '\t';
}

static bool has_ctl(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c < ' ' && c != '\t') || c == 0x7F)
      return true;
  }
  return false;
}

static bool is_tchar(char c) {
  if (is_digit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'))
    return true;
  return c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

int eg_http_hex(char c) {
  if (is_digit(c))
    return c - '0';
  char lower = (char)(c | 0x20);
  if (lower >= 'a' && lower <= 'f')
    return lower - 'a' + 10;
  return -1;
}

// status-line = HTTP-version SP 3DIGIT SP [ reason-phrase ]
int eg_http_status(const char* line, size_t n) {
  if (line == NULL || n < 12 || has_ctl(line, n))
    return -1;
  if (memcmp(line, "HTTP/1.", 7) != 0 || !is_digit(line[7]) || line[8] != ' ')
    return -1;
  if (!is_digit(line[9]) || !is_digit(line[10]) || !is_digit(line[11]))
    return -1;
  if (n > 12 && line[12] != ' ')
    return -1;
  int code = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');
  if (code < 100 || code > 599)
    return -1;
  return code;
}

// field-line = token ":" OWS field-value OWS
bool eg_http_header(char* line, size_t n, char** name, char** value) {
  if (line == NULL || name == NULL || value == NULL || has_ctl(line, n))
    return false;
  size_t colon = 0;
  while (colon < n && line[colon] != ':')
    colon++;
  if (colon == 0 || colon == n)
    return false;
  for (size_t i = 0; i < colon; i++)
    if (!is_tchar(line[i]))
      return false;
  size_t start = colon + 1;
  size_t end = n;
  while (start < end && is_ows(line[start]))
    start++;
  while (end > start && is_ows(line[end - 1]))
    end--;
  line[colon] = '\0';
  line[end] = '\0';
  *name = line;
  *value = line + start;
  return true;
}

// header name or method
bool eg_http_token(const char* s, size_t n) {
  if (s == NULL || n == 0)
    return false;
  for (size_t i = 0; i < n; i++)
    if (!is_tchar(s[i]))
      return false;
  return true;
}

// no CR or LF
bool eg_http_text(const char* s, size_t n) {
  return s != NULL && !has_ctl(s, n);
}

// chunk-size [ BWS ";" chunk-ext ], capped at 32 bits
bool eg_http_chunk_size(const char* line, size_t n, uint32_t* out) {
  if (line == NULL || out == NULL || has_ctl(line, n))
    return false;
  uint32_t size = 0;
  size_t i = 0;
  for (; i < n; i++) {
    int digit = eg_http_hex(line[i]);
    if (digit < 0)
      break;
    if (size > (UINT32_MAX >> 4))
      return false;
    size = (size << 4) | (uint32_t)digit;
  }
  if (i == 0)
    return false;
  while (i < n && is_ows(line[i]))
    i++;
  if (i < n && line[i] != ';')
    return false;
  *out = size;
  return true;
}
