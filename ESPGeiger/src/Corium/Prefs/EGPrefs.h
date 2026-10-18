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

// bytes per group file
#define EGPREFS_MAX_GROUP_SZ 1024

// string maxes, rows and buffers both use these. buffers +1
#define EGPREFS_MAX_VALUE 255   // longest string max_len (webhook url/key)
#define EGPREF_ID_MAX     17    // announce_interval
#define EG_NAME_MAX       32
#define EG_HOST_MAX       64
#define EG_IPV4_MAX       15
#define EG_HHMM_MAX       5
#define EG_U32_MAX_LEN    10    // 4294967295
#define EGPREF_PATTERN_MAX 127  // regex or enum options

// build macro as text, for defaults and maxlength
#define STR_(x) #x
#define STR(x)  STR_(x)

enum EGPrefType : uint8_t {
  EGP_BOOL,
  EGP_INT,
  EGP_UINT,
  EGP_FLOAT,
  EGP_STRING,
  EGP_LABEL,
  EGP_HEADER,
  EGP_ENUM,     // options in pattern, value = min_i + index
};

static constexpr uint16_t EGP_SENSITIVE = 1 << 0;
static constexpr uint16_t EGP_HIDDEN    = 1 << 1;
static constexpr uint16_t EGP_ADVANCED  = 1 << 2;
static constexpr uint16_t EGP_READONLY  = 1 << 3;
static constexpr uint16_t EGP_TIME      = 1 << 4;
static constexpr uint16_t EGP_SLIDER    = 1 << 5;
static constexpr uint16_t EGP_REQUIRED  = 1 << 6;
static constexpr uint16_t EGP_INLINE    = 1 << 7;
static constexpr uint16_t EGP_REBOOT    = 1 << 8;   // a change needs a restart
static constexpr uint16_t EGP_PIN       = 1 << 9;   // input GPIO, put checks PinSafety
static constexpr uint16_t EGP_PIN_OUT   = 1 << 10;  // output GPIO, input-only refused too

// no PSTR() in static init. default_val stays SRAM
#define EG_PSTR(name, val) static const char name[] PROGMEM = val

// tables must be PROGMEM
struct EGPref {
  const char* id;         // PROGMEM, max 17 chars
  const char* label;      // SRAM or PROGMEM
  const char* help;       // SRAM or PROGMEM
  const char* default_val;// SRAM
  const char* pattern;    // regex, SRAM or PROGMEM
  int32_t     min_i;
  int32_t     max_i;      // == min_i: no range
  uint8_t     max_len;    // 0 = unlimited
  EGPrefType  type;
  uint16_t    flags;
};

// flash is 32-bit loads only, copy the whole row
class EGPrefRows {
public:
  constexpr EGPrefRows(const EGPref* rows) : _rows(rows) {}
  EGPref row(size_t j) const {
    EGPref r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
  const char* id_P(size_t j) const {
    return (const char*)pgm_read_ptr(&_rows[j].id);
  }
  const char* default_val(size_t j) const {
    return (const char*)pgm_read_ptr(&_rows[j].default_val);
  }
private:
  const EGPref* _rows;
};

// narrow flash reads fault, use get()
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

// hidden rows still post. PROGMEM, ends with {}
struct EGPrefShow {
  const char* id;       // row id, nullptr = the whole group
  const char* when;     // "module.key", the form field it watches
  uint16_t    values;   // bit n = shown while it's n
};

// SYSTEM must stay 0
enum EGPrefCategory : uint8_t {
  EGP_CAT_SYSTEM = 0,
  EGP_CAT_INPUT  = 1,
  EGP_CAT_OUTPUT = 2,
  EGP_CAT_UPLOAD = 3,
  EGP_CAT_BACKUP = 4,
};

// groups must be PROGMEM
struct EGPrefGroup {
  const char* module_id;
  char        label[20];  // flash, _P or %s only
  EGFlash<uint16_t> version;  // bump on schema change
  EGPrefRows    prefs;
  EGFlash<size_t>   count;
  EGFlash<uint8_t>  category;
  const char*   enable_key;   // on/off badge, nullptr = none

