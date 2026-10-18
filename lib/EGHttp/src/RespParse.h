/*
  RespParse.h - HTTP response line parsing for EGHttpClient, without String.

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
#ifndef EG_RESPPARSE_H
#define EG_RESPPARSE_H

#include <stddef.h>
#include <stdint.h>

// one line, no CRLF

// 100-599 or -1
int eg_http_status(const char* line, size_t n);

// in place, needs a NUL after n
bool eg_http_header(char* line, size_t n, char** name, char** value);

bool eg_http_chunk_size(const char* line, size_t n, uint32_t* out);

// request side
bool eg_http_token(const char* s, size_t n);
bool eg_http_text(const char* s, size_t n);

// -1 = not hex
int eg_http_hex(char c);

#endif
