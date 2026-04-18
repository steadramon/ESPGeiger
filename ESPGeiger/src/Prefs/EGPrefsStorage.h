/*
  EGPrefsStorage.h - LittleFS-backed per-module binary store for EGPrefs.

  File format (EGP1): one file per module at /<namespace>/<module_id>.bin
    magic:   4 bytes 'E','G','P','1'
    record:  key_len:u8  key:<key_len>  val_len:u16le  val:<val_len>
    ...repeated to EOF.

  Copyright (C) 2025 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#ifndef EGPREFS_STORAGE_H
#define EGPREFS_STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>

class EGPrefsStorage {
  public:
    // Called for each decoded record during readGroup(). Pointers are NOT
    // null-terminated and are valid only for the duration of the call.
    typedef void (*ReadCb)(void* ctx,
                           const char* key, uint8_t key_len,
                           const char* val, uint16_t val_len);

    EGPrefsStorage();
    ~EGPrefsStorage();

    bool begin(const char* name, bool readOnly = false);
    void end();

    bool has(const char* module_id);
    bool readGroup(const char* module_id, ReadCb cb, void* ctx);
    bool writeGroup(const char* module_id, const void* buf, size_t len);
    bool removeGroup(const char* module_id);

  private:
    void ensure_mounted();  // idempotent remount (other code may end() LittleFS)
    String _path;
    bool   _started;
    bool   _readOnly;
};

#endif
