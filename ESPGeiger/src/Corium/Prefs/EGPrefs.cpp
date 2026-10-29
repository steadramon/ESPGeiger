/*
  EGPrefs.cpp - Schema-driven preferences runtime.

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

#include "EGPrefs.h"
#include "EGPrefsStorage.h"
#include "PrefPattern.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/StringUtil.h"
#ifndef EGPREFS_NO_LEGACY
#include <ArduinoJson.h>
#endif
#include <EGEscape.h>
#include <EGStr.h>
#include "Corium/System/Fs.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <new>  // std::nothrow
#ifdef ESP32
#include <esp_task_wdt.h>
#endif

#ifndef EGPREFS_MAX_GROUPS
#define EGPREFS_MAX_GROUPS EG_MAX_MODULES
#endif
static_assert(EGPREFS_MAX_GROUPS >= EG_MAX_MODULES, "every module needs a prefs group slot");

#define EGPREFS_NAMESPACE    "prefs"

// max 32 prefs a group (heap_mask)
struct GroupShadow {
  const EGPrefGroup* group;
  EGModule*          module;
  const char**       values;
  uint32_t           heap_mask;
  uint8_t            dirty;     // DIRTY, or HELD: commit_soon called back, write waits
  bool               stored;
  uint8_t            partial;   // a stored value didn't load, never write over it: row + 1, or MANY
  bool               wipe;      // reset_all, no file unless a put comes
};
static constexpr uint8_t DIRTY = 1;
static constexpr uint8_t HELD  = 2;
static constexpr uint8_t MANY  = 0xFF;   // more than one row, only a reset clears it

const char EG_K_DNS[]          PROGMEM = "dns";
const char EG_K_GW[]           PROGMEM = "gw";
const char EG_K_IP[]           PROGMEM = "ip";
const char EG_K_SCL[]          PROGMEM = "scl";
const char EG_K_SDA[]          PROGMEM = "sda";
const char EG_K_SERVER[]       PROGMEM = "server";
const char EG_K_SN[]           PROGMEM = "sn";
const char EG_K_STATIC_IP[]    PROGMEM = "static_ip";
const char EG_K_TZ[]           PROGMEM = "tz";
const char EG_K_WEB_PASS[]     PROGMEM = "web_pass";
const char EG_H_RBA[] PROGMEM = "Reboot to apply";
const char EG_P_HOST[] PROGMEM = "[A-Za-z0-9.:_\\-]+";

static const char FMT_LD[] PROGMEM = "%ld";
static const char FMT_LU[] PROGMEM = "%lu";

static GroupShadow    s_groups[EGPREFS_MAX_GROUPS];
static size_t         s_group_count = 0;
static bool           s_begun = false;
static bool           s_restart_pending = false;
static bool           s_reboot_put = false;   // an EGP_REBOOT row changed
static const char*    s_refused = nullptr;
static const char*    s_warned = nullptr;
static EGPrefsStorage s_storage;
static bool           s_waiting = false;   // commit_soon's write
static GroupShadow*   s_frozen = nullptr;  // the last commit left it out
static void         (*s_wake)(uint32_t) = nullptr;

static inline bool shadow_is_heap(const GroupShadow& gs, size_t j) {
  return (gs.heap_mask >> j) & 1u;
}

// default reuses the flash pointer, no heap
// no memory = old value kept, false
static bool shadow_set(GroupShadow& gs, size_t j, const char* val, size_t len) {
  const char* dv = gs.group->prefs.default_val(j);
  bool is_dv = dv && strlen(dv) == len && memcmp(dv, val, len) == 0;
  char* copy = nullptr;
  if (!is_dv) {
    copy = (char*)malloc(len + 1);
    if (!copy) return false;
    memcpy(copy, val, len);
    copy[len] = '\0';
  }
  if (shadow_is_heap(gs, j)) free((void*)gs.values[j]);
  gs.values[j] = is_dv ? dv : copy;
  if (copy) gs.heap_mask |=  (1u << j);
  else      gs.heap_mask &= ~(1u << j);
  return true;
}

static void shadow_reset(GroupShadow& gs, size_t j) {
  if (shadow_is_heap(gs, j)) free((void*)gs.values[j]);
  const char* dv = gs.group->prefs.default_val(j);
  gs.values[j] = dv ? dv : "";
  gs.heap_mask &= ~(1u << j);
}

namespace {

static GroupShadow* s_last_group = nullptr;  // one-entry cache

GroupShadow* find_group(const char* module_id) {
  if (!module_id) return nullptr;
  if (s_last_group && strcmp(s_last_group->group->module_id, module_id) == 0)
    return s_last_group;
  for (size_t i = 0; i < s_group_count; i++) {
    if (strcmp(s_groups[i].group->module_id, module_id) == 0) {
      s_last_group = &s_groups[i];
      return s_last_group;
    }
  }
  return nullptr;
}

int find_pref_index(const EGPrefGroup* g, const char* key) {
  if (!g || !key) return -1;
  for (size_t i = 0; i < g->count.get(); i++) {
    if (streq_PP(key, g->prefs.id_P(i))) return (int)i;
  }
  return -1;
}

const char* moved_to(const char* from);

// a moved key reads from its new group, as put
const char* find_value(const char* module, const char* key) {
  GroupShadow* gs = find_group(module);
  int idx = gs ? find_pref_index(gs->group, key) : -1;
  if (idx >= 0) return gs->values[idx];
  const char* to = moved_to(module);
  return to ? find_value(to, key) : nullptr;
}

bool string_ok(const EGPref* p, const char* value) {
  for (const char* v = value; *v; v++) {
    unsigned char c = (unsigned char)*v;
    if (c < 0x20 || c == 0x7f) return false;
  }
  return egesc_utf8_ok(value) && pref_pattern(p->pattern, value) == 1;
}

bool ranged(const EGPref* p) { return p->min_i != p->max_i; }

int copy_value(const char* value, char* out, size_t outsz) {
  size_t len = strlen(value);
  if (len >= outsz) return -1;
  memcpy(out, value, len + 1);
  return (int)len;
}

int valid_bool(const char* value, char* out, size_t outsz) {
  char c = value[0];
  bool v = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
  return snprintf_P(out, outsz, PSTR("%c"), v ? '1' : '0');
}

// refuse, don't clamp
int valid_int(const EGPref* p, const char* value, char* out, size_t outsz) {
  int32_t v;
  if (!parse_i32(value, &v)) return -1;
  if (ranged(p) && (v < p->min_i || v > p->max_i)) return -1;
  return snprintf_P(out, outsz, FMT_LD, (long)v);
}

// long is 64 bit on host
int valid_uint(const EGPref* p, const char* value, char* out, size_t outsz) {
  // strtoul takes "-1"
  if (strchr(value, '-')) return -1;
  char* end;
  errno = 0;
  unsigned long v = strtoul(value, &end, 10);
  if (*end != '\0' || end == value || errno == ERANGE || v > UINT32_MAX) return -1;
  // unsigned, no wrap above INT32_MAX
  if (ranged(p) && (v < (uint32_t)p->min_i || v > (uint32_t)p->max_i)) return -1;
  return snprintf_P(out, outsz, FMT_LU, v);
}

// kept as typed, no String(float)
int valid_float(const EGPref* p, const char* value, char* out, size_t outsz) {
  char* end;
  float v = parse_f(value, &end);
  if (*end != '\0' || end == value || !isfinite(v)) return -1;   // 40 digits is inf
  if (ranged(p) && (v < p->min_i || v > p->max_i)) return -1;
  return copy_value(value, out, outsz);
}

int valid_string(const EGPref* p, const char* value, char* out, size_t outsz) {
  if (p->max_len > 0 && strlen(value) > p->max_len) return -1;
  if (!string_ok(p, value)) return -1;
  // "" = off
  if ((p->flags & EGP_TIME) && *value && !parseTime(value).isValid) return -1;
  return copy_value(value, out, outsz);
}

// -1 = invalid
int validate(const EGPref* p, const char* value, char* out, size_t outsz) {
  if (!p || !value) return -1;
  switch (p->type) {
    case EGP_BOOL:   return valid_bool(value, out, outsz);
    case EGP_INT:    return valid_int(p, value, out, outsz);
    case EGP_UINT:
    case EGP_ENUM:   return valid_uint(p, value, out, outsz);   // enum as index
    case EGP_FLOAT:  return valid_float(p, value, out, outsz);
    case EGP_STRING: return valid_string(p, value, out, outsz);
    case EGP_LABEL:
    case EGP_HEADER: return -1;
  }
  return -1;
}

// same checks as put, a bad one gets the default, never an edge
bool stored_ok(const EGPref& p, const char* value, uint16_t vlen) {
  if (p.type == EGP_LABEL || p.type == EGP_HEADER) return true;
  if (vlen > EGPREFS_MAX_VALUE || memchr(value, '\0', vlen)) return false;
  char copy[EGPREFS_MAX_VALUE + 1];
  memcpy(copy, value, vlen);
  copy[vlen] = '\0';
  if (p.default_val && strcmp(copy, p.default_val) == 0) return true;   // "" defaults
  char out[EGPREFS_MAX_VALUE + 1];
  return validate(&p, copy, out, sizeof(out)) >= 0;
}

void not_loaded(GroupShadow& gs, size_t j) {
  gs.partial = gs.partial ? MANY : (uint8_t)(j + 1);
}

// own = the group's file, a refused one stays in it; a moved one isn't carried
void read_record(GroupShadow* gs, const char* key, uint8_t klen,
                 const char* value, uint16_t vlen, bool own) {
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    const char* pid = gs->group->prefs.id_P(j);
    if (strlen_P(pid) == klen && strncmp_P(key, pid, klen) == 0) {
      EGPref p = gs->group->prefs.row(j);
      if (!stored_ok(p, value, vlen)) {
        Log::console(PSTR("prefs: %s.%s refused, default kept"), gs->group->module_id, pid);
        if (own) not_loaded(*gs, j);
        return;
      }
      if (!shadow_set(*gs, j, value, vlen)) not_loaded(*gs, j);
      return;
    }
  }
}

void load_record_cb(void* ctx, const char* key, uint8_t klen,
                    const char* value, uint16_t vlen) {
  read_record((GroupShadow*)ctx, key, klen, value, vlen, true);
}

// file size: magic, then klen key vlen value per row
size_t group_bytes(const GroupShadow& gs) {
  size_t total = 4;
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    total += 1 + strlen_P(gs.group->prefs.id_P(j)) + 2 + strlen(gs.values[j]);
  }
  return total;
}

bool write_group(GroupShadow& gs) {
  if (gs.partial) return false;
  size_t total = group_bytes(gs);
  if (total > EGPREFS_MAX_GROUP_SZ) {
    Log::console(PSTR("prefs: group %s too large (%u)"), gs.group->module_id, (unsigned)total);
    return false;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) return false;
  size_t p = 0;
  buf[p++] = 'E'; buf[p++] = 'G'; buf[p++] = 'P'; buf[p++] = '1';
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    const char* key = gs.group->prefs.id_P(j);
    size_t klen = strlen_P(key);
    const char* value = gs.values[j];
    size_t vlen = strlen(value);
    buf[p++] = (uint8_t)klen;
    memcpy_P(&buf[p], key, klen); p += klen;
    wr_le16(&buf[p], (uint16_t)vlen); p += 2;
    memcpy(&buf[p], value, vlen); p += vlen;
  }
  bool ok = s_storage.writeGroup(gs.group->module_id, buf, total);
  free(buf);
  if (ok) gs.stored = true;
  return ok;
}

// moved keys, names kept. Taken once, first listed wins
#ifdef EGPREFS_MOVED
struct MovedKeys { const char* from; const char* to; };
static const MovedKeys MOVED[] PROGMEM = { EGPREFS_MOVED };
static const size_t MOVED_N = sizeof(MOVED) / sizeof(MOVED[0]);

const char* from_at(size_t i) { return (const char*)pgm_read_ptr(&MOVED[i].from); }
const char* to_at(size_t i) { return (const char*)pgm_read_ptr(&MOVED[i].to); }

const char* moved_to(const char* from) {
  for (size_t i = 0; i < MOVED_N; i++) {
    if (strcmp(from_at(i), from) == 0) return to_at(i);
  }
  return nullptr;
}

void moved_record_cb(void* ctx, const char* key, uint8_t klen,
                     const char* value, uint16_t vlen) {
  read_record((GroupShadow*)ctx, key, klen, value, vlen, false);
}

void adopt_moved() {
  for (size_t k = 0; k < MOVED_N; k++) {
    const char* to = to_at(k);
    GroupShadow* gs = find_group(to);
    if (!gs || s_storage.has(to)) continue;
    // last listed read first, so the first overwrites
    bool any = false;
    for (size_t i = MOVED_N; i-- > 0;) {
      if (strcmp(to_at(i), to) != 0 || !s_storage.has(from_at(i))) continue;
      s_storage.readGroup(from_at(i), moved_record_cb, gs);
      any = true;
    }
    if (any && write_group(*gs)) Log::console(PSTR("Prefs: keys moved to %s"), to);
  }
}

// on reset, else they come back next boot; a live group's file is reset_all's
void drop_moved() {
  for (size_t i = 0; i < MOVED_N; i++) {
    if (!find_group(from_at(i))) s_storage.removeGroup(from_at(i));
  }
}
#else
const char* moved_to(const char*) { return nullptr; }   // nothing moved
void adopt_moved() {}
void drop_moved() {}
#endif

// === LEGACY IMPORT (remove after v1.0.0) ===
#ifdef EGPREFS_NO_LEGACY
void import_legacy() {}   // new boards, nothing to import
#else
constexpr size_t LEGACY_PATHS = 4;

bool has_path(const char* const* list, size_t n, const char* path) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(list[i], path) == 0) return true;
  }
  return false;
}

void add_path(const char** list, size_t* n, const char* path) {
  if (!has_path(list, *n, path) && *n < LEGACY_PATHS) list[(*n)++] = path;
}

// stale once every group has its own file
void drop_stale_legacy() {
  const char* seen[LEGACY_PATHS] = {0};
  size_t n = 0;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module || !gs.module->legacy_aliases()) continue;
    const char* path = gs.module->legacy_file();
#ifdef EGPREFS_LEGACY_FILE
    if (!path) path = EGPREFS_LEGACY_FILE;
#else
    if (!path) continue;
#endif
    if (has_path(seen, n, path) || n == LEGACY_PATHS) continue;
    seen[n++] = path;
    if (Fs::remove(path)) {
      Log::console(PSTR("Prefs: removed stale legacy %s"), path);
    }
  }
}

// one-off migration
void import_legacy() {
  bool any_missing = false;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    if (s_groups[gi].module && s_groups[gi].module->legacy_aliases() &&
        !s_storage.has(s_groups[gi].group->module_id)) {
      any_missing = true; break;
    }
  }
  if (!any_missing) { drop_stale_legacy(); return; }

  DynamicJsonDocument doc(3072);
  const char* cached_file = nullptr;
  bool cached_ok = false;
  int imported = 0;
  const char* drop[LEGACY_PATHS] = {0};
  const char* keep[LEGACY_PATHS] = {0};   // a group's import didn't land
  size_t drop_n = 0, keep_n = 0;

  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module) continue;
    if (s_storage.has(gs.group->module_id)) continue;
    EGLegacyRows aliases = gs.module->legacy_aliases();
    if (!aliases) continue;

    const char* path = gs.module->legacy_file();
#ifdef EGPREFS_LEGACY_FILE
    if (!path) path = EGPREFS_LEGACY_FILE;
#else
    if (!path) continue;
#endif
    if (cached_file == nullptr || strcmp(cached_file, path) != 0) {
      doc.clear();
      cached_file = path;
      cached_ok = false;
      File f = Fs::open_r(path);
      if (!f) continue;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (err) {
        Log::console(PSTR("prefs: legacy parse error %s: %s"), path, err.c_str());
        continue;
      }
      cached_ok = true;
    }
    if (!cached_ok) continue;
    JsonObject root = doc.as<JsonObject>();

    bool any = false;
    for (size_t j = 0;; j++) {
      EGLegacyAlias a = aliases.row(j);
      if (!a.legacy_key[0]) break;
      const char* raw = root[a.legacy_key].as<const char*>();
      if (!raw) continue;
      int idx = find_pref_index(gs.group, a.new_key);
      if (idx < 0) continue;
      char normalized[EGPREFS_MAX_VALUE + 1];
      EGPref p = gs.group->prefs.row(idx);
      int nlen = validate(&p, raw, normalized, sizeof(normalized));
      if (nlen < 0) continue;
      if (!shadow_set(gs, idx, normalized, (size_t)nlen)) {
        not_loaded(gs, idx);   // legacy file kept
        continue;
      }
      any = true;
      imported++;
    }
    bool failed = any && !write_group(gs);
    if (failed) Log::console(PSTR("prefs: legacy persist failed for %s"), gs.group->module_id);
    if (failed || gs.partial) add_path(keep, &keep_n, path);
    else if (any) add_path(drop, &drop_n, path);
  }
  if (imported > 0) {
    Log::console(PSTR("Prefs: Imported %d legacy value(s)"), imported);
  }
  for (size_t i = 0; i < drop_n; i++) {
    if (has_path(keep, keep_n, drop[i])) continue;
    if (Fs::remove(drop[i])) {
      Log::console(PSTR("Prefs: removed legacy %s"), drop[i]);
    }
  }
}
#endif
// === END LEGACY IMPORT ===

static bool runs(EGModule* m) { return m && !EGModuleRegistry::quarantined(m); }

// defaults, then whatever's stored
void add_group(EGModule* m, const EGPrefGroup* g) {
  if (g->count.get() > 32) {
    Log::console(PSTR("prefs: %s has %u prefs, exceeds bitmap cap of 32 - skipped"),
                 g->module_id, (unsigned)g->count.get());
    return;
  }
  GroupShadow& gs = s_groups[s_group_count];
  gs.group = g;
  gs.module = m;
  gs.dirty = false;
  gs.heap_mask = 0;
  gs.values = new (std::nothrow) const char*[g->count.get()];
  if (!gs.values) {
    Log::console(PSTR("prefs: %s value alloc failed - skipped"), g->module_id);
    return;
  }
  s_group_count++;

  for (size_t j = 0; j < g->count.get(); j++) {
    const char* dv = g->prefs.default_val(j);
    gs.values[j] = dv ? dv : "";
  }
  gs.stored = s_storage.readGroup(g->module_id, load_record_cb, &gs);

  Log::debug(PSTR("prefs: %s (%u keys, v%u)"),
             g->module_id, (unsigned)g->count.get(), (unsigned)g->version.get());
}

} // anonymous namespace

void EGPrefs::begin() {
  if (s_begun) return;
  s_group_count = 0;

  if (!s_storage.begin(EGPREFS_NAMESPACE)) {
    Log::console(PSTR("Prefs: storage mount failed, using defaults"));
  }

  uint8_t n = EGModuleRegistry::count();
  for (uint8_t i = 0; i < n && s_group_count < EGPREFS_MAX_GROUPS; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    const EGPrefGroup* g = m ? m->prefs_group() : nullptr;
    if (g) add_group(m, g);
  }

  adopt_moved();    // current data before any legacy file
  import_legacy();  // LEGACY IMPORT (remove after v1.0.0)

  s_begun = true;
  Log::console(PSTR("Prefs: Initialised (%u groups)"), (unsigned)s_group_count);

  for (size_t i = 0; i < s_group_count; i++) {
    if (runs(s_groups[i].module)) s_groups[i].module->on_prefs_loaded();
  }
}

const char* EGPrefs::getString(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? v : "";
}

int32_t EGPrefs::getInt(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? (int32_t)strtol(v, nullptr, 10) : 0;
}

uint32_t EGPrefs::getUInt(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? (uint32_t)strtoul(v, nullptr, 10) : 0;
}

bool EGPrefs::getBool(const char* module, const char* key) {
  const char* v = find_value(module, key);
  if (!v || v[0] == '\0') return false;
  char c = v[0];
  return (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
}

float EGPrefs::getFloat(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? parse_f(v, nullptr) : 0.0f;
}

static PinSafety::Use pin_use(uint16_t flags) {
  if ((flags & EGP_PIN_BTN) == EGP_PIN_BTN) return PinSafety::USE_BUTTON;
  return flags & EGP_PIN_OUT ? PinSafety::USE_OUT : PinSafety::USE_IN;
}

static bool pin_refused(const EGPref& p, const char* module, const char* key, const char* v) {
  int32_t pin;
  if (!(p.flags & (EGP_PIN | EGP_PIN_OUT)) || !parse_i32(v, &pin)) return false;
  uint8_t needs = (p.flags & EGP_PIN_IRQ ? PinSafety::NEED_IRQ : 0) |
                  (p.flags & EGP_PIN_PWM ? PinSafety::NEED_PWM : 0) |
                  (p.flags & EGP_PIN_OD ? PinSafety::NEED_OD : 0);
  s_refused = PinSafety::refuse(pin, pin_use(p.flags), needs, &s_warned);
  if (!s_refused) return false;
  Log::console(PSTR("Prefs: %s.%s pin %s refused (%s)"), module, key, v, s_refused);
  return true;
}

bool EGPrefs::put(const char* module, const char* key, const char* value) {
  s_refused = nullptr;
  s_warned = nullptr;
  GroupShadow* gs = find_group(module);
  int idx = gs ? find_pref_index(gs->group, key) : -1;
  if (idx < 0) {
    // moved key, forward it
    const char* to = moved_to(module);
    return to && put(to, key, value);
  }
  const EGPref p = gs->group->prefs.row(idx);
  if (p.flags & EGP_READONLY) return false;

  char normalized[EGPREFS_MAX_VALUE + 1];
  int nlen = validate(&p, value, normalized, sizeof(normalized));
  if (nlen < 0) return false;

  if (strcmp(normalized, gs->values[idx]) != 0) {
    // changes only, a stored pin still loads
    if (pin_refused(p, module, key, normalized)) return false;
    // write_group would refuse it, and every commit after
    if (group_bytes(*gs) - strlen(gs->values[idx]) + nlen > EGPREFS_MAX_GROUP_SZ) {
      s_refused = PSTR("group full");
      return false;
    }
    if (!shadow_set(*gs, idx, normalized, (size_t)nlen)) {
      s_refused = PSTR("no memory");
      return false;
    }
    if (gs->partial == idx + 1) gs->partial = 0;   // its new value stands
    gs->dirty = true;
    gs->wipe = false;
    if (p.flags & EGP_REBOOT) s_reboot_put = true;
  }
  return true;
}

const char* EGPrefs::refused() { return s_refused; }
const char* EGPrefs::warned() { return s_refused ? nullptr : s_warned; }

bool EGPrefs::putInt(const char* module, const char* key, int32_t value) {
  char buf[EG_U32_MAX_LEN + 2];   // sign
  snprintf_P(buf, sizeof(buf), FMT_LD, (long)value);
  return put(module, key, buf);
}

bool EGPrefs::putUInt(const char* module, const char* key, uint32_t value) {
  char buf[EG_U32_MAX_LEN + 1];
  snprintf_P(buf, sizeof(buf), FMT_LU, (unsigned long)value);
  return put(module, key, buf);
}

// a wiped group is all defaults, so no file
static bool save_group(GroupShadow& gs) {
  const char* id = gs.group->module_id;
  if (gs.wipe) return !s_storage.has(id) || s_storage.removeGroup(id);
  return write_group(gs);
}

static int frozen_text(const GroupShadow& gs, char* out, size_t n) {
  const char* id = gs.group->module_id;
  if (gs.partial == MANY) return snprintf_P(out, n, PSTR("%s didn't load in full, reset it"), id);
  return snprintf_P(out, n, PSTR("%s.%s didn't load, set it or reset %s"),
                    id, gs.group->prefs.id_P(gs.partial - 1), id);
}

// its write would go over a value that didn't load: dropped, said once, never retried
static void drop_frozen(GroupShadow& gs) {
  char why[EGPREFS_FROZEN_MAX];
  frozen_text(gs, why, sizeof(why));
  Log::console(PSTR("Prefs: not saved, %s"), why);
  gs.dirty = false;
  s_frozen = &gs;
}

size_t EGPrefs::frozen(char* out, size_t n) {
  int len = s_frozen ? frozen_text(*s_frozen, out, n) : 0;
  return len > 0 ? (size_t)len : 0;
}

static bool any_dirty() {
  for (size_t i = 0; i < s_group_count; i++) {
    if (s_groups[i].dirty) return true;
  }
  return false;
}

// a failed group stays dirty, the next commit writes it
bool EGPrefs::commit(bool fire_callbacks) {
  s_restart_pending = s_reboot_put;
  s_frozen = nullptr;
  bool all_ok = true;
  bool wrote = false;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (!gs.dirty) continue;
    if (gs.partial) {
      drop_frozen(gs);
      continue;
    }
    // a write can take 200 ms, an import writes them all
    if (wrote) {
      yield();
#ifdef ESP32
      esp_task_wdt_reset();
#endif
    }
    wrote = true;
    if (!save_group(gs)) {
      all_ok = false;
      continue;
    }
    bool held = gs.dirty == HELD;
    gs.dirty = false;
    if (fire_callbacks && !held && runs(gs.module)) gs.module->on_prefs_saved();
  }
  if (all_ok) s_reboot_put = false;
  return all_ok && !s_frozen;
}

// callbacks now as commit's, the write waits for flush()
bool EGPrefs::commit_soon(uint32_t now) {
  if (!s_wake) return commit();
  s_restart_pending = s_reboot_put;
  s_frozen = nullptr;
  bool held = false;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (gs.dirty != DIRTY) continue;
    if (gs.partial) {
      drop_frozen(gs);
      continue;
    }
    gs.dirty = HELD;
    held = true;
    if (runs(gs.module)) gs.module->on_prefs_saved();
  }
  if (held && !s_waiting) {
    s_waiting = true;
    s_wake(now + EGPREFS_HOLD_MS);
  }
  return !s_frozen;
}

// a failed write waits again, a hold later; shutdown's flush tries it too
void EGPrefs::flush() {
  if (!s_waiting) return;
  s_waiting = false;
  if (commit() || !any_dirty()) return;   // a frozen one is dropped, not retried
  Log::console(PSTR("Prefs: not saved"));
  s_waiting = true;
  if (s_wake) s_wake(millis() + EGPREFS_HOLD_MS);
}

void EGPrefs::hold_by(void (*wake)(uint32_t at)) { s_wake = wake; }

bool EGPrefs::reset(const char* module, const char* key) {
  if (key) {
    EGPref p;
    return find_pref(module, key, &p) && put(module, key, p.default_val);
  }
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  bool ok = true;
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    EGPref p = gs->group->prefs.row(j);
    if (p.flags & EGP_READONLY) continue;
    ok = put(module, p.id, p.default_val) && ok;
  }
  // nothing stored is wanted, the defaults go over it
  if (gs->partial) gs->dirty = true;
  gs->partial = 0;
  return ok;
}

bool EGPrefs::remove_group(const char* module) {
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  bool removed = s_storage.removeGroup(module);
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    shadow_reset(*gs, j);
  }
  gs->dirty = false;
  gs->partial = 0;
  import_legacy();  // LEGACY IMPORT (remove after v1.0.0)
  if (runs(gs->module)) gs->module->on_prefs_loaded();
  return removed;
}

// EGP_KEEP rows always stay, EGP_LOCAL too with keep_network (an import never carries them)
void EGPrefs::reset_all(bool keep_network) {
  uint16_t keep = keep_network ? (EGP_KEEP | EGP_LOCAL) : EGP_KEEP;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    size_t n = gs.group->count.get();
    size_t kept = 0;
    for (size_t j = 0; j < n; j++) {
      if (gs.group->prefs.row(j).flags & keep) kept++;
    }
    if (kept && kept == n) continue;   // all kept, file as is
    for (size_t j = 0; j < n; j++) {
      if (!(gs.group->prefs.row(j).flags & keep)) shadow_reset(gs, j);
    }
    // files change at commit, till then the old ones stand
    gs.dirty = true;
    gs.wipe = !kept;
    if (!kept) gs.partial = 0;   // nothing stored is wanted
  }
  drop_moved();
}

bool EGPrefs::group_enabled(const EGPrefGroup* g) {
  if (!g->enable_key) return true;
  const char* ev = getString(g->module_id, g->enable_key);
  return ev[0] != '\0' && strcmp_P(ev, PSTR("0")) != 0 && strcmp_P(ev, PSTR("-1")) != 0;
}

void EGPrefs::request_restart() { s_restart_pending = true; }
bool EGPrefs::restart_pending() { return s_restart_pending; }

bool EGPrefs::stored(const char* module) {
  GroupShadow* gs = find_group(module);
  return gs && gs->stored;
}

size_t EGPrefs::group_count() {
  return s_group_count;
}

const EGPrefGroup* EGPrefs::group_at(size_t idx) {
  if (idx >= s_group_count) return nullptr;
  return s_groups[idx].group;
}

EGModule* EGPrefs::module_at(size_t idx) {
  if (idx >= s_group_count) return nullptr;
  return s_groups[idx].module;
}

bool EGPrefs::find_pref(const char* module, const char* key, EGPref* out) {
  GroupShadow* gs = find_group(module);
  int idx = gs ? find_pref_index(gs->group, key) : -1;
  if (idx < 0) {
    const char* to = moved_to(module);
    return to && find_pref(to, key, out);
  }
  if (out) *out = gs->group->prefs.row(idx);
  return true;
}

const char* EGPrefs::moved_from(size_t i) {
#ifdef EGPREFS_MOVED
  return i < MOVED_N ? from_at(i) : nullptr;
#else
  return nullptr;
#endif
}

bool EGPrefs::show_rule(const EGPrefShow* rows, const char* id, EGPrefShow* out) {
  if (!rows) return false;
  for (;; rows++) {
    memcpy_P(out, rows, sizeof(*out));
    if (!out->when) return false;
    if (!out->id && !id) return true;
    if (out->id && id && strcmp_P(id, out->id) == 0) return true;
  }
}
