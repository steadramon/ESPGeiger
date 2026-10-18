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
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Util/StringUtil.h"
#include <ArduinoJson.h>
#include <EGEscape.h>
#include <LittleFS.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <new>  // std::nothrow

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
  bool               dirty;
};

const char EG_K_BRIGHTNESS[]   PROGMEM = "brightness";
const char EG_K_CLK[]          PROGMEM = "clk";
const char EG_K_CLKR[]         PROGMEM = "clkr";
const char EG_K_DNS[]          PROGMEM = "dns";
const char EG_K_ENABLE[]       PROGMEM = "enable";
const char EG_K_FADE_SHIFT[]   PROGMEM = "fade_shift";
const char EG_K_FBT[]          PROGMEM = "fbt";
const char EG_K_FREQ[]         PROGMEM = "freq";
const char EG_K_GW[]           PROGMEM = "gw";
const char EG_K_INTERVAL[]     PROGMEM = "interval";
const char EG_K_IP[]           PROGMEM = "ip";
const char EG_K_LAT[]          PROGMEM = "lat";
const char EG_K_LON[]          PROGMEM = "lon";
const char EG_K_MODE[]         PROGMEM = "mode";
const char EG_K_PASSWORD[]     PROGMEM = "password";
const char EG_K_PIN[]          PROGMEM = "pin";
const char EG_K_POLARITY[]     PROGMEM = "polarity";
const char EG_K_PORT[]         PROGMEM = "port";
const char EG_K_PULSE_MS[]     PROGMEM = "pulse_ms";
const char EG_K_QUIET_FROM[]   PROGMEM = "quiet_from";
const char EG_K_QUIET_TO[]     PROGMEM = "quiet_to";
const char EG_K_RATIO[]        PROGMEM = "ratio";
const char EG_K_SCL[]          PROGMEM = "scl";
const char EG_K_SDA[]          PROGMEM = "sda";
const char EG_K_SECS[]         PROGMEM = "secs";
const char EG_K_SEND[]         PROGMEM = "send";
const char EG_K_SERVER[]       PROGMEM = "server";
const char EG_K_SN[]           PROGMEM = "sn";
const char EG_K_STATIC_IP[]    PROGMEM = "static_ip";
const char EG_K_TPL[]          PROGMEM = "tpl";
const char EG_K_TYPE[]         PROGMEM = "type";
const char EG_K_TZ[]           PROGMEM = "tz";
const char EG_K_USER[]         PROGMEM = "user";
const char EG_K_VOLUME[]       PROGMEM = "volume";
const char EG_K_WEB_PASS[]     PROGMEM = "web_pass";
const char EG_L_EN[]  PROGMEM = "Enable";
const char EG_L_MOD[] PROGMEM = "Mode";
const char EG_L_INT[] PROGMEM = "Interval";
const char EG_L_PRT[] PROGMEM = "Port";
const char EG_L_PWD[] PROGMEM = "Password";
const char EG_L_PIN[] PROGMEM = "GPIO";
const char EG_L_POL[] PROGMEM = "Polarity";
const char EG_O_POL[] PROGMEM = "Active high|Active low";
const char EG_L_PUL[] PROGMEM = "Pulse width ms";
const char EG_H_PUL[] PROGMEM = "1-50";
const char EG_L_FAD[] PROGMEM = "Fade rate";
const char EG_O_FAD[] PROGMEM = "Fast|Medium|Slow";
const char EG_L_BRT[] PROGMEM = "Brightness";
const char EG_L_VOL[] PROGMEM = "Volume";
const char EG_H_VOL[] PROGMEM = "0-100";
const char EG_L_QFR[] PROGMEM = "Quiet from";
const char EG_L_QTO[] PROGMEM = "Quiet to";
const char EG_H_QTO[] PROGMEM = "End of quiet window; crosses midnight if from > to";
const char EG_H_RBA[] PROGMEM = "Reboot to apply";
const char EG_P_IP[]  PROGMEM = "[0-9.]+";
const char EG_P_HOST[] PROGMEM = "[A-Za-z0-9.:_\\-]+";

static const char FMT_LD[] PROGMEM = "%ld";
static const char FMT_LU[] PROGMEM = "%lu";

