/*
  SystemPrefs.cpp - Core geiger system prefs module.

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
#include "SystemPrefs.h"
#include "../Module/EGModuleRegistry.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Util/DeviceInfo.h"
#if GEIGER_IS_TEST(GEIGER_TYPE)
#include "../GeigerInput/GeigerInputTest.h"
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
#include "../GeigerInput/SerialFormat.h"
#endif

#define _STR(x) #x
#define STR(x)  _STR(x)

// --- System prefs (live-apply, no reboot needed) ---

SystemPrefs systemPrefs;
EG_REGISTER_MODULE(systemPrefs)

EG_PSTR(SY_L_NAM, "Friendly name");
EG_PSTR(SY_H_NAM, "Optional label for web UI");
EG_PSTR(SY_L_RAT, "uSv/h Ratio");
EG_PSTR(SY_H_RAT, "CPM to uSv/h factor");
EG_PSTR(SY_L_WRN, "Warning CPM");
EG_PSTR(SY_H_WRN, "Warning trigger (CPM)");
EG_PSTR(SY_L_ALR, "Alert CPM");
EG_PSTR(SY_H_ALR, "Alert trigger (CPM)");

static const EGPref SYSTEM_PREF_ITEMS[] = {
  {"name",   SY_L_NAM, SY_H_NAM, "",      nullptr, 0, 0,    32, EGP_STRING, 0},
  {"ratio",  SY_L_RAT, SY_H_RAT, "151.0", nullptr, 0, 0,    0,  EGP_FLOAT,  0},
  {"warn",   SY_L_WRN, SY_H_WRN, "50",    nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {"alert",  SY_L_ALR, SY_H_ALR, "100",   nullptr, 0, 9999, 0,  EGP_UINT,   0},
};

static const EGPrefGroup SYSTEM_PREF_GROUP = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const EGPrefGroup* SystemPrefs::prefs_group() { return &SYSTEM_PREF_GROUP; }

extern void applyFriendlyTitle();

void SystemPrefs::on_prefs_loaded() {
  gcounter.set_ratio(EGPrefs::getFloat("sys", "ratio"));
  gcounter.set_warning((int)EGPrefs::getUInt("sys", "warn"));
  gcounter.set_alert((int)EGPrefs::getUInt("sys", "alert"));
  DeviceInfo::setFriendlyName(EGPrefs::getString("sys", "name"));
  applyFriendlyTitle();
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias SYSTEM_LEGACY[] = {
  {"geigerRatio", "ratio"},
  {"geigerWarn",  "warn"},
  {"geigerAlert", "alert"},
  {nullptr, nullptr},
};
const EGLegacyAlias* SystemPrefs::legacy_aliases() { return SYSTEM_LEGACY; }
// === END LEGACY IMPORT ===

// --- Input prefs ---
// Pin / serial-type changes trigger a reboot (pinMode + attachInterrupt
// only run at init). PCNT filter, PCNT pull, and debounce apply live.

class InputPrefs : public EGModule {
public:
  const char* name() override { return "input"; }
  uint8_t display_order() override { return 12; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
  void on_prefs_saved() override;
  const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
private:
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  uint8_t _serial_type = 0;
#endif
};

static InputPrefs inputPrefs;
EG_REGISTER_MODULE(inputPrefs)

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
// Filled by SerialFormat::describe_types at prefs-load; pref entry
// points at this so help text tracks the TYPES[] table.
static char serial_type_desc[48] = "";
#endif

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
EG_PSTR(IN_L_STY, "Serial Type");
EG_PSTR(IN_L_CPW, "CPM Window (CPM-only counters)");
EG_PSTR(IN_H_CPW, "Rolling CPM window (seconds). Lower=more responsive, higher=smoother. Reboot to apply.");
#endif
#ifndef RXPIN_BLOCKED
EG_PSTR(IN_L_RXP, "RX Pin");
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
EG_PSTR(IN_L_TXP, "TX Pin");
#endif
#ifdef USE_PCNT
EG_PSTR(IN_L_PCF, "PCNT Filter");
EG_PSTR(IN_H_PCF, "Pulse counter filter (0-1023, 0=off)");
EG_PSTR(IN_L_PCP, "PCNT Pull");
EG_PSTR(IN_H_PCP, "Pin pull (0=none, 1=up, 2=down)");
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
EG_PSTR(IN_L_DBN, "Debounce");
EG_PSTR(IN_H_DBN, "Debounce (us)");
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
EG_PSTR(IN_L_PWU, "Pulse width");
EG_PSTR(IN_H_PWU, "Simulated pulse width (us)");
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
EG_PSTR(IN_L_DTU, "Tube dead time");
EG_PSTR(IN_H_DTU, "GM tube dead time (us). 0=disabled. J305=50, SBM-20=150.");
#endif
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
EG_PSTR(IN_L_GMD, "Geiger Counter");
EG_PSTR(IN_H_GMD, "Connected counter/tube model (e.g., SBM-20, J305)");
EG_PSTR(IN_L_THD, "Tube Detects");
EG_PSTR(IN_L_TBA, "\xCE\xB1 Alpha");
EG_PSTR(IN_H_TBA, "Detects alpha radiation");
EG_PSTR(IN_L_TBB, "\xCE\xB2 Beta");
EG_PSTR(IN_H_TBB, "Detects beta radiation");
EG_PSTR(IN_L_TBG, "\xCE\xB3 Gamma");
EG_PSTR(IN_H_TBG, "Detects gamma radiation");
#endif

static const EGPref INPUT_PREF_ITEMS[] = {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {"serial_type", IN_L_STY, serial_type_desc, STR(GEIGER_SERIALTYPE), nullptr, 1, 255, 0, EGP_UINT, 0},
  {"cpm_window",  IN_L_CPW, IN_H_CPW, "30", nullptr, 1, 60, 0, EGP_UINT, EGP_SLIDER},
#endif
#ifndef RXPIN_BLOCKED
  {"rx_pin", IN_L_RXP, nullptr, STR(GEIGER_RXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"tx_pin", IN_L_TXP, nullptr, STR(GEIGER_TXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
#ifdef USE_PCNT
  {"pcnt_filter", IN_L_PCF, IN_H_PCF, "200", nullptr, 0, 1023, 0, EGP_UINT, 0},
  {"pcnt_pull",   IN_L_PCP, IN_H_PCP, STR(PCNT_PIN_PULL_DEFAULT), nullptr, 0, 2, 0, EGP_UINT, 0},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"debounce", IN_L_DBN, IN_H_DBN, STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, EGP_UINT, 0},
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  {"pulse_width_us", IN_L_PWU, IN_H_PWU, STR(GEIGER_PULSE_WIDTH), nullptr, 10, 2000, 0, EGP_UINT, 0},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  {"dead_time_us", IN_L_DTU, IN_H_DTU, STR(GEIGER_DEAD_TIME_DEFAULT), nullptr, 0, 1000, 0, EGP_UINT, 0},
#endif
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {"geiger_model", IN_L_GMD, IN_H_GMD, GEIGER_MODEL, nullptr, 0, 0, 32, EGP_STRING, 0},
  {"_tube_hdr", IN_L_THD, nullptr, nullptr, nullptr, 0, 0, 0, EGP_LABEL, 0},
  {"tube_alpha", IN_L_TBA, IN_H_TBA, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {"tube_beta",  IN_L_TBB, IN_H_TBB, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {"tube_gamma", IN_L_TBG, IN_H_TBG, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
#endif
};

static const EGPrefGroup INPUT_PREF_GROUP = {
  "input", "Input", 1,
  INPUT_PREF_ITEMS,
  sizeof(INPUT_PREF_ITEMS) / sizeof(INPUT_PREF_ITEMS[0]),
};

const EGPrefGroup* InputPrefs::prefs_group() { return &INPUT_PREF_GROUP; }

void InputPrefs::on_prefs_loaded() {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  _serial_type = (uint8_t)EGPrefs::getUInt("input", "serial_type");
  SerialFormat::describe_types(serial_type_desc, sizeof(serial_type_desc));
  uint8_t cw = (uint8_t)EGPrefs::getUInt("input", "cpm_window");
  if (SerialFormat::has_cps(_serial_type)) cw = GEIGER_CPM_COUNT;
  gcounter.set_cpm_window(cw);
#endif
#ifndef RXPIN_BLOCKED
  gcounter.set_rx_pin((int)EGPrefs::getUInt("input", "rx_pin"));
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  gcounter.set_tx_pin((int)EGPrefs::getUInt("input", "tx_pin"));
#endif
#ifdef USE_PCNT
  gcounter.set_pcnt_filter((int)EGPrefs::getUInt("input", "pcnt_filter"));
  gcounter.apply_pcnt_filter();
  gcounter.set_pin_pull((int)EGPrefs::getUInt("input", "pcnt_pull"));
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  gcounter.set_debounce((int)EGPrefs::getUInt("input", "debounce"));
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  _pulse_width_us = (int)EGPrefs::getUInt("input", "pulse_width_us");
#endif
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {
    const char* m = EGPrefs::getString("input", "geiger_model");
    if (m[0] != '\0') DeviceInfo::setGeigermodel(m);
  }
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  gcounter.set_dead_time_us((uint16_t)EGPrefs::getUInt("input", "dead_time_us"));
#endif
}

void InputPrefs::on_prefs_saved() {
  bool need_reboot = false;
#ifndef RXPIN_BLOCKED
  if (gcounter.get_rx_pin() != (int)EGPrefs::getUInt("input", "rx_pin")) need_reboot = true;
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  if (gcounter.get_tx_pin() != (int)EGPrefs::getUInt("input", "tx_pin")) need_reboot = true;
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  if (_serial_type != (uint8_t)EGPrefs::getUInt("input", "serial_type")) need_reboot = true;
  if (!SerialFormat::has_cps(_serial_type) &&
      gcounter.get_cpm_window() != (uint8_t)EGPrefs::getUInt("input", "cpm_window")) need_reboot = true;
#endif
  on_prefs_loaded();
  if (need_reboot) EGPrefs::request_restart();
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias INPUT_LEGACY[] = {
#ifndef RXPIN_BLOCKED
  {"geigerRX", "rx_pin"},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"geigerTX", "tx_pin"},
#endif
#ifdef USE_PCNT
  {"pcntFilter",    "pcnt_filter"},
  {"pcntPull",      "pcnt_pull"},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"geigerDebounce","debounce"},
#endif
  {nullptr, nullptr},
};
const EGLegacyAlias* InputPrefs::legacy_aliases() { return INPUT_LEGACY; }
// === END LEGACY IMPORT ===

#if !defined(DISABLE_INTERNAL_BLIP) || defined(GEIGER_BLIPLED)
class LedPrefs : public EGModule {
public:
  const char* name() override { return "led"; }
  uint8_t display_order() override { return 13; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static LedPrefs ledPrefs;
EG_REGISTER_MODULE(ledPrefs)

EG_PSTR(LD_L_BLP, "Blip LED");
EG_PSTR(LD_H_BLP, "Flash LED on each count");
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266)) && !defined(GEIGER_BLIPLED)
EG_PSTR(LD_L_BRT, "Blip brightness");
EG_PSTR(LD_H_BRT, "LED brightness (0-100%)");
#endif
#if !defined(GEIGER_BLIPLED) && defined(HAS_EXT_BLIP)
EG_PSTR(LD_L_EXP, "External LED Pin");
EG_PSTR(LD_H_EXP, "GPIO to mirror blip on (-1 = off). Reboot to apply.");
EG_PSTR(LD_L_EXW, "External Pulse Width");
EG_PSTR(LD_H_EXW, "Pulse width on external pin (ms)");
#endif
EG_PSTR(LD_L_QFR, "Quiet from");
EG_PSTR(LD_H_QFR, "Silence blip LED + beeper from (blank = off)");
EG_PSTR(LD_L_QTO, "Quiet to");
EG_PSTR(LD_H_QTO, "End of quiet window; crosses midnight if from > to");

static const EGPref LED_PREF_ITEMS[] = {
  {"blip_led",    LD_L_BLP, LD_H_BLP, "1",  nullptr, 0, 1,   0, EGP_BOOL, 0},
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266)) && !defined(GEIGER_BLIPLED)
  {"blip_bright", LD_L_BRT, LD_H_BRT, "80", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#endif
#if !defined(GEIGER_BLIPLED) && defined(HAS_EXT_BLIP)
  {"blip_pin",      LD_L_EXP, LD_H_EXP, "-1", nullptr, -1, 39,  0, EGP_INT, 0},
  {"blip_pulse_ms", LD_L_EXW, LD_H_EXW, "2",  nullptr,  1, 100, 0, EGP_UINT, 0},
#endif
  {"quiet_from",  LD_L_QFR, LD_H_QFR, "",   nullptr, 0, 0,   5, EGP_STRING, EGP_TIME},
  {"quiet_to",    LD_L_QTO, LD_H_QTO, "",   nullptr, 0, 0,   5, EGP_STRING, EGP_TIME},
};

static const EGPrefGroup LED_PREF_GROUP = {
  "led", "Blip LED", 1,
  LED_PREF_ITEMS,
  sizeof(LED_PREF_ITEMS) / sizeof(LED_PREF_ITEMS[0]),
};

const EGPrefGroup* LedPrefs::prefs_group() { return &LED_PREF_GROUP; }

void LedPrefs::on_prefs_loaded() {
  gcounter.set_blip_led(EGPrefs::getBool("led", "blip_led"));
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266)) && !defined(GEIGER_BLIPLED)
  gcounter.set_blip_brightness((uint8_t)((EGPrefs::getUInt("led", "blip_bright") * 255 + 50) / 100));
#endif
#if !defined(GEIGER_BLIPLED) && defined(HAS_EXT_BLIP)
  gcounter.set_ext_blip_pin((int)EGPrefs::getInt("led", "blip_pin"));
  gcounter.set_ext_blip_pulse_ms((uint8_t)EGPrefs::getUInt("led", "blip_pulse_ms"));
#endif
  gcounter.set_quiet_hours(
    EGPrefs::getString("led", "quiet_from"),
    EGPrefs::getString("led", "quiet_to")
  );
}
#endif
