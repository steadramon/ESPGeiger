/*
  OutputVars.cpp - Central registry of variables exposed to output channels.

  Copyright (C) 2026 @steadramon

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
#include "OutputVars.h"
#include <math.h>
#include <string.h>
#include "StringUtil.h"
#include "DeviceInfo.h"
#include "Wifi.h"
#include "../Counter/Counter.h"
#include "../EnvSensor/EnvSensor.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
extern HV hv;
#endif

extern Counter gcounter;

namespace OutputVars {

typedef size_t (*RenderFn)(char* buf, size_t cap);

struct Entry {
  const char* key;
  RenderFn    render;
  const char* desc;
};

// Render helpers - one per variable. All produce SRAM C strings; the
// template walker just memcpys the result. snprintf returns negative on
// error which we coerce to 0.
static inline size_t rn(int n, size_t cap) {
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

static size_t r_cpm(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%d", gcounter.get_cpm()), cap);
}
static size_t r_cps(char* buf, size_t cap) {
  char f[12]; format_f(f, sizeof(f), gcounter.get_cps());
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_cpm5(char* buf, size_t cap) {
  char f[12]; format_f(f, sizeof(f), gcounter.get_cpm5f());
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_cpm15(char* buf, size_t cap) {
  char f[12]; format_f(f, sizeof(f), gcounter.get_cpm15f());
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_cps_int(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%d", gcounter.get_last_cps()), cap);
}
static size_t r_usv(char* buf, size_t cap) {
  char f[12]; format_f(f, sizeof(f), gcounter.get_usv());
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_tc(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%lu", (unsigned long)gcounter.total_clicks), cap);
}
static size_t r_ut(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%lu", (unsigned long)DeviceInfo::uptime()), cap);
}
static size_t r_id(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%s", DeviceInfo::chipid()), cap);
}
static size_t r_name(char* buf, size_t cap) {
  const char* fn = DeviceInfo::friendlyName();
  if (!fn || !fn[0]) fn = DeviceInfo::hostname();
  return rn(snprintf(buf, cap, "%s", fn ? fn : ""), cap);
}
static size_t r_rssi(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%d", (int)Wifi::rssi), cap);
}
static size_t r_mem(char* buf, size_t cap) {
  return rn(snprintf(buf, cap, "%lu", (unsigned long)DeviceInfo::freeHeap()), cap);
}

// MightyOhm-style mode tag from current CPS. Useful in user templates that
// want to mimic the protocol shape without using format=3 exactly.
static size_t r_mode(char* buf, size_t cap) {
  int cps = gcounter.get_last_cps();
  const char* m = (cps > 255) ? "INST" : (cps > 15) ? "FAST" : "SLOW";
  return rn(snprintf(buf, cap, "%s", m), cap);
}

#ifdef ESPG_HV_ADC
static size_t r_hv(char* buf, size_t cap) {
  char f[12]; format_f(f, sizeof(f), hv.hvReading.get());
  return rn(snprintf(buf, cap, "%s", f), cap);
}
#endif

// Env vars - return empty (0) when sensor absent so the template gracefully
// degrades. Temperature uses the user's preferred unit (env.unit pref).
static size_t r_t(char* buf, size_t cap) {
  if (!envsensor.present()) return 0;
  float v = envsensor.tempUser();
  if (isnan(v)) return 0;
  char f[12]; format_f(f, sizeof(f), v);
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_h(char* buf, size_t cap) {
  if (!envsensor.present()) return 0;
  float v = envsensor.humidity();
  if (isnan(v)) return 0;
  char f[12]; format_f(f, sizeof(f), v);
  return rn(snprintf(buf, cap, "%s", f), cap);
}
static size_t r_p(char* buf, size_t cap) {
  if (!envsensor.present()) return 0;
  float v = envsensor.pressure();
  if (isnan(v)) return 0;
  char f[12]; format_f(f, sizeof(f), v);
  return rn(snprintf(buf, cap, "%s", f), cap);
}

static const Entry ENTRIES[] = {
  {"cpm",   r_cpm,    "1-minute CPM (integer)"},
  {"cps",   r_cps,    "Counts per second, rolling float"},
  {"cps_i", r_cps_int,"Counts in the most recent whole second (integer)"},
  {"cpm5",  r_cpm5,   "5-minute CPM rolling average"},
  {"cpm15", r_cpm15,  "15-minute CPM rolling average"},
  {"usv",   r_usv,    "Dose rate in microsieverts per hour"},
  {"tc",    r_tc,     "Lifetime total clicks since first boot"},
  {"ut",    r_ut,     "Uptime in seconds since boot"},
  {"id",    r_id,     "Six-hex chip id"},
  {"name",  r_name,   "Friendly name or hostname"},
  {"rssi",  r_rssi,   "WiFi RSSI in dBm"},
  {"mem",   r_mem,    "Free heap in bytes"},
  {"mode",  r_mode,   "MightyOhm-style mode tag SLOW/FAST/INST"},
#ifdef ESPG_HV_ADC
  {"hv",    r_hv,     "Measured HV in volts"},
#endif
  {"t",     r_t,      "Temperature in user-preferred unit (env.unit)"},
  {"h",     r_h,      "Relative humidity %"},
  {"p",     r_p,      "Pressure in hPa"},
  {nullptr, nullptr,  nullptr},
};

static const Entry* find(const char* key, size_t klen) {
  for (const Entry* e = ENTRIES; e->key; e++) {
    size_t k = strlen(e->key);
    if (k == klen && memcmp(e->key, key, k) == 0) return e;
  }
  return nullptr;
}

size_t render(const char* key, size_t klen, char* buf, size_t cap) {
  if (!key || !buf || cap == 0) return 0;
  const Entry* e = find(key, klen);
  if (!e) return 0;
  size_t n = e->render(buf, cap);
  if (n < cap) buf[n] = '\0';
  return n;
}

size_t renderTemplate(const char* tpl, char* buf, size_t cap) {
  if (!tpl || !buf || cap == 0) return 0;
  size_t pos = 0;
  const char* p = tpl;
  while (*p) {
    if (pos + 1 >= cap) break;  // leave room for NUL
    if (*p == '\\') {
      // Escape sequences. Unknown escapes pass through verbatim with the
      // backslash so authors can spot typos.
      p++;
      char c = 0;
      bool ok = true;
      switch (*p) {
        case 'n':  c = '\n'; break;
        case 'r':  c = '\r'; break;
        case 't':  c = '\t'; break;
        case '\\': c = '\\'; break;
        case '{':  c = '{';  break;
        case '}':  c = '}';  break;
        case '0':  c = '\0'; break;
        case 0:    buf[pos++] = '\\'; ok = false; break;
        default:   ok = false; break;
      }
      if (ok) { buf[pos++] = c; p++; }
      else if (*p) {
        if (pos + 2 >= cap) break;
        buf[pos++] = '\\';
        buf[pos++] = *p++;
      }
      continue;
    }
    if (*p == '{') {
      const char* k = p + 1;
      const char* e = strchr(k, '}');
      if (!e) { buf[pos++] = *p++; continue; }
      size_t klen = (size_t)(e - k);
      size_t n = render(k, klen, buf + pos, cap - pos);
      if (n > 0) {
        pos += n;
      } else {
        // Unknown / empty - render literal {name} so the author sees it.
        size_t need = klen + 2;
        if (pos + need >= cap) break;
        buf[pos++] = '{';
        memcpy(buf + pos, k, klen);
        pos += klen;
        buf[pos++] = '}';
      }
      p = e + 1;
      continue;
    }
    buf[pos++] = *p++;
  }
  buf[pos] = '\0';
  return pos;
}

const VarDef* list() {
  // Sentinel-terminated. Memory-aligned: VarDef has same layout as Entry's
  // first three fields so we can reuse the same storage. Future-proof by
  // building a real array if Entry diverges.
  static VarDef defs[sizeof(ENTRIES) / sizeof(ENTRIES[0])];
  static bool built = false;
  if (!built) {
    size_t i = 0;
    for (const Entry* e = ENTRIES; e->key; e++, i++) {
      defs[i].key = e->key;
      defs[i].desc = e->desc;
    }
    defs[i].key = nullptr;
    defs[i].desc = nullptr;
    built = true;
  }
  return defs;
}

}  // namespace OutputVars