static GroupShadow    s_groups[EGPREFS_MAX_GROUPS];
static size_t         s_group_count = 0;
static bool           s_begun = false;
static bool           s_restart_pending = false;
static bool           s_reboot_put = false;   // an EGP_REBOOT row changed
static const char*    s_refused = nullptr;
static EGPrefsStorage s_storage;

static inline bool shadow_is_heap(const GroupShadow& gs, size_t j) {
  return (gs.heap_mask >> j) & 1u;
}

// default reuses the flash pointer, no heap
static void shadow_set(GroupShadow& gs, size_t j, const char* val, size_t len) {
  if (shadow_is_heap(gs, j)) free((void*)gs.values[j]);
  const char* dv = gs.group->prefs.default_val(j);
  if (dv && strlen(dv) == len && memcmp(dv, val, len) == 0) {
    gs.values[j] = dv;
    gs.heap_mask &= ~(1u << j);
    return;
  }
  char* copy = (char*)malloc(len + 1);
  if (copy) { memcpy(copy, val, len); copy[len] = '\0'; }
  gs.values[j] = copy ? copy : "";
  if (copy) gs.heap_mask |=  (1u << j);
  else      gs.heap_mask &= ~(1u << j);
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

// SRAM or PROGMEM
bool same_key(const char* a, const char* b) {
  if (a == b) return true;
  for (;; a++, b++) {
    uint8_t c = pgm_read_byte(a);
    if (c != pgm_read_byte(b)) return false;
    if (!c) return true;
  }
}

int find_pref_index(const EGPrefGroup* g, const char* key) {
  if (!g || !key) return -1;
  for (size_t i = 0; i < g->count.get(); i++) {
    if (same_key(key, g->prefs.id_P(i))) return (int)i;
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

inline char pat(const char* p, size_t i) { return (char)pgm_read_byte(p + i); }

bool in_class(const char* p, size_t from, size_t to, char c) {
  for (size_t i = from; i < to; i++) {
    char lo = pat(p, i);
    if (lo == '\\' && i + 1 < to) lo = pat(p, ++i);
    char hi = lo;
    if (i + 2 < to && pat(p, i + 1) == '-') {
      i += 2;
      hi = pat(p, i);
      if (hi == '\\' && i + 1 < to) hi = pat(p, ++i);
    }
    if (c >= lo && c <= hi) return true;
  }
  return false;
}

// index of the ], or of the NUL if there isn't one
size_t class_end(const char* p) {
  size_t i = 1;
  for (; pat(p, i) && pat(p, i) != ']'; i++) {
    if (pat(p, i) == '\\' && pat(p, i + 1)) i++;
  }
  return i;
}

unsigned read_num(const char* p, size_t* q) {
  unsigned n = 0;
  for (; pat(p, *q) >= '0' && pat(p, *q) <= '9'; (*q)++) n = n * 10 + (pat(p, *q) - '0');
  return n;
}

// + * {m} {m,} {m,n} or nothing, then the end
bool parse_count(const char* p, size_t q, unsigned* min, unsigned* max) {
  char c = pat(p, q);
  *min = *max = 1;
  if (c == '+' || c == '*') {
    *min = c == '+';
    *max = ~0u;
    q++;
  } else if (c == '{') {
    q++;
    *min = *max = read_num(p, &q);
    if (pat(p, q) == ',') {
      q++;
      *max = pat(p, q) == '}' ? ~0u : read_num(p, &q);
    }
    if (pat(p, q) != '}') return false;
    q++;
  }
  return pat(p, q) == '\0';
}

// only [..]+ [..]* [..]{m,n} \d{m,n}, rest left to the browser
bool pattern_ok(const char* p, const char* value) {
  if (!p || !value[0]) return true;
  bool digits = pat(p, 0) == '\\' && pat(p, 1) == 'd';
  size_t to = 0, q = 2;
  if (!digits) {
    if (pat(p, 0) != '[') return true;
    to = class_end(p);
    if (pat(p, to) != ']') return true;
    q = to + 1;
  }
  unsigned min, max;
  if (!parse_count(p, q, &min, &max)) return true;

  unsigned n = 0;
  for (const char* v = value; *v; v++, n++) {
    bool ok = digits ? (*v >= '0' && *v <= '9') : in_class(p, 1, to, *v);
    if (!ok) return false;
  }
  return n >= min && n <= max;
}

bool string_ok(const EGPref* p, const char* value) {
  for (const char* v = value; *v; v++) {
    unsigned char c = (unsigned char)*v;
    if (c < 0x20 || c == 0x7f) return false;
  }
  return egesc_utf8_ok(value) && pattern_ok(p->pattern, value);
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
  if (*end != '\0' || end == value) return -1;
  if (ranged(p) && (v < p->min_i || v > p->max_i)) return -1;
  return copy_value(value, out, outsz);
}

int valid_string(const EGPref* p, const char* value, char* out, size_t outsz) {
  if (p->max_len > 0 && strlen(value) > p->max_len) return -1;
  if (!string_ok(p, value)) return -1;
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

void load_record_cb(void* ctx, const char* key, uint8_t klen,
                    const char* value, uint16_t vlen) {
  GroupShadow* gs = (GroupShadow*)ctx;
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    const char* pid = gs->group->prefs.id_P(j);
    if (strlen_P(pid) == klen && strncmp_P(key, pid, klen) == 0) {
      EGPref p = gs->group->prefs.row(j);
      if (!stored_ok(p, value, vlen)) {
        Log::console(PSTR("prefs: %s.%s refused, default kept"), gs->group->module_id, pid);
        return;
      }
      shadow_set(*gs, j, value, vlen);
      return;
    }
  }
}

bool write_group(GroupShadow& gs) {
  size_t total = 4;
  for (size_t j = 0; j < gs.group->count.get(); j++) {
    total += 1 + strlen_P(gs.group->prefs.id_P(j)) + 2 + strlen(gs.values[j]);
  }
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
    buf[p++] = (uint8_t)(vlen & 0xFF);
    buf[p++] = (uint8_t)((vlen >> 8) & 0xFF);
    memcpy(&buf[p], value, vlen); p += vlen;
  }
  bool ok = s_storage.writeGroup(gs.group->module_id, buf, total);
  free(buf);
  return ok;
}

// moved keys, names kept. Taken once, first listed wins
struct MovedKeys { const char* from; const char* to; };
static const MovedKeys MOVED[] = {
  {"display", "i2c"},   // OLED builds
  {"env",     "i2c"},
  {"input",   "counter"},
  {"sys",     "counter"},
};
static const size_t MOVED_N = sizeof(MOVED) / sizeof(MOVED[0]);

const char* moved_to(const char* from) {
  for (const MovedKeys& m : MOVED) {
    if (strcmp(m.from, from) == 0) return m.to;
  }
  return nullptr;
}

void adopt_moved() {
  for (const MovedKeys& m : MOVED) {
    GroupShadow* gs = find_group(m.to);
    if (!gs || s_storage.has(m.to)) continue;
    // last listed read first, so the first overwrites
    bool any = false;
    for (size_t i = MOVED_N; i-- > 0;) {
      if (strcmp(MOVED[i].to, m.to) != 0 || !s_storage.has(MOVED[i].from)) continue;
      s_storage.readGroup(MOVED[i].from, load_record_cb, gs);
      any = true;
    }
    if (any && write_group(*gs)) Log::console(PSTR("Prefs: keys moved to %s"), m.to);
  }
}

// on reset, else they come back next boot
void drop_moved() {
  for (const MovedKeys& m : MOVED) s_storage.removeGroup(m.from);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
// stale once every group has its own file
void drop_stale_legacy() {
  const char* seen[4] = {0};
  size_t n = 0;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module || !gs.module->legacy_aliases()) continue;
    const char* path = gs.module->legacy_file();
    if (!path) path = "/geigerconfig.json";
    bool dup = false;
    for (size_t i = 0; i < n; i++) dup = dup || strcmp(seen[i], path) == 0;
    if (dup || n == sizeof(seen) / sizeof(seen[0])) continue;
    seen[n++] = path;
    if (LittleFS.exists(path) && LittleFS.remove(path)) {
      Log::console(PSTR("Prefs: removed stale legacy %s"), path);
    }
  }
}

// NOLINTNEXTLINE(complexity) one-off migration
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
  const char* drop[4] = {0};
  size_t drop_n = 0;

  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module) continue;
    if (s_storage.has(gs.group->module_id)) continue;
    EGLegacyRows aliases = gs.module->legacy_aliases();
    if (!aliases) continue;

    const char* path = gs.module->legacy_file();
    if (!path) path = "/geigerconfig.json";
    if (cached_file == nullptr || strcmp(cached_file, path) != 0) {
      doc.clear();
      cached_file = path;
      cached_ok = false;
      if (!LittleFS.exists(path)) continue;
      File f = LittleFS.open(path, "r");
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
      shadow_set(gs, idx, normalized, (size_t)nlen);
      any = true;
      imported++;
    }
    if (any && !write_group(gs)) {
      Log::console(PSTR("prefs: legacy persist failed for %s"), gs.group->module_id);
      continue;
    }
    if (any) {
      bool seen = false;
      for (size_t i = 0; i < drop_n; i++) {
        if (strcmp(drop[i], path) == 0) { seen = true; break; }
      }
      if (!seen && drop_n < sizeof(drop) / sizeof(drop[0])) drop[drop_n++] = path;
    }
  }
  if (imported > 0) {
    Log::console(PSTR("Prefs: Imported %d legacy value(s)"), imported);
  }
  for (size_t i = 0; i < drop_n; i++) {
    if (LittleFS.remove(drop[i])) {
      Log::console(PSTR("Prefs: removed legacy %s"), drop[i]);
    }
  }
}
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
  s_storage.readGroup(g->module_id, load_record_cb, &gs);

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

