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
#include "../Module/EGModuleRegistry.h"
#include "../Module/EGModule.h"
#include "../Logger/Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

#ifndef EGPREFS_MAX_GROUPS
#define EGPREFS_MAX_GROUPS 16
#endif

#define EGPREFS_NAMESPACE    "prefs"
#define EGPREFS_MAX_GROUP_SZ 1024

struct PrefShadow {
  const char* value;   // points to schema default_val (flash) or strdup'd override (heap)
  bool        is_heap; // true if value was malloc'd - must free before reassign
};

struct GroupShadow {
  const EGPrefGroup* group;
  EGModule*          module;
  PrefShadow*        shadows;
  bool               dirty;
};

static GroupShadow    s_groups[EGPREFS_MAX_GROUPS];
static size_t         s_group_count = 0;
static bool           s_begun = false;
static bool           s_restart_pending = false;
static EGPrefsStorage s_storage;

// Set shadow to a heap-copied value, freeing any previous heap value.
static void shadow_set(PrefShadow& s, const char* val, size_t len) {
  if (s.is_heap) free((void*)s.value);
  char* copy = (char*)malloc(len + 1);
  if (copy) { memcpy(copy, val, len); copy[len] = '\0'; }
  s.value = copy ? copy : "";
  s.is_heap = (copy != nullptr);
}

// Reset shadow to schema default (flash pointer, no heap).
static void shadow_reset(PrefShadow& s, const EGPref& p) {
  if (s.is_heap) free((void*)s.value);
  s.value = p.default_val ? p.default_val : "";
  s.is_heap = false;
}

namespace {

static GroupShadow* s_last_group = nullptr;  // one-entry cache for consecutive reads

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
  for (size_t i = 0; i < g->count; i++) {
    if (strcmp(g->prefs[i].id, key) == 0) return (int)i;
  }
  return -1;
}

const PrefShadow* find_shadow(const char* module, const char* key) {
  GroupShadow* gs = find_group(module);
  if (!gs) return nullptr;
  int idx = find_pref_index(gs->group, key);
  return (idx < 0) ? nullptr : &gs->shadows[idx];
}

// Validate + normalize value into a stack buffer. Returns length, or -1 on failure.
int validate(const EGPref* p, const char* value, char* out, size_t outsz) {
  if (!p || !value) return -1;
  switch (p->type) {
    case EGP_BOOL: {
      char c = value[0];
      bool v = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
      return snprintf_P(out, outsz, PSTR("%c"), v ? '1' : '0');
    }
    case EGP_INT: {
      char* end;
      long v = strtol(value, &end, 10);
      if (*end != '\0' || end == value) return -1;
      if (p->min_i != p->max_i && (v < p->min_i || v > p->max_i)) return -1;
      return snprintf_P(out, outsz, PSTR("%ld"), v);
    }
    case EGP_UINT: {
      char* end;
      unsigned long v = strtoul(value, &end, 10);
      if (*end != '\0' || end == value) return -1;
      if (p->min_i != p->max_i && ((int32_t)v < p->min_i || (int32_t)v > p->max_i)) return -1;
      return snprintf_P(out, outsz, PSTR("%lu"), v);
    }
    case EGP_FLOAT: {
      char* end;
      strtof(value, &end);
      if (*end != '\0' || end == value) return -1;
      // Pass through as-is (avoid Arduino String(float) heap alloc)
      size_t len = strlen(value);
      if (len >= outsz) return -1;
      memcpy(out, value, len + 1);
      return (int)len;
    }
    case EGP_STRING: {
      size_t len = strlen(value);
      if (p->max_len > 0 && len > p->max_len) return -1;
      if (len >= outsz) return -1;
      memcpy(out, value, len + 1);
      return (int)len;
    }
  }
  return -1;
}

// Callback for EGPrefsStorage::readGroup - overwrites matching shadow entry.
void load_record_cb(void* ctx, const char* key, uint8_t klen,
                    const char* val, uint16_t vlen) {
  GroupShadow* gs = (GroupShadow*)ctx;
  for (size_t j = 0; j < gs->group->count; j++) {
    const char* pid = gs->group->prefs[j].id;
    if (strlen(pid) == klen && memcmp(pid, key, klen) == 0) {
      shadow_set(gs->shadows[j], val, vlen);
      return;
    }
  }
}

bool write_group(GroupShadow& gs) {
  size_t total = 4;
  for (size_t j = 0; j < gs.group->count; j++) {
    total += 1 + strlen(gs.group->prefs[j].id) + 2 + strlen(gs.shadows[j].value);
  }
  if (total > EGPREFS_MAX_GROUP_SZ) {
    Log::console(PSTR("prefs: group %s too large (%u)"), gs.group->module_id, (unsigned)total);
    return false;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) return false;
  size_t p = 0;
  buf[p++] = 'E'; buf[p++] = 'G'; buf[p++] = 'P'; buf[p++] = '1';
  for (size_t j = 0; j < gs.group->count; j++) {
    const char* key = gs.group->prefs[j].id;
    size_t klen = strlen(key);
    const char* val = gs.shadows[j].value;
    size_t vlen = strlen(val);
    buf[p++] = (uint8_t)klen;
    memcpy(&buf[p], key, klen); p += klen;
    buf[p++] = (uint8_t)(vlen & 0xFF);
    buf[p++] = (uint8_t)((vlen >> 8) & 0xFF);
    memcpy(&buf[p], val, vlen); p += vlen;
  }
  bool ok = s_storage.writeGroup(gs.group->module_id, buf, total);
  free(buf);
  return ok;
}

