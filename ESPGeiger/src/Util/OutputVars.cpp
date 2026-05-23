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

enum : uint8_t {
  K_INT,       // signed-int producer (cpm, cps_i, rssi)
  K_FLOAT,     // float producer, formatted via format_f
  K_ULONG,     // unsigned long producer (tc, ut, mem)
  K_STR,       // const char* producer (id, name)
  K_MODE,      // inline MightyOhm-style mode tag
  K_ENV_T,     // env temperature in user unit
  K_ENV_H,     // env humidity %
  K_ENV_P,     // env pressure hPa
#ifdef ESPG_HV_ADC
  K_HV,        // measured HV in volts
#endif
};

struct Entry {
  const char* key;
  uint8_t     klen;   // compile-time length of key; lets find() skip strlen
  uint8_t     kind;
  union {
    int           (*fn_int)();
    float         (*fn_float)();
    unsigned long (*fn_ulong)();
    const char*   (*fn_str)();
  };
  const char* desc;
};

#define EV_KEY(s) s, (uint8_t)(sizeof(s) - 1)

// Free-function adapters - tiny, often optimised to tail calls. We need
// these because member-function pointers would inflate Entry by 4 bytes
// per row and frustrate the union packing.
static int           g_cpm()    { return gcounter.get_cpm(); }
static int           g_cps_i()  { return gcounter.get_last_cps(); }
static int           g_rssi()   { return (int)Wifi::rssi; }
static float         g_cps()    { return gcounter.get_cps(); }
static float         g_cpm5()   { return gcounter.get_cpm5f(); }
static float         g_cpm15()  { return gcounter.get_cpm15f(); }
static float         g_usv()    { return gcounter.get_usv(); }
static unsigned long g_tc()     { return (unsigned long)gcounter.total_clicks; }
static unsigned long g_ut()     { return (unsigned long)DeviceInfo::uptime(); }
static unsigned long g_mem()    { return (unsigned long)DeviceInfo::freeHeap(); }
static const char*   g_id()     { return DeviceInfo::chipid(); }
static const char*   g_name() {
  const char* fn = DeviceInfo::friendlyName();
  return (fn && fn[0]) ? fn : DeviceInfo::hostname();
}

static const Entry ENTRIES[] = {
  {EV_KEY("cpm"),   K_INT,    {.fn_int   = g_cpm},   "1-minute CPM (integer)"},
  {EV_KEY("cps"),   K_FLOAT,  {.fn_float = g_cps},   "Counts per second, rolling float"},
  {EV_KEY("cps_i"), K_INT,    {.fn_int   = g_cps_i}, "Counts in the most recent whole second (integer)"},
  {EV_KEY("cpm5"),  K_FLOAT,  {.fn_float = g_cpm5},  "5-minute CPM rolling average"},
  {EV_KEY("cpm15"), K_FLOAT,  {.fn_float = g_cpm15}, "15-minute CPM rolling average"},
  {EV_KEY("usv"),   K_FLOAT,  {.fn_float = g_usv},   "Dose rate in microsieverts per hour"},
  {EV_KEY("tc"),    K_ULONG,  {.fn_ulong = g_tc},    "Lifetime total clicks since first boot"},
  {EV_KEY("ut"),    K_ULONG,  {.fn_ulong = g_ut},    "Uptime in seconds since boot"},
  {EV_KEY("id"),    K_STR,    {.fn_str   = g_id},    "Six-hex chip id"},
  {EV_KEY("name"),  K_STR,    {.fn_str   = g_name},  "Friendly name or hostname"},
  {EV_KEY("rssi"),  K_INT,    {.fn_int   = g_rssi},  "WiFi RSSI in dBm"},
  {EV_KEY("mem"),   K_ULONG,  {.fn_ulong = g_mem},   "Free heap in bytes"},
  {EV_KEY("mode"),  K_MODE,   {.fn_int   = nullptr}, "MightyOhm-style mode tag SLOW/FAST/INST"},
#ifdef ESPG_HV_ADC
  {EV_KEY("hv"),    K_HV,     {.fn_int   = nullptr}, "Measured HV in volts"},
#endif
  {EV_KEY("t"),     K_ENV_T,  {.fn_int   = nullptr}, "Temperature in user-preferred unit (env.unit)"},
  {EV_KEY("h"),     K_ENV_H,  {.fn_int   = nullptr}, "Relative humidity %"},
  {EV_KEY("p"),     K_ENV_P,  {.fn_int   = nullptr}, "Pressure in hPa"},
  {nullptr, 0, 0,             {.fn_int   = nullptr}, nullptr},
};

static const Entry* find(const char* key, size_t klen) {
  for (const Entry* e = ENTRIES; e->key; e++) {
    if (e->klen == klen && memcmp(e->key, key, klen) == 0) return e;
  }
  return nullptr;
}

static inline size_t rn(int n, size_t cap) {
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

// Env helper - 3 entries collapse into this. Returns 0 (= "omit") when
// the sensor is absent OR the field is not provided by this chip (e.g.
// humidity on BMP280).
static size_t render_env_float(float v, char* buf, size_t cap) {
  if (!envsensor.present() || isnan(v)) return 0;
  char f[12]; format_f(f, sizeof(f), v);
  return rn(snprintf(buf, cap, "%s", f), cap);
}

size_t render(const char* key, size_t klen, char* buf, size_t cap) {
  if (!key || !buf || cap == 0) return 0;
  const Entry* e = find(key, klen);
  if (!e) return 0;
  size_t n = 0;
  switch (e->kind) {
    case K_INT:
      n = rn(snprintf(buf, cap, "%d", e->fn_int()), cap);
      break;
    case K_ULONG:
      n = rn(snprintf(buf, cap, "%lu", e->fn_ulong()), cap);
      break;
    case K_STR: {
      const char* s = e->fn_str();
      n = rn(snprintf(buf, cap, "%s", s ? s : ""), cap);
      break;
    }
    case K_FLOAT: {
      char f[12]; format_f(f, sizeof(f), e->fn_float());
      n = rn(snprintf(buf, cap, "%s", f), cap);
      break;
    }
    case K_MODE: {
      int cps = gcounter.get_last_cps();
      const char* m = (cps > 255) ? "INST" : (cps > 15) ? "FAST" : "SLOW";
      n = rn(snprintf(buf, cap, "%s", m), cap);
      break;
    }
#ifdef ESPG_HV_ADC
    case K_HV: {
      char f[12]; format_f(f, sizeof(f), hv.hvReading.get());
      n = rn(snprintf(buf, cap, "%s", f), cap);
      break;
    }
#endif
    case K_ENV_T: n = render_env_float(envsensor.tempUser(),  buf, cap); break;
    case K_ENV_H: n = render_env_float(envsensor.humidity(),  buf, cap); break;
    case K_ENV_P: n = render_env_float(envsensor.pressure(),  buf, cap); break;
  }
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
  // Sentinel-terminated. Built once from ENTRIES so callers see only the
  // public (key, desc) shape and can't accidentally invoke a renderer.
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
