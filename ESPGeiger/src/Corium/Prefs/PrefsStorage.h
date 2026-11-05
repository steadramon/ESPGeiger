/*
  corium::PrefsStorage.h - LittleFS-backed per-module binary store for corium::Prefs.

  File format (EGP1): one file per module at /<namespace>/<module_id>.bin
    magic:   4 bytes 'E','G','P','1'
    record:  key_len:u8  key:<key_len>  val_len:u16le  val:<val_len>
    ...repeated to EOF.

  Copyright (C) 2025 @steadramon

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

#ifndef CORIUM_PREFS_STORAGE_H
#define CORIUM_PREFS_STORAGE_H

#include <Arduino.h>

namespace corium {

static constexpr size_t PREFS_MAGIC_LEN = 4;
inline void putMagic(uint8_t* p) { p[0] = 'E'; p[1] = 'G'; p[2] = 'P'; p[3] = '1'; }
inline bool isMagic(const uint8_t* p) { return p[0] == 'E' && p[1] == 'G' && p[2] == 'P' && p[3] == '1'; }

class PrefsStorage {
  public:
    // not NUL-terminated, valid during the call only; key_len 0 = a record didn't read, the rest lost
    typedef void (*ReadCb)(void* ctx,
                           const char* key, uint8_t key_len,
                           const char* value, uint16_t val_len);

    PrefsStorage();
    ~PrefsStorage();

    bool begin(const char* name);
    void end();

    bool has(const char* module_id);
    bool readGroup(const char* module_id, ReadCb cb, void* ctx);
    bool writeGroup(const char* module_id, const void* buf, size_t len);
    // writeGroup in two steps, many groups land together
    bool stageGroup(const char* module_id, const void* buf, size_t len);
    bool commitGroup(const char* module_id);
    void dropStaged(const char* module_id);
    bool removeGroup(const char* module_id);

    static constexpr size_t DIR_NAME_MAX = 15;

  private:
    char   _path[DIR_NAME_MAX + 3];   // "/name/"
    bool   _started;
};

}  // namespace corium

#endif
