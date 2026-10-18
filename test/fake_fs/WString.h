/*
  WString.h - host fake of the Arduino String, only as much as the
  filesystem callers under test use.

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

#ifndef EG_FAKE_WSTRING_H
#define EG_FAKE_WSTRING_H

#include <string>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  String operator+(const char* r) const { String o(*this); o.s_ += r ? r : ""; return o; }
  String operator+(const String& r) const { String o(*this); o.s_ += r.s_; return o; }

 private:
  std::string s_;
};

#endif
