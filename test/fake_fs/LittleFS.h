/*
  LittleFS.h - in-memory host fake of the LittleFS / File API.

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

#ifndef EG_FAKE_LITTLEFS_H
#define EG_FAKE_LITTLEFS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <map>
#include <set>
#include <string>

#include "WString.h"

class FakeFS;

class File {
 public:
  File() {}
  File(FakeFS* fs, const char* path, bool dir) : fs_(fs), path_(path), dir_(dir) {}

  explicit operator bool() const { return fs_ != nullptr; }
  bool isDirectory() const { return dir_; }
  void close() { fs_ = nullptr; }

  size_t size() const;
  int available() const { return (int)(size() - pos_); }
  size_t read(uint8_t* buf, size_t n);
  int read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
  size_t readBytes(char* buf, size_t n) { return read((uint8_t*)buf, n); }
  size_t readBytesUntil(char end, char* buf, size_t n) {
    size_t got = 0;
    for (int c; got < n && (c = read()) >= 0 && c != end; ) buf[got++] = (char)c;
    return got;
  }
  size_t write(const uint8_t* buf, size_t n);
  size_t println(const char* s) {
    size_t n = write((const uint8_t*)s, strlen(s));
    return n + write((const uint8_t*)"\r\n", 2);
  }

 private:
  FakeFS*     fs_ = nullptr;
  std::string path_;
  bool        dir_ = false;
  size_t      pos_ = 0;
};

class FakeFS {
 public:
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  size_t space = SIZE_MAX;
  bool   rename_fails = false;
  int    mount_fails = 0;
  int    mounts = 0;
  int    formats = 0;
  bool   format_fails = false;

  bool begin(bool = false) {
    mounts++;
    if (mount_fails > 0) { mount_fails--; return false; }
    return true;
  }
  void end() {}
  bool format() {
    formats++;
    if (format_fails) return false;
    files.clear();
    dirs.clear();
    return true;
  }
  bool exists(const char* p) { return files.count(p) || dirs.count(p); }
  bool mkdir(const char* p) { dirs.insert(p); return true; }
  bool remove(const char* p) { return files.erase(p) || dirs.erase(p); }

  // replaces an existing to, as lfs_rename
  bool rename(const char* from, const char* to) {
    auto it = files.find(from);
    if (rename_fails || it == files.end()) return false;
    std::string data = it->second;
    files.erase(it);
    files[to] = data;
    return true;
  }

  File open(const char* p, const char* mode) {
    if (dirs.count(p)) return File(this, p, true);
    if (mode[0] == 'w') files[p].clear();
    else if (mode[0] == 'a') files[p];   // created if missing
    else if (!files.count(p)) return File();
    return File(this, p, false);
  }
};

inline FakeFS LittleFS;

inline size_t File::size() const {
  if (!fs_ || dir_) return 0;
  auto it = fs_->files.find(path_);
  return it == fs_->files.end() ? 0 : it->second.size();
}

inline size_t File::read(uint8_t* buf, size_t n) {
  if (!fs_ || dir_) return 0;
  const std::string& d = fs_->files[path_];
  if (pos_ >= d.size()) return 0;
  if (n > d.size() - pos_) n = d.size() - pos_;
  memcpy(buf, d.data() + pos_, n);
  pos_ += n;
  return n;
}

inline size_t File::write(const uint8_t* buf, size_t n) {
  if (!fs_ || dir_) return 0;
  if (n > fs_->space) n = fs_->space;
  if (fs_->space != SIZE_MAX) fs_->space -= n;
  fs_->files[path_].append((const char*)buf, n);
  return n;
}

#endif
