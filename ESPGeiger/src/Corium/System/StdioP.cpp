/*
  StdioP.cpp - the esp8266 _P printfs without libc stdio

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
#ifdef ESP8266
#include <stdarg.h>
#include <stdio.h>
#include <ets_sys.h>

// libc keeps these with printf_P in one section, which pulls in the FILE stdio
// Define all three, else libc's section comes back and gives a multiple definition
extern "C" {
int sprintf_P(char* s, const char* f, ...) {  // libc api
  va_list a;
  va_start(a, f);
  int n = vsprintf(s, f, a);
  va_end(a);
  return n;
}

int snprintf_P(char* s, size_t n, const char* f, ...) {
  va_list a;
  va_start(a, f);
  int r = vsnprintf(s, n, f, a);
  va_end(a);
  return r;
}

int vsnprintf_P(char* s, size_t n, const char* f, va_list a) {
  return vsnprintf(s, n, f, a);
}

// lwip's diag lines go straight to the uart, as the core's stdout did
int printf(const char* f, ...) {
  char b[128];
  va_list a;
  va_start(a, f);
  int n = vsnprintf(b, sizeof(b), f, a);
  va_end(a);
  for (const char* c = b; *c; c++) ets_putc(*c);
  return n;
}
}
#endif