static bool pin_refused(const EGPref& p, const char* module, const char* key, const char* v) {
  int32_t pin;
  if (!(p.flags & (EGP_PIN | EGP_PIN_OUT)) || !parse_i32(v, &pin)) return false;
  s_refused = PinSafety::refuse(pin, p.flags & EGP_PIN_OUT);
  if (!s_refused) return false;
  Log::console(PSTR("Prefs: %s.%s pin %s refused (%s)"), module, key, v, s_refused);
  return true;
}

bool EGPrefs::put(const char* module, const char* key, const char* value) {
  s_refused = nullptr;
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
    shadow_set(*gs, idx, normalized, (size_t)nlen);
    gs->dirty = true;
    if (p.flags & EGP_REBOOT) s_reboot_put = true;
  }
  return true;
}

const char* EGPrefs::refused() { return s_refused; }

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

bool EGPrefs::commit(bool fire_callbacks) {
  s_restart_pending = s_reboot_put;
  s_reboot_put = false;
  bool all_ok = true;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (!gs.dirty) continue;
    if (!write_group(gs)) {
      all_ok = false;
      continue;
    }
    gs.dirty = false;
    if (fire_callbacks && runs(gs.module)) gs.module->on_prefs_saved();
  }
  return all_ok;
}

bool EGPrefs::remove_group(const char* module) {
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  bool removed = s_storage.removeGroup(module);
  for (size_t j = 0; j < gs->group->count.get(); j++) {
    shadow_reset(*gs, j);
  }
  gs->dirty = false;
  import_legacy();  // LEGACY IMPORT (remove after v1.0.0)
  if (runs(gs->module)) gs->module->on_prefs_loaded();
  return removed;
}

void EGPrefs::reset_all(bool keep_network) {
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    // imports skip wifi.* too, so keep its tuning
    if (keep_network && (strcmp(gs.group->module_id, "net") == 0 ||
                         strcmp(gs.group->module_id, "wifi") == 0)) continue;
    bool is_sys = keep_network && strcmp(gs.group->module_id, "sys") == 0;
    s_storage.removeGroup(gs.group->module_id);
    bool kept_any = false;
    for (size_t j = 0; j < gs.group->count.get(); j++) {
      if (is_sys && same_key(EG_K_WEB_PASS, gs.group->prefs.id_P(j))) {
        kept_any = true;
        continue;
      }
      shadow_reset(gs, j);
    }
    // re-dirty, keeps web_pass
    gs.dirty = kept_any;
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
  return i < MOVED_N ? MOVED[i].from : nullptr;
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
