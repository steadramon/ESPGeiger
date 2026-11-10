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
#include "App/AppKeys.h"
#include "App/ChipVoice.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "App/AppConfig.h"
#include "Corium/System/PinSafety.h"

#include "Corium/Util/Bytes.h"
#include "Corium/Util/StringUtil.h"
#include "Counter/Counter.h"

CORIUM_PSTR(K_CYCLES, "cycles");
CORIUM_PSTR(K_MAX_HZ, "max_hz");

PulseOut pulseout;
CORIUM_REGISTER_MODULE(pulseout)

volatile uint8_t PulseOut::s_req = 0;
uint8_t PulseOut::s_seen = 0;
volatile uint8_t PulseOut::s_idle = 0;
uint8_t PulseOut::s_idle_seen = 0;
bool PulseOut::s_busy = false;

CORIUM_PSTR(PO_H_EN,   "Per-pulse GPIO output for LED, piezo, speaker or line-out");
CORIUM_PSTR(PO_H_PIN,  "-1 disables. Reboot to apply.");
CORIUM_PSTR(PO_O_MODE, "Pulse|Burst|LED fade");
CORIUM_PSTR(PO_L_FRQ,  "Burst Hz");
CORIUM_PSTR(PO_H_FRQ,  "Match the piezo's marked resonance (1000-8000)");
CORIUM_PSTR(PO_L_CYC,  "Burst cycles");
CORIUM_PSTR(PO_H_CYC,  "1-10. More = louder and longer");
CORIUM_PSTR(PO_L_MHZ,  "Max Hz");
CORIUM_PSTR(PO_H_MHZ,  "Cap clicks per second (0 = unlimited, 1-200)");
CORIUM_PSTR(PO_H_BRT,  "0-100% (LED only). Applies to Pulse + Fade modes.");
CORIUM_PSTR(PO_H_QFR,  "Silence clicks from this time (blank = off)");

