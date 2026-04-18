/*
  SystemPrefs.cpp - Core geiger system prefs module.

  Copyright (C) 2025 @steadramon
*/
#include "SystemPrefs.h"
#include "../Module/EGModuleRegistry.h"
#include "../GeigerInput/GeigerInput.h"

#define _STR(x) #x
#define STR(x)  _STR(x)

// --- System prefs (live-apply, no reboot needed) ---

SystemPrefs systemPrefs;
EG_REGISTER_MODULE(systemPrefs)

static const EGPref SYSTEM_PREF_ITEMS[] = {
  {"ratio",  "uSv/h Ratio",   "CPM to uSv/h factor",  "151.0",      nullptr, 0, 0,    0,  EGP_FLOAT,  0},
  {"warn",   "Warning CPM",   "Warning trigger (CPM)","50",         nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {"alert",  "Alert CPM",     "Alert trigger (CPM)",  "100",        nullptr, 0, 9999, 0,  EGP_UINT,   0},
#ifdef USE_PCNT
  {"pcnt_filter", "PCNT Filter", "Pulse counter filter (0-1023, 0=off)", "100", nullptr, 0, 1023, 0, EGP_UINT, 0},
  {"pcnt_pull",   "PCNT Pull",   "Pin pull (0=none, 1=up, 2=down)",     STR(PCNT_PIN_PULL_DEFAULT), nullptr, 0, 2, 0, EGP_UINT, 0},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"debounce", "Debounce", "Debounce (us)", STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup SYSTEM_PREF_GROUP = {
  "system", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const EGPrefGroup* SystemPrefs::prefs_group() { return &SYSTEM_PREF_GROUP; }

void SystemPrefs::on_prefs_loaded() {
  gcounter.set_ratio(EGPrefs::getFloat("system", "ratio"));
  gcounter.set_warning((int)EGPrefs::getUInt("system", "warn"));
  gcounter.set_alert((int)EGPrefs::getUInt("system", "alert"));
#ifdef USE_PCNT
  gcounter.set_pcnt_filter((int)EGPrefs::getUInt("system", "pcnt_filter"));
  gcounter.set_pin_pull((int)EGPrefs::getUInt("system", "pcnt_pull"));
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  gcounter.set_debounce((int)EGPrefs::getUInt("system", "debounce"));
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias SYSTEM_LEGACY[] = {
  {"geigerRatio", "ratio"},
  {"geigerWarn",  "warn"},
  {"geigerAlert", "alert"},
#ifdef USE_PCNT
  {"pcntFilter",    "pcnt_filter"},
  {"pcntPull",      "pcnt_pull"},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"geigerDebounce","debounce"},
#endif
  {nullptr, nullptr},
};
const EGLegacyAlias* SystemPrefs::legacy_aliases() { return SYSTEM_LEGACY; }
// === END LEGACY IMPORT ===

// --- Input prefs (any change triggers reboot) ---

class InputPrefs : public EGModule {
public:
  const char* name() override { return "input"; }
  uint8_t display_order() override { return 12; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
  void on_prefs_saved() override {
    on_prefs_loaded();
    EGPrefs::request_restart();
  }
  const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
};

static InputPrefs inputPrefs;
EG_REGISTER_MODULE(inputPrefs)

static const EGPref INPUT_PREF_ITEMS[] = {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {"serial_type", "Serial Type", "1=GC10 2=GC10Next 3=MightyOhm 4=ESPGeiger", STR(GEIGER_SERIALTYPE), nullptr, 1, 255, 0, EGP_UINT, 0},
#endif
#ifndef RXPIN_BLOCKED
  {"rx_pin", "RX Pin",        "",  STR(GEIGER_RXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"tx_pin", "TX Pin",        "",  STR(GEIGER_TXPIN), nullptr, 0, 0, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup INPUT_PREF_GROUP = {
  "input", "Input", 1,
  INPUT_PREF_ITEMS,
  sizeof(INPUT_PREF_ITEMS) / sizeof(INPUT_PREF_ITEMS[0]),
};

const EGPrefGroup* InputPrefs::prefs_group() { return &INPUT_PREF_GROUP; }

void InputPrefs::on_prefs_loaded() {
#ifndef RXPIN_BLOCKED
  gcounter.set_rx_pin((int)EGPrefs::getUInt("input", "rx_pin"));
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  gcounter.set_tx_pin((int)EGPrefs::getUInt("input", "tx_pin"));
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias INPUT_LEGACY[] = {
#ifndef RXPIN_BLOCKED
  {"geigerRX", "rx_pin"},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"geigerTX", "tx_pin"},
#endif
  {nullptr, nullptr},
};
const EGLegacyAlias* InputPrefs::legacy_aliases() { return INPUT_LEGACY; }
// === END LEGACY IMPORT ===
