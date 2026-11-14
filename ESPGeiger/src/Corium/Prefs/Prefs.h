/*
  Prefs.h - module preferences

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

#ifndef CORIUM_PREFS_H
#define CORIUM_PREFS_H

#include <Arduino.h>
#include "App/AppConfig.h"
#include "Corium/Util/Attr.h"

// value bytes per group, keys not counted. a group's maxBytes can ask for more
#ifndef CORIUM_PREFS_MAX_GROUP_SZ
#define CORIUM_PREFS_MAX_GROUP_SZ 1024
#endif
// most a group may ask for, files are sized from it. static_assert a group's maxBytes against it
#ifndef CORIUM_PREFS_GROUP_SZ_MAX
#define CORIUM_PREFS_GROUP_SZ_MAX 4096
#endif

// commitSoon's write waits this, the ones meanwhile join it
#ifndef CORIUM_PREFS_HOLD_MS
#define CORIUM_PREFS_HOLD_MS 5000
#endif

// string maxes, rows and buffers both use these. buffers +1
#ifndef CORIUM_PREFS_MAX_VALUE
#define CORIUM_PREFS_MAX_VALUE 255   // longest value a row may carry, maxLen is 8 bit
#endif
#ifndef CORIUM_PREF_ID_MAX
#define CORIUM_PREF_ID_MAX     17    // longest pref or group id
#endif
#ifndef CORIUM_NAME_MAX
#define CORIUM_NAME_MAX       32
#endif
#ifndef CORIUM_HOST_MAX
#define CORIUM_HOST_MAX       64
#endif
#define CORIUM_IPV4_MAX       15
#define CORIUM_HHMM_MAX       5
#define CORIUM_U32_MAX_LEN    10    // 4294967295
#define CORIUM_PREFS_FROZEN_MAX (3 * CORIUM_PREF_ID_MAX + 32)   // frozen()'s text, with the nul
#define CORIUM_PREF_PATTERN_MAX 127  // regex or enum options

// build macro as text, for defaults and maxlength
#define STR_(x) #x
#define STR(x)  STR_(x)

namespace corium {

enum PrefType : uint8_t {
  P_BOOL,
  P_INT,
  P_UINT,
  P_FLOAT,
  P_STRING,
  P_LABEL,
  P_HEADER,
  P_ENUM,     // options in pattern, value = minI + index
};

static constexpr uint16_t P_SENSITIVE = 1 << 0;
static constexpr uint16_t P_HIDDEN    = 1 << 1;
static constexpr uint16_t P_ADVANCED  = 1 << 2;
static constexpr uint16_t P_READONLY  = 1 << 3;   // put refuses it, reset skips it
static constexpr uint16_t P_TIME      = 1 << 4;   // HH:MM string, "" = off
static constexpr uint16_t P_SLIDER    = 1 << 5;
static constexpr uint16_t P_REQUIRED  = 1 << 6;
static constexpr uint16_t P_INLINE    = 1 << 7;
static constexpr uint16_t P_REBOOT    = 1 << 8;   // a change needs a restart
static constexpr uint16_t P_PIN       = 1 << 9;   // input GPIO, put checks PinSafety
static constexpr uint16_t P_PIN_OUT   = 1 << 10;  // output GPIO, input-only refused too
static constexpr uint16_t P_PIN_BTN   = P_PIN | P_PIN_OUT;  // button: input, no strap note
static constexpr uint16_t P_LOCAL     = 1 << 11;  // this device's, no export, an import keeps it
static constexpr uint16_t P_KEEP      = 1 << 12;  // no resetAll clears it, only its own reset
static constexpr uint16_t P_PIN_IRQ   = 1 << 13;  // pin needs an interrupt, else a warning
static constexpr uint16_t P_PIN_PWM   = 1 << 14;  // pin needs PWM
static constexpr uint16_t P_PIN_OD    = 1 << 15;  // pin needs open-drain

// no PSTR() in static init. defaultVal stays SRAM
#define CORIUM_PSTR(name, val) static const char name[] PROGMEM = val

// tables must be PROGMEM
struct Pref {
  const char* id;         // PROGMEM, max CORIUM_PREF_ID_MAX
  const char* label;      // SRAM or PROGMEM
  const char* help;       // SRAM or PROGMEM
  const char* defaultVal;// SRAM
  const char* pattern;    // regex, SRAM or PROGMEM
  int32_t     minI;      // strings: shortest a new one may be, "" still ok
  int32_t     maxI;      // == minI: no range
  uint8_t     maxLen;    // 0 = unlimited
  PrefType  type;
  uint16_t    flags;
};

// flash is 32-bit loads only, copy the whole row
class PrefRows {
public:
  constexpr PrefRows(const Pref* rows) : _rows(rows) {}
  Pref row(size_t j) const {
    Pref r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
  const char* id_P(size_t j) const {
    return (const char*)pgm_read_ptr(&_rows[j].id);
  }
  const char* defaultVal(size_t j) const {
    return (const char*)pgm_read_ptr(&_rows[j].defaultVal);
  }
private:
  const Pref* _rows;
};

// narrow flash reads fault, use get()
template <typename T>
class Flash {
public:
  constexpr Flash() : _v() {}
  constexpr Flash(T v) : _v(v) {}
  T get() const {
    T v;
    memcpy_P(&v, &_v, sizeof(v));
    return v;
  }
private:
  T _v;
};

// hidden rows still post. PROGMEM, ends with {}
struct PrefShow {
  const char* id;       // row id, nullptr = the whole group
  const char* when;     // "module.key", the form field it watches
  uint16_t    values;   // bit n = shown while it's n
};

// SYSTEM must stay 0
enum PrefCategory : uint8_t {
  P_CAT_SYSTEM = 0,
  P_CAT_INPUT  = 1,
  P_CAT_OUTPUT = 2,
  P_CAT_UPLOAD = 3,
  P_CAT_BACKUP = 4,
};
static constexpr uint8_t P_CAT_TAB   = 0x0F;   // the tab, the rest are marks
static constexpr uint8_t P_CAT_BOARD = 0x80;   // this board's own calibration, an import from another keeps it

// groups must be PROGMEM
struct PrefGroup {
  const char* moduleId;
  char        label[20];  // flash, _P or %s only
  Flash<uint16_t> version;  // bump on schema change
  PrefRows    prefs;
  Flash<size_t>   count;
  Flash<uint8_t>  category;
  const char*   enableKey;   // on/off badge, nullptr = none
  Flash<uint16_t> maxBytes;  // its values, 0 = CORIUM_PREFS_MAX_GROUP_SZ

  // no copy, byte reads of flash fault
  PrefGroup(const PrefGroup&) = delete;
  PrefGroup& operator=(const PrefGroup&) = delete;
};

// Shared keys
extern const char K_DNS[];
extern const char K_ESPOTA[];
extern const char K_GW[];
extern const char K_IP[];
extern const char K_SCL[];
extern const char K_SDA[];
extern const char K_SERVER[];
extern const char K_SN[];
extern const char K_STATIC_IP[];
extern const char K_TZ[];
extern const char K_WEB_PASS[];

// Shared labels
extern const char H_RBA[];
extern const char P_HOST[];
extern const char WRITE_FAILED[];   // a failed commit that isn't frozen()
extern const char AUTH_HELP[];      // a 401's body, APP_AUTH_HELP sets it

// legacy import
// PROGMEM, {old, new} rows ending {}
struct LegacyAlias {
  char legacyKey[20];
  char newKey[CORIUM_PREF_ID_MAX + 1];
};

class LegacyRows {
public:
  constexpr LegacyRows(const LegacyAlias* rows = nullptr) : _rows(rows) {}
  explicit operator bool() const { return _rows != nullptr; }
  LegacyAlias row(size_t j) const {
    LegacyAlias r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
private:
  const LegacyAlias* _rows;
};

// all static, one store for every module's group
class Prefs {
public:
  // setup, once modules are registered; loads every group then onPrefsLoaded
  static void begin();

  // module SRAM, key SRAM or PROGMEM; unknown = "" or 0
  static const char* getString(const char* module, const char* key);
  static int32_t     getInt   (const char* module, const char* key);
  static uint32_t    getUInt  (const char* module, const char* key);
  static bool        getBool  (const char* module, const char* key);
  static float       getFloat (const char* module, const char* key);

  // put, commit: loop task only. a put frees that key's getString
  static bool put(const char* module, const char* key, const char* value);
  // a value saved before, the shortest rule doesn't apply
  CORIUM_NODISCARD static bool putKept(const char* module, const char* key, const char* value);
  // why the last put refused it (PROGMEM), else nullptr
  static const char* refused();
  // a pin the last put saved anyway, why it may not work (PROGMEM)
  static const char* warned();
  CORIUM_NODISCARD static bool putInt(const char* module, const char* key, int32_t value);
  CORIUM_NODISCARD static bool putUInt(const char* module, const char* key, uint32_t value);
  static bool commit(bool fire_callbacks = true);
  // for a write per request: callbacks now, one write CORIUM_PREFS_HOLD_MS on; true = will be written
  CORIUM_NODISCARD static bool commitSoon(uint32_t now_ms);
  // why the last commit left a group out (a stored value didn't load, rest written), else 0
  static size_t frozen(char* out, size_t cap);
  // any group an export carries holding one now, the same text; the export would leave it out
  static size_t anyFrozen(char* out, size_t cap);
  static void flush(uint32_t now_ms);   // the waiting write, at wake or shutdown; now as commitSoon's
  // wake(at) has flush() called then; none = commitSoon writes now
  static void holdBy(void (*wake)(uint32_t at));
  // back to the row default, a key by put() rules; no key = the whole group, all or none
  static bool reset(const char* module, const char* key = nullptr);
  CORIUM_NODISCARD static bool removeGroup(const char* module);
  // P_KEEP rows stay; keep_network keeps the P_LOCAL ones too (net, web_pass, radio)
  static void resetAll(bool keep_network = false);
  // groups a failed commit left unsaved back to what's on flash, no callbacks
  static void revert();
  static void revert(const char* module);   // that one group only
  static bool restartPending();  // a commit since boot changed a P_REBOOT row
  // read at begin() or written since; false = defaults (no file, unreadable, FS lost)
  static bool stored(const char* module);
  // flash has what it reads: nothing waits to be written, none refused at load
  static bool landed(const char* module);
  // false = its stored value didn't load, the default stands; two in a group = every row
  static bool loaded(const char* module, const char* key);
  // enableKey unset, "0" or "-1" (no pin) = off, no key = on
  static bool groupEnabled(const PrefGroup* g);

  static size_t              groupCount();
  static const PrefGroup*  groupAt(size_t idx);
  static class Module*     moduleAt(size_t idx);
  static bool                findPref(const char* module, const char* key, Pref* out);
  // old group of a moved key, in adopt order; nullptr past the end
  static const char*         movedFrom(size_t i);
  // id SRAM or nullptr for the group; false = always shown
  static bool                showRule(const PrefShow* rows, const char* id, PrefShow* out);
};

}  // namespace corium

#endif