  // no copy, byte reads of flash fault
  EGPrefGroup(const EGPrefGroup&) = delete;
  EGPrefGroup& operator=(const EGPrefGroup&) = delete;
};

// Shared keys
extern const char EG_K_BRIGHTNESS[];
extern const char EG_K_CLK[];
extern const char EG_K_CLKR[];
extern const char EG_K_DNS[];
extern const char EG_K_ENABLE[];
extern const char EG_K_FADE_SHIFT[];
extern const char EG_K_FBT[];
extern const char EG_K_FREQ[];
extern const char EG_K_GW[];
extern const char EG_K_INTERVAL[];
extern const char EG_K_IP[];
extern const char EG_K_LAT[];
extern const char EG_K_LON[];
extern const char EG_K_MODE[];
extern const char EG_K_PASSWORD[];
extern const char EG_K_PIN[];
extern const char EG_K_POLARITY[];
extern const char EG_K_PORT[];
extern const char EG_K_PULSE_MS[];
extern const char EG_K_QUIET_FROM[];
extern const char EG_K_QUIET_TO[];
extern const char EG_K_RATIO[];
extern const char EG_K_SCL[];
extern const char EG_K_SDA[];
extern const char EG_K_SECS[];
extern const char EG_K_SEND[];
extern const char EG_K_SERVER[];
extern const char EG_K_SN[];
extern const char EG_K_STATIC_IP[];
extern const char EG_K_TPL[];
extern const char EG_K_TYPE[];
extern const char EG_K_TZ[];
extern const char EG_K_USER[];
extern const char EG_K_VOLUME[];
extern const char EG_K_WEB_PASS[];

// Shared labels
extern const char EG_L_EN[];
extern const char EG_L_MOD[];
extern const char EG_L_INT[];
extern const char EG_L_PRT[];
extern const char EG_L_PWD[];
extern const char EG_L_PIN[];
extern const char EG_L_POL[];
extern const char EG_O_POL[];
extern const char EG_L_PUL[];
extern const char EG_H_PUL[];
extern const char EG_L_FAD[];
extern const char EG_O_FAD[];
extern const char EG_L_BRT[];
extern const char EG_L_VOL[];
extern const char EG_H_VOL[];
extern const char EG_L_QFR[];
extern const char EG_L_QTO[];
extern const char EG_H_QTO[];
extern const char EG_H_RBA[];
extern const char EG_P_IP[];
extern const char EG_P_HOST[];

// === LEGACY IMPORT (remove after v1.0.0) ===
// PROGMEM, {old, new} rows ending {}
struct EGLegacyAlias {
  char legacy_key[20];
  char new_key[18];
};

class EGLegacyRows {
public:
  constexpr EGLegacyRows(const EGLegacyAlias* rows = nullptr) : _rows(rows) {}
  explicit operator bool() const { return _rows != nullptr; }
  EGLegacyAlias row(size_t j) const {
    EGLegacyAlias r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
private:
  const EGLegacyAlias* _rows;
};
// === END LEGACY IMPORT ===

class EGPrefs {
public:
  static void begin();

  // module SRAM, key SRAM or PROGMEM
  static const char* getString(const char* module, const char* key);
  static int32_t     getInt   (const char* module, const char* key);
  static uint32_t    getUInt  (const char* module, const char* key);
  static bool        getBool  (const char* module, const char* key);
  static float       getFloat (const char* module, const char* key);

  static bool put(const char* module, const char* key, const char* value);
  // why the last put refused a pin (PROGMEM), else nullptr
  static const char* refused();
  static bool putInt(const char* module, const char* key, int32_t value);
  static bool putUInt(const char* module, const char* key, uint32_t value);
  static bool commit(bool fire_callbacks = true);
  static bool remove_group(const char* module);
  // keep_network keeps net + web_pass
  static void reset_all(bool keep_network = false);
  static void request_restart();  // from on_prefs_saved
  static bool restart_pending();  // the last commit needs one
  // enable_key unset, "0" or "-1" (no pin) = off, no key = on
  static bool group_enabled(const EGPrefGroup* g);

  static size_t              group_count();
  static const EGPrefGroup*  group_at(size_t idx);
  static class EGModule*     module_at(size_t idx);
  static bool                find_pref(const char* module, const char* key, EGPref* out);
  // old group of a moved key, in adopt order; nullptr past the end
  static const char*         moved_from(size_t i);
  // id SRAM or nullptr for the group; false = always shown
  static bool                show_rule(const EGPrefShow* rows, const char* id, EGPrefShow* out);
};

#endif
