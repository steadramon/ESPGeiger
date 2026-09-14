/*
  Print.h - host shim for native unit tests.

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

// Enough of Print for a derived class to reach its byte sink. No print().


#ifndef EG_TEST_PRINT_H
#define EG_TEST_PRINT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
public:
  virtual ~Print() {}

  virtual size_t write(uint8_t c) = 0;

  virtual size_t write(const uint8_t* buf, size_t len) {
    size_t n = 0;
    while (len--) {
      if (write(*buf++) != 1) break;
      n++;
    }
    return n;
  }

  size_t write(const char* s) {
    return s ? write((const uint8_t*)s, strlen(s)) : 0;
  }
  size_t write(const char* buf, size_t len) {
    return write((const uint8_t*)buf, len);
  }
};

#endif
