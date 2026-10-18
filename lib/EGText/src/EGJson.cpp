/*
  EGJson.cpp - Write JSON into a fixed buffer; every key and string is escaped.

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
#include "EGJson.h"
#include "EGEscape.h"
#include "EGFormat.h"

#include <stdio.h>
#include <string.h>
#include <pgmspace.h>

EGJson::EGJson(char* buf, size_t cap, Sink sink, void* arg)
    : _buf(buf), _cap(cap), _sink(sink), _arg(arg) {
  if (_cap) _buf[0] = '\0';
}

void EGJson::flush() {
  if (!_sink || !_pos) return;
  _sink(_arg, _buf, _pos);
  _pos = 0;
  _mark = 0;
  _buf[0] = '\0';
}

// out with the whole fields, the one being written moves to the front
void EGJson::spill() {
  if (!_sink || !_mark) return;
  _sink(_arg, _buf, _mark);
  memmove(_buf, _buf + _mark, _pos - _mark);
  _pos -= _mark;
  _mark = 0;
}

void EGJson::map_begin(const char* key)   { open(key, '{'); }
void EGJson::map_end()                    { close('}'); }
void EGJson::array_begin(const char* key) { open(key, '['); }
void EGJson::array_end()                  { close(']'); }

void EGJson::str(const char* key, const char* s) {
  str_begin(key);
  part(s);
  str_end();
}

void EGJson::uint(const char* key, uint32_t v) {
  char t[12];
  int n = snprintf_P(t, sizeof(t), PSTR("%lu"), (unsigned long)v);
  _mark = _pos;
  name(key);
  put(t, (size_t)n);
  cut();
}

void EGJson::sint(const char* key, int32_t v) {
  char t[12];
  int n = snprintf_P(t, sizeof(t), PSTR("%ld"), (long)v);
  _mark = _pos;
  name(key);
  put(t, (size_t)n);
  cut();
}

void EGJson::fixed(const char* key, float v, uint8_t decimals) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  if ((bits & 0x7F800000u) == 0x7F800000u) return;   // NaN or infinity
  if (decimals < 1 || decimals > 9) return;
  char t[24];
  int n = format_f(t, sizeof(t), v, decimals);
  _mark = _pos;
  name(key);
  put(t, (size_t)n);
  cut();
}

void EGJson::str_begin(const char* key) {
  _mark = _pos;
  name(key);
  put("\"", 1);
}

void EGJson::part(const char* s) {
  if (overflow) return;
  size_t room = _cap - _depth - _pos;
  size_t n = egesc_json(_buf + _pos, room, s);
  if (n >= room && _sink && _mark) {
    spill();
    room = _cap - _depth - _pos;
    n = egesc_json(_buf + _pos, room, s);
  }
  if (n >= room) { overflow = true; return; }
  _pos += n;
}

void EGJson::str_end() {
  put("\"", 1);
  cut();
}

void EGJson::open(const char* key, char c) {
  _mark = _pos;
  name(key);
  _depth++;
  put(&c, 1);
  cut();
  if (overflow) { _depth--; _lost++; return; }
  _first = true;
}

void EGJson::close(char c) {
  if (_lost) { _lost--; return; }
  if (!_depth) return;
  _depth--;
  _buf[_pos++] = c;
  _buf[_pos] = '\0';
  _first = false;
}

void EGJson::name(const char* key) {
  if (!_first) put(",", 1);
  _first = false;
  if (!key) return;
  put("\"", 1);
  part(key);
  put("\":", 2);
}

void EGJson::put(const char* s, size_t n) {
  if (overflow) return;
  if (_pos + n + _depth >= _cap) spill();
  if (_pos + n + _depth >= _cap) { overflow = true; return; }
  memcpy(_buf + _pos, s, n);
  _pos += n;
}

void EGJson::cut() {
  if (overflow) _pos = _mark;
}
