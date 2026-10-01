/*
  EGPrefs.h - Schema-driven preferences for ESPGeiger modules.

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

#ifndef EGPREFS_H
#define EGPREFS_H

#include <Arduino.h>

enum EGPrefType : uint8_t {
  EGP_BOOL,
  EGP_INT,
  EGP_UINT,
  EGP_FLOAT,
  EGP_STRING,
  EGP_LABEL,
  EGP_HEADER,
  EGP_ENUM,     // <select>; options pipe-delimited in pattern, value = index 0..n-1
};

static constexpr uint8_t EGP_SENSITIVE = 1 << 0;
static constexpr uint8_t EGP_HIDDEN    = 1 << 1;
static constexpr uint8_t EGP_ADVANCED  = 1 << 2;
static constexpr uint8_t EGP_READONLY  = 1 << 3;
static constexpr uint8_t EGP_TIME      = 1 << 4;
static constexpr uint8_t EGP_SLIDER    = 1 << 5;
static constexpr uint8_t EGP_REQUIRED  = 1 << 6;
static constexpr uint8_t EGP_INLINE    = 1 << 7;

// Declare a PROGMEM pref string. label/help/pattern fields in EGPref tables
// can be set to one of these (PSTR() can't appear in static initializers).
// default_val must stay SRAM: getString returns it to callers that may do
// strcmp without _P.
#define EG_PSTR(name, val) static const char name[] PROGMEM = val

// Tables are PROGMEM: declare them `static const EGPref X[] PROGMEM`.
struct EGPref {
  char        id[18];     // in the row, so it lives in flash with it
  const char* label;      // SRAM or PROGMEM
  const char* help;       // SRAM or PROGMEM
  const char* default_val;// SRAM
  const char* pattern;    // regex for HTML5 validation, SRAM or PROGMEM, or nullptr
  int32_t     min_i;      // min range, or 0 if unconstrained
  int32_t     max_i;      // max range, same as min_i = unconstrained
  uint16_t    max_len;    // max string length, 0 = unlimited
  EGPrefType  type;
  uint8_t     flags;
};

// A PROGMEM table. Flash takes only aligned 32-bit loads, so a row is
// copied out whole rather than read in place.
class EGPrefRows {
public:
  constexpr EGPrefRows(const EGPref* rows) : _rows(rows) {}
  EGPref row(size_t j) const {
    EGPref r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
  const char* id_P(size_t j) const { return _rows[j].id; }
  const char* default_val(size_t j) const {
    return (const char*)pgm_read_ptr(&_rows[j].default_val);
  }
private:
  const EGPref* _rows;
};

// A non-pointer field of a PROGMEM struct. A narrow, or narrowed, read of
// flash faults, so the value is only reachable through get().
template <typename T>
class EGFlash {
public:
  constexpr EGFlash() : _v() {}
  constexpr EGFlash(T v) : _v(v) {}
  T get() const {
    T v;
    memcpy_P(&v, &_v, sizeof(v));
    return v;
  }
private:
  T _v;
};

// /param tab buckets. 0 = SYSTEM keeps existing literals safe (POD zero).
// BACKUP is rendered specially (no pref groups), kept here for symmetry.
enum EGPrefCategory : uint8_t {
  EGP_CAT_SYSTEM = 0,
  EGP_CAT_INPUT  = 1,
  EGP_CAT_OUTPUT = 2,
  EGP_CAT_UPLOAD = 3,
  EGP_CAT_BACKUP = 4,
};

// Groups are PROGMEM: declare them `static const EGPrefGroup X PROGMEM`.
struct EGPrefGroup {
  const char* module_id;
  char        label[20];  // in flash: _P functions or %s only
  EGFlash<uint16_t> version;  // bump to invalidate stored data on schema change
  EGPrefRows    prefs;
  EGFlash<size_t>   count;
  EGFlash<uint8_t>  category;  // EGPrefCategory; defaults SYSTEM when omitted
  // Optional: pref key whose value drives the group's on/off badge in the
  // config UI (off when unset or "0"). nullptr = no toggle, always shown plain.
  const char*   enable_key;

  // A copy could read flash a byte at a time.
  EGPrefGroup(const EGPrefGroup&) = delete;
  EGPrefGroup& operator=(const EGPrefGroup&) = delete;
};

// === LEGACY IMPORT (remove after v1.0.0) ===
// Sentinel-terminated list of {old_key_in_geigerconfig_json, new_key_in_group}
struct EGLegacyAlias {
  const char* legacy_key;
  const char* new_key;
};
// === END LEGACY IMPORT ===

class EGPrefs {
public:
  static void begin();

  static const char* getString(const char* module, const char* key);
  static int32_t     getInt   (const char* module, const char* key);
  static uint32_t    getUInt  (const char* module, const char* key);
  static bool        getBool  (const char* module, const char* key);
  static float       getFloat (const char* module, const char* key);

  static bool put(const char* module, const char* key, const char* value);
  static bool commit(bool fire_callbacks = true);
  static bool remove_group(const char* module);  // deletes stored file + resets shadow
  // Wipes every group's storage + shadows.
  // keep_network=true preserves the net group + sys.web_pass (device-local).
  static void reset_all(bool keep_network = false);
  static void request_restart();  // modules call this from on_prefs_saved if reboot needed
  static bool restart_pending();

  static size_t              group_count();
  static const EGPrefGroup*  group_at(size_t idx);
  static class EGModule*     module_at(size_t idx);  // for display_order() etc.
  static bool                find_pref(const char* module, const char* key, EGPref* out);
};

#endif
