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
};

static constexpr uint8_t EGP_SENSITIVE = 1 << 0;
static constexpr uint8_t EGP_HIDDEN    = 1 << 1;
static constexpr uint8_t EGP_ADVANCED  = 1 << 2;
static constexpr uint8_t EGP_READONLY  = 1 << 3;

struct EGPref {
  const char* id;         // 4
  const char* label;      // 4
  const char* help;       // 4
  const char* default_val;// 4
  const char* pattern;    // 4  (regex for HTML5 validation, or nullptr)
  int32_t     min_i;      // 4  (min range, or 0 if unconstrained)
  int32_t     max_i;      // 4  (max range, same as min_i = unconstrained)
  uint16_t    max_len;    // 2  (max string length, 0 = unlimited)
  EGPrefType  type;       // 1
  uint8_t     flags;      // 1
};                        // 32 bytes (was 40 - reordered to eliminate padding)

struct EGPrefGroup {
  const char* module_id;
  const char* label;
  uint16_t    version;   // bump to invalidate stored data on schema change
  const EGPref* prefs;
  size_t        count;
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
  static bool commit();
  static bool remove_group(const char* module);  // deletes stored file + resets shadow
  static void request_restart();  // modules call this from on_prefs_saved if reboot needed
  static bool restart_pending();

  static size_t              group_count();
  static const EGPrefGroup*  group_at(size_t idx);
  static class EGModule*     module_at(size_t idx);  // for display_order() etc.
  static const EGPref*       find_pref(const char* module, const char* key);
};

#endif