static const corium::Pref PULSE_PREF_ITEMS[] PROGMEM = {
  {EG_K_ENABLE,     EG_L_EN,   PO_H_EN,   "0",    nullptr, 0,    0,     0, corium::P_BOOL, 0},
  {EG_K_PIN,        EG_L_PIN,  PO_H_PIN,  "-1",   nullptr, -1,   MAX_GPIO_PIN, 0, corium::P_INT,  corium::P_REBOOT | corium::P_PIN_OUT},
  {EG_K_POLARITY,   EG_L_POL,  nullptr,   "0",    EG_O_POL, 0,   1,     0, corium::P_ENUM, 0},
  {EG_K_MODE,       EG_L_MOD,  nullptr,   "0",    PO_O_MODE, 0,  2,     0, corium::P_ENUM, 0},
  {EG_K_PULSE_MS,   EG_L_PUL,  EG_H_PUL,  "5",    nullptr, 1,    50,    0, corium::P_UINT, corium::P_ADVANCED},
  {EG_K_FREQ,       PO_L_FRQ,  PO_H_FRQ,  "3500", nullptr, 1000, 8000,  0, corium::P_UINT, corium::P_ADVANCED},
  {K_CYCLES,        PO_L_CYC,  PO_H_CYC,  "1",    nullptr, 1,    10,    0, corium::P_UINT, corium::P_ADVANCED},
  {EG_K_FADE_SHIFT, EG_L_FAD,  nullptr,   "1",    EG_O_FAD, 0,   2,     0, corium::P_ENUM, corium::P_ADVANCED},
  {EG_K_BRIGHTNESS, EG_L_BRT,  PO_H_BRT,  "100",  nullptr, 0,    100,   0, corium::P_UINT, corium::P_SLIDER | corium::P_ADVANCED},
  {K_MAX_HZ,        PO_L_MHZ,  PO_H_MHZ,  "20",   nullptr, 0,    200,   0, corium::P_UINT, corium::P_ADVANCED},
  {EG_K_QUIET_FROM, EG_L_QFR,  PO_H_QFR,  "",     nullptr, 0,    0,     CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
  {EG_K_QUIET_TO,   EG_L_QTO,  EG_H_QTO,  "",     nullptr, 0,    0,     CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
};

static const corium::PrefGroup PULSE_PREF_GROUP PROGMEM = {
  "pulse", "Pulse Out", 1,
  PULSE_PREF_ITEMS,
  sizeof(PULSE_PREF_ITEMS) / sizeof(PULSE_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, EG_K_ENABLE,
};

const corium::PrefGroup* PulseOut::prefsGroup() { return &PULSE_PREF_GROUP; }

static int8_t claim_pin(bool enabled, int p) {
  if (!enabled || p < 0) return -1;
  const char* why = corium::PinSafety::claimOutput(p, PSTR("PulseOut"));
  if (!why) return (int8_t)p;
  corium::Log::console(PSTR("PulseOut: pin=%d unsafe (%s)"), p, why);
  return -1;
}

// pin is corium::P_REBOOT, msTicker keeps the boot one
void PulseOut::read_pin(bool was_enabled) {
  if (!_begun) { _pin = corium::Prefs::getInt("pulse", EG_K_PIN); return; }
  if (_enabled && !was_enabled && _engine.pin < 0)
    corium::Log::console(PSTR("PulseOut: needs a reboot to start"));
}

void PulseOut::onPrefsLoaded() {
  bool was = _enabled;
  _enabled        = corium::Prefs::getBool("pulse", EG_K_ENABLE);
  read_pin(was);
  _engine.mode     = (uint8_t)corium::Prefs::getUInt("pulse", EG_K_MODE);
  _engine.pulseUs = corium::Prefs::getUInt("pulse", EG_K_PULSE_MS) * 1000;
  _engine.freqHz   = corium::Prefs::getUInt("pulse", EG_K_FREQ);
  _engine.cycles   = corium::Prefs::getUInt("pulse", K_CYCLES);
  _engine.polarity = (uint8_t)corium::Prefs::getUInt("pulse", EG_K_POLARITY);
  _engine.fadeShift = (uint8_t)(corium::Prefs::getUInt("pulse", EG_K_FADE_SHIFT) + corium::PulseEngine::FADE_SHIFT_MIN);
  _engine.maxHz    = corium::Prefs::getUInt("pulse", K_MAX_HZ);
  _engine.activeLevel = corium::PulseEngine::levelFromPercent(corium::Prefs::getUInt("pulse", EG_K_BRIGHTNESS));
  _quiet.set(corium::Prefs::getString("pulse", EG_K_QUIET_FROM),
             corium::Prefs::getString("pulse", EG_K_QUIET_TO));
  _engine.commitConfig();
  apply_voice();
}

// else the pin keeps the old idle level till a click
void PulseOut::onPrefsSaved() {
  uint8_t was = _engine.polarity;
  onPrefsLoaded();
  if (_engine.polarity != was) s_idle++;
}

// after the engine clamps, so it can pass them
void PulseOut::apply_voice() {
  _engine.pulseUsEff     = (uint32_t)(_engine.pulseUsEff * _voice.pulse);
  _engine.burstFreqEff = (uint32_t)(_engine.burstFreqEff * _voice.freq);
  if (_engine.burstFreqEff == 0) _engine.burstFreqEff = 1;
  _engine.publish();
}

// legacy import, v0.11 led.bin blip_pin, blip_pulse_ms to pulse.*
static const char* BLIP_MIG_MARKER = "/.pulse_blip_mig";

// one-off migration
static bool migrate_legacy_blip() {
  if (!corium::Fs::begin() || corium::Fs::exists(BLIP_MIG_MARKER)) return false;
  File f = corium::Fs::openR(F("/prefs/led.bin"));
  if (!f) {
    corium::Fs::touch(BLIP_MIG_MARKER);
    return false;
  }
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 || memcmp_P(magic, PSTR("EGP1"), 4) != 0) {
    f.close();
    corium::Fs::touch(BLIP_MIG_MARKER);
    return false;
  }
  char pin_val[16]  = {};
  char ms_val[16]   = {};
  char key[24];
  char value[64];
  // rows: klen u8, key, vlen u16 le, value
  while (f.available() > 0) {
    uint8_t klen;
    if (f.read(&klen, 1) != 1 || klen == 0 || klen >= sizeof(key)) break;
    if (f.read((uint8_t*)key, klen) != klen) break;
    uint8_t vb[2];
    if (f.read(vb, 2) != 2) break;
    uint16_t vlen = rdLe16(vb);
    if (vlen >= sizeof(value)) break;
    if (vlen > 0 && f.read((uint8_t*)value, vlen) != vlen) break;
    if (klen == 8  && strncmp_P(key, PSTR("blip_pin"), 8) == 0 && vlen < sizeof(pin_val)) {
      memcpy(pin_val, value, vlen); pin_val[vlen] = '\0';
    } else if (klen == 13 && strncmp_P(key, PSTR("blip_pulse_ms"), 13) == 0 && vlen < sizeof(ms_val)) {
      memcpy(ms_val, value, vlen); ms_val[vlen] = '\0';
    }
  }
  f.close();
  // a refused one shows (none), Prefs said why
  if (pin_val[0] && !corium::Prefs::put("pulse", EG_K_PIN, pin_val)) pin_val[0] = '\0';
  if (ms_val[0]) {
    char* endp = nullptr;
    long ms = strtol(ms_val, &endp, 10);
    if (ms > 50) ms = 50;
    if (endp == ms_val || ms <= 0 || !corium::Prefs::putInt("pulse", EG_K_PULSE_MS, ms)) ms_val[0] = '\0';
  }
  bool any = pin_val[0] || ms_val[0];
  if (any) {
    // no marker, again next boot
    if (!corium::Prefs::put("pulse", EG_K_ENABLE, "1") || !corium::Prefs::commit(false)) {
      corium::Log::console(PSTR("PulseOut: not saved"));
      return true;
    }
    corium::Log::console(PSTR("PulseOut: migrated blip pin=%s pulse_ms=%s from v0.11"),
                 pin_val[0] ? pin_val : "(none)", ms_val[0] ? ms_val : "(none)");
  } else {
    corium::Log::console(PSTR("PulseOut: no blip pin/pulse keys in /prefs/led.bin"));
  }
  corium::Fs::touch(BLIP_MIG_MARKER);
  return any;
}

static void pulseout_click(uint32_t) { pulseout.notifyClick(corium::fastMillis()); }

void PulseOut::begin() {
  gcounter.on_click(pulseout_click);
  gcounter.on_rx_click(pulseout_click);
  if (migrate_legacy_blip()) onPrefsLoaded();
  _begun = true;
  _engine.pin = claim_pin(_enabled, _pin);
  if (!_enabled || _engine.pin < 0) {
    if (_enabled && _engine.pin < 0) corium::Log::console(PSTR("PulseOut: no pin set"));
    return;
  }
  _voice = chip_blip(corium::DeviceInfo::hwId());
  _engine.begin();   // commits
  apply_voice();
  corium::Log::console(PSTR("PulseOut: GPIO%d mode=%u polarity=%u"),
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
  if (s_idle_seen != s_idle) {
    s_idle_seen = s_idle;
    _engine.idleNow();
  }
  _engine.loop(now_us);
  s_busy = _engine.busy();
}

#endif
