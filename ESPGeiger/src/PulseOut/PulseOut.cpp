/*
  PulseOut.cpp - Per-pulse GPIO output for piezo / speaker / LED / line-out.

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
#ifdef PULSE_OUT

#include "PulseOut.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Globals.h"
#include "Corium/System/PinSafety.h"

#include "Corium/System/FastMillis.h"
#include "Corium/Util/StringUtil.h"

EG_PSTR(K_CYCLES, "cycles");
EG_PSTR(K_MAX_HZ, "max_hz");

PulseOut pulseout;
EG_REGISTER_MODULE(pulseout)

volatile uint8_t PulseOut::s_req = 0;
uint8_t PulseOut::s_seen = 0;
bool PulseOut::s_busy = false;

EG_PSTR(PO_H_EN,   "Per-pulse GPIO output for LED, piezo, speaker or line-out");
EG_PSTR(PO_H_PIN,  "-1 disables. Reboot to apply.");
EG_PSTR(PO_O_MODE, "Pulse|Burst|LED fade");
EG_PSTR(PO_L_FRQ,  "Burst Hz");
EG_PSTR(PO_H_FRQ,  "Match the piezo's marked resonance (1000-8000)");
EG_PSTR(PO_L_CYC,  "Burst cycles");
EG_PSTR(PO_H_CYC,  "1-10. More = louder and longer");
EG_PSTR(PO_L_MHZ,  "Max Hz");
EG_PSTR(PO_H_MHZ,  "Cap clicks per second (0 = unlimited, 1-200)");
EG_PSTR(PO_H_BRT,  "0-100% (LED only). Applies to Pulse + Fade modes.");
EG_PSTR(PO_H_QFR,  "Silence clicks from this time (blank = off)");

static const EGPref PULSE_PREF_ITEMS[] PROGMEM = {
  {EG_K_ENABLE,     EG_L_EN,   PO_H_EN,   "0",    nullptr, 0,    0,     0, EGP_BOOL, 0},
  {EG_K_PIN,        EG_L_PIN,  PO_H_PIN,  "-1",   nullptr, -1,   MAX_GPIO_PIN, 0, EGP_INT,  EGP_REBOOT | EGP_PIN_OUT},
  {EG_K_POLARITY,   EG_L_POL,  nullptr,   "0",    EG_O_POL, 0,   1,     0, EGP_ENUM, 0},
  {EG_K_MODE,       EG_L_MOD,  nullptr,   "0",    PO_O_MODE, 0,  2,     0, EGP_ENUM, 0},
  {EG_K_PULSE_MS,   EG_L_PUL,  EG_H_PUL,  "5",    nullptr, 1,    50,    0, EGP_UINT, EGP_ADVANCED},
  {EG_K_FREQ,       PO_L_FRQ,  PO_H_FRQ,  "3500", nullptr, 1000, 8000,  0, EGP_UINT, EGP_ADVANCED},
  {K_CYCLES,        PO_L_CYC,  PO_H_CYC,  "1",    nullptr, 1,    10,    0, EGP_UINT, EGP_ADVANCED},
  {EG_K_FADE_SHIFT, EG_L_FAD,  nullptr,   "1",    EG_O_FAD, 0,   2,     0, EGP_ENUM, EGP_ADVANCED},
  {EG_K_BRIGHTNESS, EG_L_BRT,  PO_H_BRT,  "100",  nullptr, 0,    100,   0, EGP_UINT, EGP_SLIDER | EGP_ADVANCED},
  {K_MAX_HZ,        PO_L_MHZ,  PO_H_MHZ,  "20",   nullptr, 0,    200,   0, EGP_UINT, EGP_ADVANCED},
  {EG_K_QUIET_FROM, EG_L_QFR,  PO_H_QFR,  "",     nullptr, 0,    0,     EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
  {EG_K_QUIET_TO,   EG_L_QTO,  EG_H_QTO,  "",     nullptr, 0,    0,     EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
};

static const EGPrefGroup PULSE_PREF_GROUP PROGMEM = {
  "pulse", "Pulse Out", 1,
  PULSE_PREF_ITEMS,
  sizeof(PULSE_PREF_ITEMS) / sizeof(PULSE_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, EG_K_ENABLE,
};

const EGPrefGroup* PulseOut::prefs_group() { return &PULSE_PREF_GROUP; }

static int8_t claim_pin(bool enabled, int p) {
  if (!enabled || p < 0) return -1;
  const char* why = PinSafety::claim_output(p, PSTR("PulseOut"));
  if (!why) return (int8_t)p;
  Log::console(PSTR("PulseOut: pin=%d unsafe (%s)"), p, why);
  return -1;
}

// pin is EGP_REBOOT, msTicker keeps the boot one
void PulseOut::read_pin(bool was_enabled) {
  if (!_begun) { _pin = EGPrefs::getInt("pulse", EG_K_PIN); return; }
  if (_enabled && !was_enabled && _engine.pin < 0)
    Log::console(PSTR("PulseOut: needs a reboot to start"));
}

void PulseOut::on_prefs_loaded() {
  bool was = _enabled;
  _enabled        = EGPrefs::getBool("pulse", EG_K_ENABLE);
  read_pin(was);
  _engine.mode     = (uint8_t)EGPrefs::getUInt("pulse", EG_K_MODE);
  _engine.pulse_us = EGPrefs::getUInt("pulse", EG_K_PULSE_MS) * 1000;
  _engine.freq_hz  = EGPrefs::getUInt("pulse", EG_K_FREQ);
  _engine.cycles   = EGPrefs::getUInt("pulse", K_CYCLES);
  _engine.polarity = (uint8_t)EGPrefs::getUInt("pulse", EG_K_POLARITY);
  _engine.fade_shift = (uint8_t)(EGPrefs::getUInt("pulse", EG_K_FADE_SHIFT) + 2);   // ui index
  _engine.max_hz   = EGPrefs::getUInt("pulse", K_MAX_HZ);
  _engine.active_level = PulseEngine::levelFromPercent(EGPrefs::getUInt("pulse", EG_K_BRIGHTNESS));
  _quiet.set(EGPrefs::getString("pulse", EG_K_QUIET_FROM),
             EGPrefs::getString("pulse", EG_K_QUIET_TO));
  _engine.commitConfig();
}

// === LEGACY IMPORT (remove after v1.0.0) ===
// v0.11 led.bin blip_pin, blip_pulse_ms to pulse.*
static const char* BLIP_MIG_MARKER = "/.pulse_blip_mig";

// NOLINTNEXTLINE(complexity) one-off migration
static bool migrate_legacy_blip() {
  if (!Fs::begin() || LittleFS.exists(BLIP_MIG_MARKER)) return false;
  File f = Fs::open_r("/prefs/led.bin");
  if (!f) {
    Fs::touch(BLIP_MIG_MARKER);
    return false;
  }
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 || memcmp(magic, "EGP1", 4) != 0) {
    f.close();
    Fs::touch(BLIP_MIG_MARKER);
    return false;
  }
  char pin_val[16]  = {0};
  char ms_val[16]   = {0};
  bool any = false;
  char key[24];
  char value[64];
  while (f.available() > 0) {
    uint8_t klen;
    if (f.read(&klen, 1) != 1 || klen == 0 || klen >= sizeof(key)) break;
    if (f.read((uint8_t*)key, klen) != klen) break;
    uint8_t vb[2];
    if (f.read(vb, 2) != 2) break;
    uint16_t vlen = (uint16_t)vb[0] | ((uint16_t)vb[1] << 8);
    if (vlen >= sizeof(value)) break;
    if (vlen > 0 && f.read((uint8_t*)value, vlen) != vlen) break;
    if (klen == 8  && strncmp(key, "blip_pin", 8) == 0 && vlen < sizeof(pin_val)) {
      memcpy(pin_val, value, vlen); pin_val[vlen] = '\0';
    } else if (klen == 13 && strncmp(key, "blip_pulse_ms", 13) == 0 && vlen < sizeof(ms_val)) {
      memcpy(ms_val, value, vlen); ms_val[vlen] = '\0';
    }
  }
  f.close();
  if (pin_val[0]) {
    EGPrefs::put("pulse", EG_K_PIN, pin_val);
    any = true;
  }
  if (ms_val[0]) {
    char* endp = nullptr;
    long ms = strtol(ms_val, &endp, 10);
    if (endp != ms_val && ms > 0) {
      if (ms > 50) ms = 50;
      EGPrefs::putInt("pulse", EG_K_PULSE_MS, ms);
      any = true;
    }
  }
  if (any) {
    EGPrefs::put("pulse", EG_K_ENABLE, "1");
    EGPrefs::commit(false);
    Log::console(PSTR("PulseOut: migrated blip pin=%s pulse_ms=%s from v0.11"),
                 pin_val[0] ? pin_val : "(none)", ms_val[0] ? ms_val : "(none)");
  } else {
    Log::console(PSTR("PulseOut: no blip pin/pulse keys in /prefs/led.bin"));
  }
  Fs::touch(BLIP_MIG_MARKER);
  return any;
}
// === END LEGACY IMPORT ===

void PulseOut::begin() {
  if (migrate_legacy_blip()) on_prefs_loaded();
  _begun = true;
  _engine.pin = claim_pin(_enabled, _pin);
  if (!_enabled || _engine.pin < 0) {
    if (_enabled && _engine.pin < 0) Log::console(PSTR("PulseOut: no pin set"));
    return;
  }
  // chip-id jitter, +/-15% width, +/-3% freq
  uint64_t id = DeviceInfo::hwid();
  uint32_t r = (uint32_t)id ^ (uint32_t)(id >> 32);
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  int32_t  s = (int32_t)(r & 0xFF) - 128;
  _engine.voice_pulse = 1.0f + s * (0.15f / 128.0f);
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  s = (int32_t)(r & 0xFF) - 128;
  _engine.voice_freq  = 1.0f + s * (0.03f / 128.0f);
  _engine.commitConfig();
  _engine.begin();
  Log::console(PSTR("PulseOut: GPIO%d mode=%u polarity=%u"),
               (int)_engine.pin, (unsigned)_engine.mode, (unsigned)_engine.polarity);
}

void PulseOut::notifyClick(unsigned long now_ms) {
  if (!_enabled) return;
  if (_quiet.active()) return;
  if (_engine.take(now_ms)) s_req++;
}

void PulseOut::poll(uint32_t now_us) {
  if (s_seen != s_req) {
    s_seen = s_req;
    _engine.start();
  }
  _engine.loop(now_us);
  s_busy = _engine.busy();
}

#endif
