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

static const EGPref SYSTEM_PREF_ITEMS[] = {
  {"ratio",  "uSv/h Ratio",   "CPM to uSv/h factor",  "151.0",      nullptr, 0, 0,    0,  EGP_FLOAT,  0},
  {"warn",   "Warning CPM",   "Warning trigger (CPM)","50",         nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {"alert",  "Alert CPM",     "Alert trigger (CPM)",  "100",        nullptr, 0, 9999, 0,  EGP_UINT,   0},
};

static const EGPrefGroup SYSTEM_PREF_GROUP = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const EGPrefGroup* SystemPrefs::prefs_group() { return &SYSTEM_PREF_GROUP; }

void SystemPrefs::on_prefs_loaded() {
  gcounter.set_ratio(EGPrefs::getFloat("sys", "ratio"));
  gcounter.set_warning((int)EGPrefs::getUInt("sys", "warn"));
  gcounter.set_alert((int)EGPrefs::getUInt("sys", "alert"));
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

static const EGPref INPUT_PREF_ITEMS[] = {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {"serial_type", "Serial Type", serial_type_desc, STR(GEIGER_SERIALTYPE), nullptr, 1, 255, 0, EGP_UINT, 0},
  {"cpm_window", "CPM Window", "Rolling CPM window (seconds). Lower=more responsive, higher=smoother. Reboot to apply.", "30", nullptr, 1, 60, 0, EGP_UINT, EGP_SLIDER},
#endif
#ifndef RXPIN_BLOCKED
  {"rx_pin", "RX Pin",        "",  STR(GEIGER_RXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"tx_pin", "TX Pin",        "",  STR(GEIGER_TXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
#ifdef USE_PCNT
  {"pcnt_filter", "PCNT Filter", "Pulse counter filter (0-1023, 0=off)", "200", nullptr, 0, 1023, 0, EGP_UINT, 0},
  {"pcnt_pull",   "PCNT Pull",   "Pin pull (0=none, 1=up, 2=down)",     STR(PCNT_PIN_PULL_DEFAULT), nullptr, 0, 2, 0, EGP_UINT, 0},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"debounce", "Debounce", "Debounce (us)", STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, EGP_UINT, 0},
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  {"pulse_width_us", "Pulse width", "Simulated pulse width (us)", STR(GEIGER_PULSE_WIDTH), nullptr, 10, 2000, 0, EGP_UINT, 0},
#endif
#ifndef DISABLE_INTERNAL_BLIP
  {"blip_led",    "Blip LED",       "Flash LED on each count", "1",   nullptr, 0, 1,   0, EGP_BOOL, 0},
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266))
  {"blip_bright", "Blip brightness","LED brightness (0-100%)", "80",  nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#endif
  {"quiet_from",  "Quiet from",     "Silence blip LED + beeper from (blank = off)", "", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
  {"quiet_to",    "Quiet to",       "End of quiet window; crosses midnight if from > to", "", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
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
  gcounter.set_cpm_window((uint8_t)EGPrefs::getUInt("input", "cpm_window"));
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
#ifndef DISABLE_INTERNAL_BLIP
  gcounter.set_blip_led(EGPrefs::getBool("input", "blip_led"));
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266))
  gcounter.set_blip_brightness((uint8_t)((EGPrefs::getUInt("input", "blip_bright") * 255 + 50) / 100));
#endif
  gcounter.set_quiet_hours(
    EGPrefs::getString("input", "quiet_from"),
    EGPrefs::getString("input", "quiet_to")
  );
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
  if (gcounter.get_cpm_window() != (uint8_t)EGPrefs::getUInt("input", "cpm_window")) need_reboot = true;
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
