/*
  EGJson.h - Write JSON into a fixed buffer; every key and string is escaped.

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
#pragma once

#include <stddef.h>
#include <stdint.h>

// may be PROGMEM, overflow stays valid; with a sink flush() sends the rest
class EGJson {
public:
  typedef void (*Sink)(void* arg, const char* s, size_t n);

  EGJson(char* buf, size_t cap, Sink sink = nullptr, void* arg = nullptr);
  void flush();

  void map_begin(const char* key = nullptr);
  void map_end();
  void array_begin(const char* key = nullptr);
  void array_end();

  void str(const char* key, const char* s);
  void uint(const char* key, uint32_t v);
  void sint(const char* key, int32_t v);
  void fixed(const char* key, float v, uint8_t decimals);   // nan / inf left out

  void str_begin(const char* key);
  void part(const char* s);
  void str_end();

  size_t length() const { return _pos; }
  bool overflow = false;

private:
  void open(const char* key, char c);
  void close(char c);
  void name(const char* key);
  void put(const char* s, size_t n);
  void cut();
  void spill();

  char*   _buf;
  size_t  _cap;
  Sink    _sink;
  void*   _arg;
  size_t  _pos = 0;
  size_t  _mark = 0;
  uint8_t _depth = 0;
  uint8_t _lost = 0;
  bool    _first = true;
};
