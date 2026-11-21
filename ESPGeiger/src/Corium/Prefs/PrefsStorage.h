/*
  PrefsStorage.h - prefs files on LittleFS, one per module

  File format (EGP1): one file per module at /<namespace>/<module_id>.bin
    magic:   4 bytes 'E','G','P','1'
    record:  key_len:u8  key:<key_len>  val_len:u16le  val:<val_len>
    ...repeated to EOF.
    last:    1 '~' 4 crc32le, over every byte before it. Older readers skip it
             as an unknown key; a file without it loads as before.

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
#include "Corium/Util/Attr.h"
#include "Corium/Util/Bytes.h"
#include "Prefs.h"

#define CORIUM_PS_STAGING_SUFFIX ".new"   // a write not landed yet, never read back

namespace corium {

static constexpr size_t PREFS_MAGIC_LEN = 4;
static constexpr size_t PREFS_MAX_ROWS = 32;
// Row head is key_len, key, val_len
static constexpr size_t PREFS_ROW_HEAD = 1 + CORIUM_PREF_ID_MAX + 2;
static constexpr size_t PREFS_TAIL_LEN = 1 + 1 + 2 + 4;
// Room for every row head, so a row added later still fits
static constexpr size_t PREFS_FILE_MAX =
  PREFS_MAGIC_LEN + CORIUM_PREFS_GROUP_SZ_MAX + PREFS_MAX_ROWS * PREFS_ROW_HEAD + PREFS_TAIL_LEN;
inline void putMagic(uint8_t* p) { p[0] = 'E'; p[1] = 'G'; p[2] = 'P'; p[3] = '1'; }
inline bool isMagic(const uint8_t* p) { return p[0] == 'E' && p[1] == 'G' && p[2] == 'P' && p[3] == '1'; }
inline bool isTail(const uint8_t* p) { return p[0] == 1 && p[1] == '~' && p[2] == 4 && p[3] == 0; }
// p holds len bytes of file, the tail is written after them
inline void putTail(uint8_t* p, size_t len) {
  uint8_t* t = p + len;
  t[0] = 1; t[1] = '~'; t[2] = 4; t[3] = 0;
  wrLe32(t + 4, crc32Update(0, p, len));
}

class PrefsStorage {
  public:
    // key and value aren't NUL terminated and are only valid during the call.
    // key_len 0 = the rest of the file was lost
    typedef void (*ReadCb)(void* ctx,
                           const char* key, uint8_t key_len,
                           const char* value, uint16_t val_len);

    PrefsStorage();
    ~PrefsStorage();

    // false = prefs run on defaults
    CORIUM_NODISCARD bool begin(const char* name);
    void end();

    bool has(const char* module_id);
    // false = no file, or it's bad or cut short. A file that fails its crc is set aside as .bad
    bool readGroup(const char* module_id, ReadCb cb, void* ctx);
    CORIUM_NODISCARD bool writeGroup(const char* module_id, const void* buf, size_t len);
    // writeGroup split in two steps, so many groups can land together
    CORIUM_NODISCARD bool stageGroup(const char* module_id, const void* buf, size_t len);
    CORIUM_NODISCARD bool commitGroup(const char* module_id);
    void dropStaged(const char* module_id);
    CORIUM_NODISCARD bool removeGroup(const char* module_id);   // true = no file now

    static constexpr size_t DIR_NAME_MAX = 15;

  private:
    char   _dir[DIR_NAME_MAX + 3];   // "/name/"
    bool   _started;
    bool   _aside;   // A file was set aside this boot
};

}  // namespace corium

#endif
