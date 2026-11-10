/*
  Prefs.cpp - module preferences

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

#include "Prefs.h"
#include "PrefsStorage.h"
#include "PrefPattern.h"
#include "Corium/Module/Registry.h"
#include "Corium/Module/Module.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/StringUtil.h"
#ifndef CORIUM_PREFS_NO_LEGACY
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

#ifndef CORIUM_PREFS_MAX_GROUPS
#define CORIUM_PREFS_MAX_GROUPS CORIUM_MAX_MODULES
#endif
static_assert(CORIUM_PREFS_MAX_GROUPS >= CORIUM_MAX_MODULES, "every module needs a prefs group slot");

#define CORIUM_PREFS_NAMESPACE    "prefs"

// a group's live values in SRAM, files only change at commit
struct GroupShadow {
  const corium::PrefGroup* group;
  corium::Module*          module;
  const char**       values;    // per row: heap copy or the default's own pointer
  uint32_t           heap_mask; // bit j = values[j] is malloc'd
  uint8_t            dirty;     // DIRTY, or HELD: commitSoon called back, write waits
  bool               stored;
  uint8_t            partial;   // a stored value didn't load, never write over it: row + 1, or MANY
  bool               wipe;      // resetAll, no file unless a put comes
};
enum : uint8_t { CLEAN, DIRTY, HELD };
static constexpr size_t MAX_ROWS = sizeof(GroupShadow::heap_mask) * 8;
static constexpr uint8_t MANY  = 0xFF;   // more than one row, only a reset clears it

const char corium::K_DNS[]          PROGMEM = "dns";
const char corium::K_GW[]           PROGMEM = "gw";
const char corium::K_IP[]           PROGMEM = "ip";
const char corium::K_SCL[]          PROGMEM = "scl";
const char corium::K_SDA[]          PROGMEM = "sda";
const char corium::K_SERVER[]       PROGMEM = "server";
const char corium::K_SN[]           PROGMEM = "sn";
const char corium::K_STATIC_IP[]    PROGMEM = "static_ip";
const char corium::K_TZ[]           PROGMEM = "tz";
const char corium::K_WEB_PASS[]     PROGMEM = "web_pass";
const char corium::H_RBA[] PROGMEM = "Reboot to apply";
const char corium::P_HOST[] PROGMEM = "[A-Za-z0-9.:_\\-]+";

static const char FMT_LD[] PROGMEM = "%ld";
static const char FMT_LU[] PROGMEM = "%lu";

static GroupShadow    s_groups[CORIUM_PREFS_MAX_GROUPS];
static size_t         s_group_count = 0;
static bool           s_begun = false;
static bool           s_restart_pending = false;
static bool           s_reboot_put = false;   // a P_REBOOT row changed
static const char*    s_refused = nullptr;
static const char*    s_warned = nullptr;
static corium::PrefsStorage s_storage;
static bool           s_waiting = false;   // commitSoon's write
static GroupShadow*   s_frozen = nullptr;  // the last commit left it out
static void         (*s_wake)(uint32_t) = nullptr;
static uint8_t        s_fails = 0;   // flushes failed in a row
static constexpr uint8_t FAILS_MAX = 7;   // hold doubles, 64x tops

static inline bool shadow_is_heap(const GroupShadow& gs, size_t j) {
  return (gs.heap_mask >> j) & 1u;
}

// default reuses the flash pointer, no heap
// no memory = old value kept, false
static bool shadow_set(GroupShadow& gs, size_t j, const char* val, size_t len) {
  const char* dv = gs.group->prefs.defaultVal(j);
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
  const char* dv = gs.group->prefs.defaultVal(j);
  gs.values[j] = dv ? dv : "";
  gs.heap_mask &= ~(1u << j);
}

namespace {

static GroupShadow* s_last_group = nullptr;  // one-entry cache

GroupShadow* find_group(const char* id) {
  if (!id) return nullptr;
  if (s_last_group && strcmp(s_last_group->group->moduleId, id) == 0)
    return s_last_group;
  for (size_t i = 0; i < s_group_count; i++) {
    if (strcmp(s_groups[i].group->moduleId, id) == 0) {
      s_last_group = &s_groups[i];
      return s_last_group;
    }
  }
  return nullptr;
}

int find_pref_index(const corium::PrefGroup* g, const char* key) {
  if (!g || !key) return -1;
  for (size_t i = 0; i < g->count.get(); i++) {
    if (streq_PP(key, g->prefs.id_P(i))) return (int)i;
  }
  return -1;
}

const char* moved_to(const char* from);

// the group holding module.key, a moved one in its new group; module follows the move
GroupShadow* locate(const char*& module, const char* key, int& idx) {
  for (const char* m = module; m; m = moved_to(m)) {
    GroupShadow* gs = find_group(m);
    idx = gs ? find_pref_index(gs->group, key) : -1;
    if (idx >= 0) { module = m; return gs; }
  }
  return nullptr;
}

const char* find_value(const char* module, const char* key) {
  int idx;
  GroupShadow* gs = locate(module, key, idx);
  return gs ? gs->values[idx] : nullptr;
}

// no control chars, valid utf8, fits the pattern
bool string_ok(const corium::Pref* p, const char* value) {
  for (const char* v = value; *v; v++) {
    unsigned char c = (unsigned char)*v;
    if (c < 0x20 || c == 0x7f) return false;
  }
  return egesc_utf8_ok(value) && prefPattern(p->pattern, value) == 1;
}

bool ranged(const corium::Pref* p) { return p->minI != p->maxI; }

int copy_value(const char* value, char* out, size_t outsz) {
  size_t len = strlen(value);
  if (len >= outsz) return -1;
  memcpy(out, value, len + 1);
  return (int)len;
}

// 1, true, yes
bool truthy(char c) { return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y'; }

// any case, 0.12 saved Y and N
struct BoolWords { char on[5]; char off[6]; };
static const BoolWords BOOL_WORDS[] PROGMEM = {
  {"y", "n"}, {"yes", "no"}, {"true", "false"}, {"on", "off"},
};

// the rest refused, never read as off
int valid_bool(const char* value, char* out, size_t outsz) {
  // the form and the files
  if ((value[0] == '0' || value[0] == '1') && !value[1]) return copy_value(value, out, outsz);
  for (const BoolWords& w : BOOL_WORDS) {
    if (strcasecmp_P(value, w.on) == 0) return copy_value("1", out, outsz);
    if (strcasecmp_P(value, w.off) == 0) return copy_value("0", out, outsz);
  }
  return -1;
}

// refuse, don't clamp
int valid_int(const corium::Pref* p, const char* value, char* out, size_t outsz) {
  int32_t v;
  if (!parseI32(value, &v)) return -1;
  if (ranged(p) && (v < p->minI || v > p->maxI)) return -1;
  return snprintf_P(out, outsz, FMT_LD, (long)v);
}

// long is 64 bit on host
int valid_uint(const corium::Pref* p, const char* value, char* out, size_t outsz) {
  // strtoul takes "-1"
  if (strchr(value, '-')) return -1;
  char* end;
  errno = 0;
  unsigned long v = strtoul(value, &end, 10);
  if (*end != '\0' || end == value || errno == ERANGE || v > UINT32_MAX) return -1;
  // unsigned, no wrap above INT32_MAX
  if (ranged(p) && (v < (uint32_t)p->minI || v > (uint32_t)p->maxI)) return -1;
  return snprintf_P(out, outsz, FMT_LU, v);
}

static constexpr int JUNK = -2;   // never meant anything on this build

// a blank option is a number this build hasn't got
int enum_slot(const corium::Pref* p, const char* out, int len) {
  if (!p->pattern) return len;
  uint32_t skip = strtoul(out, nullptr, 10) - (uint32_t)p->minI;
  const char* opt = p->pattern;
  while (skip) {
    char c = (char)pgm_read_byte(opt++);
    if (!c) return -1;   // past the last
    if (c == '|') skip--;
  }
  char c = (char)pgm_read_byte(opt);
  return c && c != '|' ? len : JUNK;
}

// kept as typed, no String(float)
int valid_float(const corium::Pref* p, const char* value, char* out, size_t outsz) {
  char* end;
  float v = parseF(value, &end);
  if (*end != '\0' || end == value || !isfinite(v)) return -1;   // 40 digits is inf
  if (ranged(p) && (v < p->minI || v > p->maxI)) return -1;
  return copy_value(value, out, outsz);
}

int valid_string(const corium::Pref* p, const char* value, char* out, size_t outsz) {
  if (p->maxLen > 0 && strlen(value) > p->maxLen) return -1;
  if (!string_ok(p, value)) return -1;
  // "" = off
  if ((p->flags & corium::P_TIME) && *value && !parseTime(value).isValid) return -1;
  return copy_value(value, out, outsz);
}

// -1 = invalid, JUNK = a blank enum slot
int validate(const corium::Pref* p, const char* value, char* out, size_t outsz) {
  if (!p || !value) return -1;
  switch (p->type) {
    case corium::P_BOOL:   return valid_bool(value, out, outsz);
    case corium::P_INT:    return valid_int(p, value, out, outsz);
    case corium::P_UINT:
    case corium::P_ENUM: {   // enum as index
      int len = valid_uint(p, value, out, outsz);
      return len < 0 || p->type == corium::P_UINT ? len : enum_slot(p, out, len);
    }
    case corium::P_FLOAT:  return valid_float(p, value, out, outsz);
    case corium::P_STRING: return valid_string(p, value, out, outsz);
    case corium::P_LABEL:
    case corium::P_HEADER: return -1;
  }
  return -1;
}

// same checks as put, a bad one gets the default, never an edge; < 0 = refused
int check_stored(const corium::Pref& p, const char* value, uint16_t vlen) {
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) return 0;
  if (vlen > CORIUM_PREFS_MAX_VALUE || memchr(value, '\0', vlen)) return -1;
  char copy[CORIUM_PREFS_MAX_VALUE + 1];
  memcpy(copy, value, vlen);
  copy[vlen] = '\0';
  if (p.defaultVal && strcmp(copy, p.defaultVal) == 0) return 0;   // "" defaults
  char out[CORIUM_PREFS_MAX_VALUE + 1];
  int len = validate(&p, copy, out, sizeof(out));
  if (len != -1 || p.type == corium::P_STRING) return len;
  // text in a number row is junk, a number another build may want
  const char* v = copy;
  if (*v == '-' || *v == '+') v++;
  return (*v >= '0' && *v <= '9') || *v == '.' ? -1 : JUNK;
}

void not_loaded(GroupShadow& gs, size_t j) {
  gs.partial = gs.partial ? MANY : (uint8_t)(j + 1);
}

// own = the group's file, a refused one stays in it; a moved one isn't carried. true = a row took it
bool read_record(GroupShadow* gs, const char* key, uint8_t klen,
                 const char* value, uint16_t vlen, bool own) {
  // the rest of the file didn't read, never written over; an old moved file isn't
  if (!klen) {
    if (own) gs->partial = MANY;
    return false;
  }
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    const char* pid = gs->group->prefs.id_P(j);
    if (strlen_P(pid) == klen && strncmp_P(key, pid, klen) == 0) {
      corium::Pref p = gs->group->prefs.row(j);
      int got = check_stored(p, value, vlen);
      if (got < 0) {
        // junk isn't kept, the next save writes the default over it
        corium::Log::console(PSTR("prefs: %s.%s %s, default kept"), gs->group->moduleId, pid,
                             got == JUNK ? PSTR("junk") : PSTR("refused"));
        if (own && got != JUNK) not_loaded(*gs, j);
        return false;
      }
      if (shadow_set(*gs, j, value, vlen)) return true;
      not_loaded(*gs, j);
      return false;
    }
  }
  return false;
}

void load_record_cb(void* ctx, const char* key, uint8_t klen,
                    const char* value, uint16_t vlen) {
  read_record((GroupShadow*)ctx, key, klen, value, vlen, true);
}

// file size: magic, then klen key vlen value per row
size_t group_bytes(const GroupShadow& gs) {
  size_t total = corium::PREFS_MAGIC_LEN;
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    total += 1 + strlen_P(gs.group->prefs.id_P(j)) + 2 + strlen(gs.values[j]);
  }
  return total;
}

// stage = the .new only, commit puts it in place
CORIUM_NODISCARD bool write_group(GroupShadow& gs, bool stage = false) {
  if (gs.partial) return false;
  size_t total = group_bytes(gs);
  if (total > CORIUM_PREFS_MAX_GROUP_SZ) {
    corium::Log::console(PSTR("prefs: group %s too large (%u)"), gs.group->moduleId, (unsigned)total);
    return false;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) return false;
  corium::putMagic(buf);
  size_t pos = corium::PREFS_MAGIC_LEN;
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    const char* key = gs.group->prefs.id_P(j);
    size_t klen = strlen_P(key);
    const char* value = gs.values[j];
    size_t vlen = strlen(value);
    buf[pos++] = (uint8_t)klen;
    memcpy_P(&buf[pos], key, klen); pos += klen;
    wrLe16(&buf[pos], (uint16_t)vlen); pos += 2;
    memcpy(&buf[pos], value, vlen); pos += vlen;
  }
  const char* id = gs.group->moduleId;
  bool ok = stage ? s_storage.stageGroup(id, buf, total) : s_storage.writeGroup(id, buf, total);
  free(buf);
  if (ok && !stage) gs.stored = true;
  return ok;
}

// moved keys, names kept. Taken once, first listed wins
#ifdef CORIUM_PREFS_MOVED
struct MovedKeys { const char* from; const char* to; };
static const MovedKeys MOVED[] PROGMEM = { CORIUM_PREFS_MOVED };
static const size_t MOVED_N = sizeof(MOVED) / sizeof(MOVED[0]);

const char* from_at(size_t i) { return (const char*)pgm_read_ptr(&MOVED[i].from); }
const char* to_at(size_t i) { return (const char*)pgm_read_ptr(&MOVED[i].to); }

const char* moved_to(const char* from) {
  for (size_t i = 0; i < MOVED_N; i++) {
    if (strcmp(from_at(i), from) == 0) return to_at(i);
  }
  return nullptr;
}

struct Adopt { GroupShadow* gs; bool any; };   // any = a row landed

void moved_record_cb(void* ctx, const char* key, uint8_t klen,
                     const char* value, uint16_t vlen) {
  Adopt* a = (Adopt*)ctx;
  if (read_record(a->gs, key, klen, value, vlen, false)) a->any = true;
}

void adopt_moved() {
  for (size_t k = 0; k < MOVED_N; k++) {
    const char* to = to_at(k);
    GroupShadow* gs = find_group(to);
    if (!gs || s_storage.has(to)) continue;
    // last listed read first, so the first overwrites
    Adopt a = {gs, false};
    for (size_t i = MOVED_N; i-- > 0;) {
      if (strcmp(to_at(i), to) != 0 || !s_storage.has(from_at(i))) continue;
      s_storage.readGroup(from_at(i), moved_record_cb, &a);
    }
    if (a.any && write_group(*gs)) corium::Log::console(PSTR("Prefs: keys moved to %s"), to);
  }
}

// on reset, else they come back next boot; a live group's file is resetAll's
void drop_moved() {
  for (size_t i = 0; i < MOVED_N; i++) {
    if (find_group(from_at(i))) continue;
    // one that stays: a file for the new group stops adopt_moved
    if (!s_storage.removeGroup(from_at(i))) {
      GroupShadow* to = find_group(to_at(i));
      if (to) to->wipe = false;
    }
    s_storage.dropStaged(from_at(i));   // 0.13 staged too
  }
}
#else
const char* moved_to(const char*) { return nullptr; }   // nothing moved
void adopt_moved() {}
void drop_moved() {}
#endif

// legacy import
#ifdef CORIUM_PREFS_NO_LEGACY
void import_legacy() {}   // new boards, nothing to import
void drop_stale_legacy() {}
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

// stale once every group has its own file, or a reset wiped them
void drop_stale_legacy() {
  const char* seen[LEGACY_PATHS] = {0};
  size_t n = 0;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module || !gs.module->legacyAliases()) continue;
    const char* path = gs.module->legacyFile();
#ifdef CORIUM_PREFS_LEGACY_FILE
    if (!path) path = CORIUM_PREFS_LEGACY_FILE;
#else
    if (!path) continue;
#endif
    if (has_path(seen, n, path) || n == LEGACY_PATHS) continue;
    seen[n++] = path;
    if (corium::Fs::exists(path) && corium::Fs::remove(path)) {
      corium::Log::console(PSTR("Prefs: removed stale legacy %s"), path);
    }
  }
}

// one-off migration
void import_legacy() {
  bool any_missing = false;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    if (s_groups[gi].module && s_groups[gi].module->legacyAliases() &&
        !s_storage.has(s_groups[gi].group->moduleId)) {
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
    if (s_storage.has(gs.group->moduleId)) continue;
    corium::LegacyRows aliases = gs.module->legacyAliases();
    if (!aliases) continue;

    const char* path = gs.module->legacyFile();
#ifdef CORIUM_PREFS_LEGACY_FILE
    if (!path) path = CORIUM_PREFS_LEGACY_FILE;
#else
    if (!path) continue;
#endif
    if (cached_file == nullptr || strcmp(cached_file, path) != 0) {
      doc.clear();
      cached_file = path;
      cached_ok = false;
      File f = corium::Fs::openR(path);
      if (!f) continue;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (err) {
        corium::Log::console(PSTR("prefs: legacy parse error %s: %s"), path, err.c_str());
        continue;
      }
      cached_ok = true;
    }
    if (!cached_ok) continue;
    JsonObject root = doc.as<JsonObject>();

    bool any = false;
    for (size_t j = 0;; j++) {
      corium::LegacyAlias a = aliases.row(j);
      if (!a.legacyKey[0]) break;
      const char* raw = root[a.legacyKey].as<const char*>();
      if (!raw) continue;
      int idx = find_pref_index(gs.group, a.newKey);
      if (idx < 0) continue;
      char normalized[CORIUM_PREFS_MAX_VALUE + 1];
      corium::Pref p = gs.group->prefs.row(idx);
      int nlen = validate(&p, raw, normalized, sizeof(normalized));
      if (nlen < 0) {
        corium::Log::console(PSTR("prefs: legacy %s.%s refused"), gs.group->moduleId, a.newKey);
        continue;
      }
      if (!shadow_set(gs, idx, normalized, (size_t)nlen)) {
        not_loaded(gs, idx);   // legacy file kept
        continue;
      }
      any = true;
      imported++;
    }
    bool failed = any && !write_group(gs);
    if (failed) corium::Log::console(PSTR("prefs: legacy persist failed for %s"), gs.group->moduleId);
    if (failed || gs.partial) add_path(keep, &keep_n, path);
    else if (any) add_path(drop, &drop_n, path);
  }
  if (imported > 0) {
    corium::Log::console(PSTR("Prefs: Imported %d legacy value(s)"), imported);
  }
  for (size_t i = 0; i < drop_n; i++) {
    if (has_path(keep, keep_n, drop[i])) continue;
    if (corium::Fs::remove(drop[i])) {
      corium::Log::console(PSTR("Prefs: removed legacy %s"), drop[i]);
    }
  }
}
#endif

static bool runs(corium::Module* m) { return m && !corium::Registry::quarantined(m); }

// defaults, then whatever's stored
void load_group(GroupShadow& gs) {
  for (size_t j = 0; j < gs.group->count.get(); j++) shadow_reset(gs, j);
  gs.dirty = CLEAN;
  gs.wipe = false;
  gs.partial = 0;
  gs.stored = s_storage.readGroup(gs.group->moduleId, load_record_cb, &gs);
}

void add_group(corium::Module* m, const corium::PrefGroup* g) {
  if (g->count.get() > MAX_ROWS) {
    corium::Log::console(PSTR("prefs: %s has %u prefs, exceeds bitmap cap of 32 - skipped"),
                 g->moduleId, (unsigned)g->count.get());
    return;
  }
  GroupShadow& gs = s_groups[s_group_count];
  gs.group = g;
  gs.module = m;
  gs.heap_mask = 0;
  gs.values = new (std::nothrow) const char*[g->count.get()];
  if (!gs.values) {
    corium::Log::console(PSTR("prefs: %s value alloc failed - skipped"), g->moduleId);
    return;
  }
  s_group_count++;
  s_storage.dropStaged(g->moduleId);   // a cut mid commit, the old file stands
  load_group(gs);

  corium::Log::debug(PSTR("prefs: %s (%u keys, v%u)"),
             g->moduleId, (unsigned)g->count.get(), (unsigned)g->version.get());
}

} // anonymous namespace

void corium::Prefs::begin() {
  if (s_begun) return;
  s_group_count = 0;

  if (!s_storage.begin(CORIUM_PREFS_NAMESPACE)) {
    corium::Log::console(PSTR("Prefs: storage mount failed, using defaults"));
  }

  uint8_t n = corium::Registry::count();
  for (uint8_t i = 0; i < n && s_group_count < CORIUM_PREFS_MAX_GROUPS; i++) {
    corium::Module* m = corium::Registry::get(i);
    const corium::PrefGroup* g = m ? m->prefsGroup() : nullptr;
    if (g) add_group(m, g);
  }

  adopt_moved();    // current data before any legacy file
  import_legacy();

  s_begun = true;
  corium::Log::console(PSTR("Prefs: Initialised (%u groups)"), (unsigned)s_group_count);

  for (size_t i = 0; i < s_group_count; i++) {
    if (runs(s_groups[i].module)) s_groups[i].module->onPrefsLoaded();
  }
}

const char* corium::Prefs::getString(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? v : "";
}

int32_t corium::Prefs::getInt(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? (int32_t)strtol(v, nullptr, 10) : 0;
}

uint32_t corium::Prefs::getUInt(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? (uint32_t)strtoul(v, nullptr, 10) : 0;
}

bool corium::Prefs::getBool(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v && truthy(v[0]);
}

float corium::Prefs::getFloat(const char* module, const char* key) {
  const char* v = find_value(module, key);
  return v ? parseF(v, nullptr) : 0.0f;
}

static corium::PinSafety::Use pin_use(uint16_t flags) {
  if ((flags & corium::P_PIN_BTN) == corium::P_PIN_BTN) return corium::PinSafety::USE_BUTTON;
  return flags & corium::P_PIN_OUT ? corium::PinSafety::USE_OUT : corium::PinSafety::USE_IN;
}

// pin rows only, sets s_refused and s_warned
static bool pin_refused(const corium::Pref& p, const char* module, const char* key, const char* v) {
  int32_t pin;
  if (!(p.flags & (corium::P_PIN | corium::P_PIN_OUT)) || !parseI32(v, &pin)) return false;
  uint8_t needs = (p.flags & corium::P_PIN_IRQ ? corium::PinSafety::NEED_IRQ : 0) |
                  (p.flags & corium::P_PIN_PWM ? corium::PinSafety::NEED_PWM : 0) |
                  (p.flags & corium::P_PIN_OD ? corium::PinSafety::NEED_OD : 0);
  s_refused = corium::PinSafety::refuse(pin, pin_use(p.flags), needs, &s_warned);
  if (!s_refused) return false;
  corium::Log::console(PSTR("Prefs: %s.%s pin %s refused (%s)"), module, key, v, s_refused);
  return true;
}

// a stored shorter one still loads
static bool too_short(const corium::Pref& p, int nlen) {
  if (p.type != corium::P_STRING || nlen == 0 || nlen >= p.minI) return false;
  s_refused = PSTR("too short");
  return true;
}

CORIUM_NODISCARD static bool put_value(const char* module, const char* key, const char* value, bool kept) {
  s_refused = nullptr;
  s_warned = nullptr;
  int idx;
  GroupShadow* gs = locate(module, key, idx);
  if (!gs) return false;
  const corium::Pref p = gs->group->prefs.row(idx);
  if (p.flags & corium::P_READONLY) return false;

  char out[CORIUM_PREFS_MAX_VALUE + 1];
  int nlen = validate(&p, value, out, sizeof(out));
  if (nlen < 0) return false;

  // same value = nothing to do, still true
  if (strcmp(out, gs->values[idx]) != 0) {
    // changes only, a stored pin still loads
    if (pin_refused(p, module, key, out)) return false;
    if (!kept && too_short(p, nlen)) return false;
    // write_group would refuse it, and every commit after
    if (group_bytes(*gs) - strlen(gs->values[idx]) + nlen > CORIUM_PREFS_MAX_GROUP_SZ) {
      s_refused = PSTR("group full");
      return false;
    }
    if (!shadow_set(*gs, idx, out, (size_t)nlen)) {
      s_refused = PSTR("no memory");
      return false;
    }
    if (gs->partial == idx + 1) gs->partial = 0;   // its new value stands
    gs->dirty = DIRTY;
    gs->wipe = false;
    if (p.flags & corium::P_REBOOT) s_reboot_put = true;
  }
  return true;
}

bool corium::Prefs::put(const char* module, const char* key, const char* value) {
  return put_value(module, key, value, false);
}

bool corium::Prefs::putKept(const char* module, const char* key, const char* value) {
  return put_value(module, key, value, true);
}

const char* corium::Prefs::refused() { return s_refused; }
const char* corium::Prefs::warned() { return s_refused ? nullptr : s_warned; }

bool corium::Prefs::putInt(const char* module, const char* key, int32_t value) {
  char buf[CORIUM_U32_MAX_LEN + 2];   // sign
  snprintf_P(buf, sizeof(buf), FMT_LD, (long)value);
  return put(module, key, buf);
}

bool corium::Prefs::putUInt(const char* module, const char* key, uint32_t value) {
  char buf[CORIUM_U32_MAX_LEN + 1];
  snprintf_P(buf, sizeof(buf), FMT_LU, (unsigned long)value);
  return put(module, key, buf);
}

static size_t frozen_text(const GroupShadow& gs, char* out, size_t cap) {
  const char* id = gs.group->moduleId;
  int len;
  if (gs.partial == MANY) len = snprintf_P(out, cap, PSTR("%s didn't load in full, reset it"), id);
  else len = snprintf_P(out, cap, PSTR("%s.%s didn't load, set it or reset %s"),
                        id, gs.group->prefs.id_P(gs.partial - 1), id);
  return len > 0 ? (size_t)len : 0;
}

size_t corium::Prefs::frozen(char* out, size_t cap) {
  return s_frozen ? frozen_text(*s_frozen, out, cap) : 0;
}

// all local = no export or import touches it
static bool exported(const GroupShadow& gs) {
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    if (!(gs.group->prefs.row(j).flags & corium::P_LOCAL)) return true;
  }
  return false;
}

size_t corium::Prefs::anyFrozen(char* out, size_t cap) {
  for (size_t i = 0; i < s_group_count; i++) {
    if (s_groups[i].partial && exported(s_groups[i])) return frozen_text(s_groups[i], out, cap);
  }
  return 0;
}

// its write would go over a value that didn't load: dropped, said once, never retried
static void drop_frozen(GroupShadow& gs) {
  gs.dirty = CLEAN;
  s_frozen = &gs;
  char why[CORIUM_PREFS_FROZEN_MAX];
  corium::Prefs::frozen(why, sizeof(why));
  corium::Log::console(PSTR("Prefs: not saved, %s"), why);
}

static bool any_dirty() {
  for (size_t i = 0; i < s_group_count; i++) {
    if (s_groups[i].dirty != CLEAN) return true;
  }
  return false;
}

// a write can take 200 ms, an import writes them all
static void pause(bool& busy) {
  if (busy) {
    yield();
#ifdef ESP32
    esp_task_wdt_reset();
#endif
  }
  busy = true;
}

static void saved(GroupShadow& gs, bool fire_callbacks) {
  bool held = gs.dirty == HELD;
  gs.dirty = CLEAN;
  if (fire_callbacks && !held && runs(gs.module)) gs.module->onPrefsSaved();
}

// every file staged before any lands, a frozen one dropped either way
static bool stage_pass(bool& busy) {
  bool ok = true;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (gs.dirty == CLEAN || gs.wipe) continue;
    if (gs.partial) {
      drop_frozen(gs);
      continue;
    }
    if (!ok) continue;
    pause(busy);
    ok = write_group(gs, true);
  }
  return ok;
}

// all staged: each put in place; else every .new goes, flash as it was
static bool land_pass(bool ok, bool fire_callbacks, bool& busy) {
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (gs.dirty == CLEAN || gs.wipe) continue;
    const char* id = gs.group->moduleId;
    if (ok) {
      pause(busy);
      ok = s_storage.commitGroup(id);
    }
    if (!ok) {
      s_storage.dropStaged(id);
      continue;
    }
    gs.stored = true;
    saved(gs, fire_callbacks);
  }
  return ok;
}

// a wiped group is all defaults, so no file
static bool wipe_pass(bool fire_callbacks, bool& busy) {
  bool all_ok = true;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (gs.dirty == CLEAN || !gs.wipe) continue;
    const char* id = gs.group->moduleId;
    pause(busy);
    if (!s_storage.removeGroup(id)) {
      all_ok = false;
      continue;
    }
    saved(gs, fire_callbacks);
  }
  return all_ok;
}

// a failed commit stays dirty, the next one writes it; a wiped file goes once the rest landed
bool corium::Prefs::commit(bool fire_callbacks) {
  s_restart_pending = s_reboot_put;
  s_frozen = nullptr;
  bool busy = false;
  bool all_ok = land_pass(stage_pass(busy), fire_callbacks, busy) && wipe_pass(fire_callbacks, busy);
  if (all_ok) {
    s_reboot_put = false;
    s_fails = 0;
  } else {
    s_frozen = nullptr;   // a failed write says more
  }
  return all_ok && !s_frozen;
}

// callbacks now as commit's, the write waits for flush()
bool corium::Prefs::commitSoon(uint32_t now_ms) {
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
    if (runs(gs.module)) gs.module->onPrefsSaved();
  }
  if (held && !s_waiting) {
    s_waiting = true;
    s_wake(now_ms + CORIUM_PREFS_HOLD_MS);
  }
  return !s_frozen;
}

// a failed write waits again, longer each time, said once; shutdown's flush tries it too
void corium::Prefs::flush(uint32_t now_ms) {
  if (!s_waiting) return;
  s_waiting = false;
  if (commit() || !any_dirty()) return;   // a frozen one is dropped, not retried
  if (!s_fails) corium::Log::console(PSTR("Prefs: not saved"));
  if (s_fails < FAILS_MAX) s_fails++;
  s_waiting = true;
  if (s_wake) s_wake(now_ms + ((uint32_t)CORIUM_PREFS_HOLD_MS << (s_fails - 1)));
}

void corium::Prefs::holdBy(void (*wake)(uint32_t at)) { s_wake = wake; }

// readonly rows and headings keep theirs
static bool resets(const corium::Pref& p) {
  return !(p.flags & corium::P_READONLY) && p.type != corium::P_LABEL && p.type != corium::P_HEADER;
}

bool corium::Prefs::reset(const char* module, const char* key) {
  if (key) {
    corium::Pref p;
    return findPref(module, key, &p) && put(module, key, p.defaultVal);
  }
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  size_t n = gs->group->count.get();
  // a bad default refuses it all, before any row changes
  char out[CORIUM_PREFS_MAX_VALUE + 1];
  for (size_t j = 0; j < n; j++) {
    corium::Pref p = gs->group->prefs.row(j);
    if (resets(p) && validate(&p, p.defaultVal, out, sizeof(out)) < 0) return false;
  }
  for (size_t j = 0; j < n; j++) {
    corium::Pref p = gs->group->prefs.row(j);
    if (!resets(p) || strcmp(gs->values[j], p.defaultVal) == 0) continue;
    shadow_reset(*gs, j);
    gs->dirty = DIRTY;
    if (p.flags & corium::P_REBOOT) s_reboot_put = true;
  }
  // nothing stored is wanted, the defaults go over it
  if (gs->partial) gs->dirty = DIRTY;
  gs->partial = 0;
  return true;
}

bool corium::Prefs::removeGroup(const char* module) {
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  bool removed = s_storage.removeGroup(module);
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    shadow_reset(*gs, j);
  }
  gs->dirty = CLEAN;
  gs->partial = 0;
  import_legacy();
  if (runs(gs->module)) gs->module->onPrefsLoaded();
  return removed;
}

// P_KEEP rows always stay, P_LOCAL too with keep_network (an import never carries them)
void corium::Prefs::resetAll(bool keep_network) {
  uint16_t keep = keep_network ? (corium::P_KEEP | corium::P_LOCAL) : corium::P_KEEP;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    size_t n = gs.group->count.get();
    size_t kept = 0;
    for (size_t j = 0; j < n; j++) {
      if (gs.group->prefs.row(j).flags & keep) kept++;
    }
    if (kept && kept == n) continue;   // all kept, file as is
    for (size_t j = 0; j < n; j++) {
      if (gs.group->prefs.row(j).flags & keep) continue;
      shadow_reset(gs, j);
      if (gs.partial == j + 1) gs.partial = 0;   // not wanted now, as put
    }
    // files change at commit, till then the old ones stand
    gs.dirty = DIRTY;
    gs.wipe = !kept;
    if (!kept) gs.partial = 0;   // nothing stored is wanted
  }
  drop_moved();
  drop_stale_legacy();
}

void corium::Prefs::revert() {
  for (size_t i = 0; i < s_group_count; i++) {
    if (s_groups[i].dirty != CLEAN) load_group(s_groups[i]);
  }
}

bool corium::Prefs::groupEnabled(const corium::PrefGroup* g) {
  if (!g->enableKey) return true;
  const char* v = getString(g->moduleId, g->enableKey);
  return v[0] != '\0' && strcmp_P(v, PSTR("0")) != 0 && strcmp_P(v, PSTR("-1")) != 0;
}

bool corium::Prefs::restartPending() { return s_restart_pending; }

bool corium::Prefs::stored(const char* module) {
  GroupShadow* gs = find_group(module);
  return gs && gs->stored;
}

bool corium::Prefs::landed(const char* module) {
  GroupShadow* gs = find_group(module);
  return gs && gs->dirty == CLEAN && !gs->partial;
}

bool corium::Prefs::loaded(const char* module, const char* key) {
  int idx;
  GroupShadow* gs = locate(module, key, idx);
  return !gs || (gs->partial != MANY && gs->partial != idx + 1);
}

size_t corium::Prefs::groupCount() {
  return s_group_count;
}

const corium::PrefGroup* corium::Prefs::groupAt(size_t idx) {
  if (idx >= s_group_count) return nullptr;
  return s_groups[idx].group;
}

corium::Module* corium::Prefs::moduleAt(size_t idx) {
  if (idx >= s_group_count) return nullptr;
  return s_groups[idx].module;
}

bool corium::Prefs::findPref(const char* module, const char* key, corium::Pref* out) {
  int idx;
  GroupShadow* gs = locate(module, key, idx);
  if (gs && out) *out = gs->group->prefs.row(idx);
  return gs != nullptr;
}

const char* corium::Prefs::movedFrom(size_t i) {
#ifdef CORIUM_PREFS_MOVED
  return i < MOVED_N ? from_at(i) : nullptr;
#else
  return nullptr;
#endif
}

bool corium::Prefs::showRule(const corium::PrefShow* rows, const char* id, corium::PrefShow* out) {
  if (!rows) return false;
  for (;; rows++) {
    memcpy_P(out, rows, sizeof(*out));
    if (!out->when) return false;
    if (!out->id && !id) return true;
    if (out->id && id && strcmp_P(id, out->id) == 0) return true;
  }
}