// === LEGACY IMPORT (remove after v1.0.0) ===
void import_legacy() {
  bool any_missing = false;
  for (size_t gi = 0; gi < s_group_count; gi++) {
    if (s_groups[gi].module && s_groups[gi].module->legacy_aliases() &&
        !s_storage.has(s_groups[gi].group->module_id)) {
      any_missing = true; break;
    }
  }
  if (!any_missing) return;

  DynamicJsonDocument doc(3072);
  const char* cached_file = nullptr;
  bool cached_ok = false;
  int imported = 0;

  for (size_t gi = 0; gi < s_group_count; gi++) {
    GroupShadow& gs = s_groups[gi];
    if (!gs.module) continue;
    if (s_storage.has(gs.group->module_id)) continue;
    const EGLegacyAlias* aliases = gs.module->legacy_aliases();
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
    for (const EGLegacyAlias* a = aliases; a->legacy_key && a->new_key; a++) {
      const char* val = root[a->legacy_key].as<const char*>();
      if (!val) continue;
      int idx = find_pref_index(gs.group, a->new_key);
      if (idx < 0) continue;
      char normalized[128];
      int nlen = validate(&gs.group->prefs[idx], val, normalized, sizeof(normalized));
      if (nlen < 0) continue;
      shadow_set(gs.shadows[idx], normalized, (size_t)nlen);
      any = true;
      imported++;
    }
    if (any && !write_group(gs)) {
      Log::console(PSTR("prefs: legacy persist failed for %s"), gs.group->module_id);
    }
  }
  if (imported > 0) {
    Log::console(PSTR("Prefs: Imported %d legacy value(s)"), imported);
  }
}
// === END LEGACY IMPORT ===

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
    if (!m) continue;
    const EGPrefGroup* g = m->prefs_group();
    if (!g) continue;

    GroupShadow& gs = s_groups[s_group_count++];
    gs.group = g;
    gs.module = m;
    gs.dirty = false;
    gs.shadows = new PrefShadow[g->count];

    for (size_t j = 0; j < g->count; j++) {
      gs.shadows[j].value = g->prefs[j].default_val ? g->prefs[j].default_val : "";
      gs.shadows[j].is_heap = false;
    }
    s_storage.readGroup(g->module_id, load_record_cb, &gs);

    Log::debug(PSTR("prefs: %s (%u keys, v%u)"),
               g->module_id, (unsigned)g->count, (unsigned)g->version);
  }

  import_legacy();  // LEGACY IMPORT (remove after v1.0.0)

  s_begun = true;
  Log::console(PSTR("Prefs: Initialised (%u groups)"), (unsigned)s_group_count);

  // Notify modules after the "Initialised" log so any module-level logs from
  // on_prefs_loaded appear in conceptual order (prefs ready -> modules configured).
  for (size_t i = 0; i < s_group_count; i++) {
    if (s_groups[i].module) s_groups[i].module->on_prefs_loaded();
  }
}

const char* EGPrefs::getString(const char* module, const char* key) {
  const PrefShadow* s = find_shadow(module, key);
  return s ? s->value : "";
}

int32_t EGPrefs::getInt(const char* module, const char* key) {
  const PrefShadow* s = find_shadow(module, key);
  return s ? (int32_t)strtol(s->value, nullptr, 10) : 0;
}

uint32_t EGPrefs::getUInt(const char* module, const char* key) {
  const PrefShadow* s = find_shadow(module, key);
  return s ? (uint32_t)strtoul(s->value, nullptr, 10) : 0;
}

bool EGPrefs::getBool(const char* module, const char* key) {
  const PrefShadow* s = find_shadow(module, key);
  if (!s || s->value[0] == '\0') return false;
  char c = s->value[0];
  return (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
}

float EGPrefs::getFloat(const char* module, const char* key) {
  const PrefShadow* s = find_shadow(module, key);
  return s ? strtof(s->value, nullptr) : 0.0f;
}

bool EGPrefs::put(const char* module, const char* key, const char* value) {
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  int idx = find_pref_index(gs->group, key);
  if (idx < 0) return false;
  const EGPref& p = gs->group->prefs[idx];
  if (p.flags & EGP_READONLY) return false;

  char normalized[128];
  int nlen = validate(&p, value, normalized, sizeof(normalized));
  if (nlen < 0) return false;

  if (strcmp(normalized, gs->shadows[idx].value) != 0) {
    shadow_set(gs->shadows[idx], normalized, (size_t)nlen);
    gs->dirty = true;
  }
  return true;
}

bool EGPrefs::commit() {
  bool all_ok = true;
  for (size_t i = 0; i < s_group_count; i++) {
    GroupShadow& gs = s_groups[i];
    if (!gs.dirty) continue;
    if (!write_group(gs)) {
      all_ok = false;
      continue;
    }
    gs.dirty = false;
    if (gs.module) gs.module->on_prefs_saved();
  }
  return all_ok;
}

bool EGPrefs::remove_group(const char* module) {
  GroupShadow* gs = find_group(module);
  if (!gs) return false;
  bool removed = s_storage.removeGroup(module);
  for (size_t j = 0; j < gs->group->count; j++) {
    shadow_reset(gs->shadows[j], gs->group->prefs[j]);
  }
  gs->dirty = false;
  import_legacy();  // LEGACY IMPORT (remove after v1.0.0)
  if (gs->module) gs->module->on_prefs_loaded();
  return removed;
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

const EGPref* EGPrefs::find_pref(const char* module, const char* key) {
  GroupShadow* gs = find_group(module);
  if (!gs) return nullptr;
  int idx = find_pref_index(gs->group, key);
  return (idx < 0) ? nullptr : &gs->group->prefs[idx];
}
