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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Util/DeviceInfo.h"
#include "../Util/LedSignal.h"
#include "../Util/PinSafety.h"
#include "../WebPortal/WebPortal.h"
#if GEIGER_IS_TEST(GEIGER_TYPE)
#include "../GeigerInput/GeigerInputTest.h"
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
#include "../GeigerInput/SerialFormat.h"
#endif

#define _STR(x) #x
#define STR(x)  _STR(x)

// --- System prefs (live-apply, no reboot needed) ---

SystemPrefs sysprefs;
EG_REGISTER_MODULE(sysprefs)

EG_PSTR(SY_L_NAM, "Friendly name");
EG_PSTR(SY_H_NAM, "Optional label for web UI");
EG_PSTR(SY_L_RAT, "uSv/h Ratio");
EG_PSTR(SY_H_RAT, "CPM to uSv/h factor");
EG_PSTR(SY_L_WRN, "Warning CPM");
EG_PSTR(SY_H_WRN, "Warning trigger (CPM)");
EG_PSTR(SY_L_ALR, "Alert CPM");
EG_PSTR(SY_H_ALR, "Alert trigger (CPM)");
EG_PSTR(SY_L_WPW, "Web password");
EG_PSTR(SY_H_WPW, "Optional. Set to require login for the web UI (user: admin). Empty = no auth.");
EG_PSTR(SY_L_LE,  "Track lifetime");
EG_PSTR(SY_H_LE,  "Persist total clicks + exposure across reboots.");

static const EGPref SYSTEM_PREF_ITEMS[] = {
  {"name",     SY_L_NAM, SY_H_NAM, "",      nullptr, 0, 0,    32, EGP_STRING, 0},
  {"ratio",    SY_L_RAT, SY_H_RAT, "151.0", nullptr, 0, 0,    0,  EGP_FLOAT,  0},
  {"warn",     SY_L_WRN, SY_H_WRN, "50",    nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {"alert",    SY_L_ALR, SY_H_ALR, "100",   nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {"web_pass", SY_L_WPW, SY_H_WPW, "",      nullptr, 0, 0,    32, EGP_STRING, EGP_SENSITIVE | EGP_ADVANCED},
  {"life_en",  SY_L_LE,  SY_H_LE,  "1",     nullptr, 0, 1,    0,  EGP_BOOL,   EGP_ADVANCED},
#ifdef ESP32
  // 0=board default, else 80/160/240. C3 silicon caps at 160.
  {"cpu_mhz",  nullptr,  nullptr,  "0",     nullptr, 0, 240,  0,  EGP_UINT,   EGP_HIDDEN},
#endif
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
  DeviceInfo::setFriendlyName(EGPrefs::getString("sys", "name"));
  gcounter.set_lifetime_enabled(EGPrefs::getUInt("sys", "life_en") != 0);
#ifdef ESP32
  uint32_t mhz = EGPrefs::getUInt("sys", "cpu_mhz");
  if (mhz == 80 || mhz == 160 || mhz == 240) {
    if (setCpuFrequencyMhz(mhz)) {
      Log::console(PSTR("System: CPU set to %u MHz"), (unsigned)mhz);
    }
  }
#endif
  // Push web_pass into the running EGHttpServer. on_prefs_loaded fires
  // at boot AND after a /param save (default on_prefs_saved chains here),
  // so this covers both initial config and live changes.
  WebPortal::applyAuthFromPrefs();
}

// --- Lifetime prefs ---
// Hidden group dedicated to the persisted counter state so the periodic
// flash save (~every 6 h) only fires this lightweight callback, not the
// full sys-group cascade (ratio/warn/alert/name re-apply + auth refresh).

class LifePrefs : public EGModule {
public:
  const char* name() override { return "life"; }
  uint8_t display_order() override { return 0; }  // hidden from /param
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static LifePrefs lifeprefs;
EG_REGISTER_MODULE(lifeprefs)

static const EGPref LIFE_PREF_ITEMS[] = {
  {"clk",  nullptr, nullptr, "0", nullptr, 0, 0, 12, EGP_STRING, EGP_HIDDEN},
  {"clkr", nullptr, nullptr, "0", nullptr, 0, 0, 12, EGP_STRING, EGP_HIDDEN},
  {"fbt",  nullptr, nullptr, "0", nullptr, 0, 0, 12, EGP_STRING, EGP_HIDDEN},
  {"secs", nullptr, nullptr, "0", nullptr, 0, 0, 12, EGP_STRING, EGP_HIDDEN},
};

static const EGPrefGroup LIFE_PREF_GROUP = {
  "life", "Lifetime", 1,
  LIFE_PREF_ITEMS,
  sizeof(LIFE_PREF_ITEMS) / sizeof(LIFE_PREF_ITEMS[0]),
};

const EGPrefGroup* LifePrefs::prefs_group() { return &LIFE_PREF_GROUP; }

void LifePrefs::on_prefs_loaded() {
  const char* lc = EGPrefs::getString("life", "clk");
  gcounter.set_lifetime_clicks(lc ? strtoul(lc, nullptr, 10) : 0);
  const char* lcr = EGPrefs::getString("life", "clkr");
  gcounter.set_lifetime_rollover(lcr ? strtoul(lcr, nullptr, 10) : 0);
  const char* fbt = EGPrefs::getString("life", "fbt");
  gcounter.set_first_boot_ts(fbt ? (uint32_t)strtoul(fbt, nullptr, 10) : 0);
  const char* secs = EGPrefs::getString("life", "secs");
  gcounter.set_lifetime_seconds(secs ? strtoul(secs, nullptr, 10) : 0);
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

// --- Network prefs (static IP / DHCP) ---
// display_order==0 means hidden from /param. Edited via the dedicated /wifi
// page which renders the form with current values prefilled. Applied at
// boot in Wifi::connectOrPortal() before WiFi.begin().

class NetPrefs : public EGModule {
public:
  const char* name() override { return "net"; }
  uint8_t display_order() override { return 0; }
  const EGPrefGroup* prefs_group() override;
};

static NetPrefs netprefs;
EG_REGISTER_MODULE(netprefs)

EG_PSTR(NT_L_STA, "Use static IP");
EG_PSTR(NT_L_IP,  "IP address");
EG_PSTR(NT_L_GW,  "Gateway");
EG_PSTR(NT_L_SN,  "Subnet mask");
EG_PSTR(NT_L_DNS, "DNS server");
EG_PSTR(NT_P_IP,  "^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$");

static const EGPref NET_PREF_ITEMS[] = {
  {"static_ip", NT_L_STA, nullptr, "0", nullptr,  0, 0, 0,  EGP_BOOL,   0},
  {"ip",        NT_L_IP,  nullptr, "",  NT_P_IP,  0, 0, 15, EGP_STRING, 0},
  {"gw",        NT_L_GW,  nullptr, "",  NT_P_IP,  0, 0, 15, EGP_STRING, 0},
  {"sn",        NT_L_SN,  nullptr, "",  NT_P_IP,  0, 0, 15, EGP_STRING, 0},
  {"dns",       NT_L_DNS, nullptr, "",  NT_P_IP,  0, 0, 15, EGP_STRING, 0},
};

static const EGPrefGroup NET_PREF_GROUP = {
  "net", "Network", 1,
  NET_PREF_ITEMS,
  sizeof(NET_PREF_ITEMS) / sizeof(NET_PREF_ITEMS[0]),
};

const EGPrefGroup* NetPrefs::prefs_group() { return &NET_PREF_GROUP; }

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

static InputPrefs inputprefs;
EG_REGISTER_MODULE(inputprefs)

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
// Filled by SerialFormat::describe_types at prefs-load; pref entry
// points at this so help text tracks the TYPES[] table.
static char serial_type_desc[48] = "";
#endif

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
EG_PSTR(IN_L_STY, "Serial Type");
EG_PSTR(IN_L_CPW, "CPM Window (CPM-only counters)");
EG_PSTR(IN_H_CPW, "Window (s). Lower=responsive, higher=smooth. Reboot.");
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
EG_PSTR(IN_O_PCP, "None|Pull-up|Pull-down");
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
EG_PSTR(IN_L_DBN, "Debounce");
EG_PSTR(IN_H_DBN, "Pulse debounce (us). Filters electrical noise. 100-300 typical.");
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
EG_PSTR(IN_L_PWU, "Pulse width");
EG_PSTR(IN_H_PWU, "Simulated pulse width (us)");
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
EG_PSTR(IN_L_DTU, "Tube dead time");
EG_PSTR(IN_H_DTU, "GM tube dead time (us). 0=disabled. J305=50, SBM-20=150.");
#endif
// CPM mode pref - PCNT/serial/UDP now feed the ring via Counter::on_pulse_batch
// (synthetic spacing), so every counter type has a meaningful ring to switch
// on. min_pulse_us still only updates from true single-pulse hooks.
EG_PSTR(IN_L_CMM, "CPM mode");
EG_PSTR(IN_O_CMM, "Fast start|Live ring|Fixed 60s|Bucket|Adaptive fast");
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
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
EG_PSTR(IN_L_URMD, "Source Mode");
EG_PSTR(IN_O_URMD, "Specific device|Sum all sources");
EG_PSTR(IN_H_URMD, "Specific uses the Pin chipid below, auto-latching the first source heard if left blank.");
EG_PSTR(IN_L_URCH, "Pin chipid");
EG_PSTR(IN_H_URCH, "6-hex chipid of the source device; only used in Specific device mode");
EG_PSTR(IN_P_URCH, "[0-9a-fA-F]{0,6}");
EG_PSTR(IN_L_URGP, "Group / listen IP");
EG_PSTR(IN_H_URGP, "239.x = multicast, else unicast listen.");
EG_PSTR(IN_P_URGP, "[0-9.]+");
EG_PSTR(IN_L_URPT, "Port");
EG_PSTR(IN_H_URPT, "UDP port (default 57340)");
EG_PSTR(IN_L_URRM, "RX sleep");
EG_PSTR(IN_O_URRM, "Light (low power)|Modem (balanced)|None (always on)");
// Defined in UdpRx.cpp; routes prefs-saved notifications to GeigerUdpRx
// without dragging WiFiUDP.h into this file.
extern "C" void udprx_notify_prefs_saved();
#endif

static const EGPref INPUT_PREF_ITEMS[] = {
  // Source selector
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {"serial_type", IN_L_STY, serial_type_desc, STR(GEIGER_SERIALTYPE), nullptr, 1, 255, 0, EGP_UINT, 0},
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {"udprx_mode",   IN_L_URMD, IN_H_URMD, "0",                  IN_O_URMD,   0, 1,     0,  EGP_ENUM,   0},
#endif
  // Pins
#ifndef RXPIN_BLOCKED
  {"rx_pin", IN_L_RXP, nullptr, STR(GEIGER_RXPIN), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {"tx_pin", IN_L_TXP, nullptr, STR(GEIGER_TXPIN), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, EGP_ADVANCED},
#endif
  // UDP source target
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {"udprx_chipid", IN_L_URCH, IN_H_URCH, "",                   IN_P_URCH,   0, 0,     6,  EGP_STRING, 0},
  {"udprx_group",  IN_L_URGP, IN_H_URGP, "239.255.86.86",      IN_P_URGP,   0, 0,     24, EGP_STRING, EGP_ADVANCED},
  {"udprx_port",   IN_L_URPT, IN_H_URPT, "57340",              nullptr,     1, 65535, 0,  EGP_UINT,   EGP_ADVANCED},
  {"udprx_rxmode", IN_L_URRM, nullptr,   "1",                  IN_O_URRM,   0, 2,     0,  EGP_ENUM,   EGP_ADVANCED},
#endif
  // Signal conditioning
#ifdef USE_PCNT
  {"pcnt_filter", IN_L_PCF, IN_H_PCF, "200", nullptr, 0, 1023, 0, EGP_UINT, EGP_ADVANCED},
  {"pcnt_pull",   IN_L_PCP, nullptr,  STR(PCNT_PIN_PULL_DEFAULT), IN_O_PCP, 0, 2, 0, EGP_ENUM, EGP_ADVANCED},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {"debounce", IN_L_DBN, IN_H_DBN, STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, EGP_UINT, EGP_ADVANCED},
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  {"pulse_width_us", IN_L_PWU, IN_H_PWU, STR(GEIGER_PULSE_WIDTH), nullptr, 10, 2000, 0, EGP_UINT, EGP_ADVANCED},
#endif
  // CPM calculation
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {"cpm_window",  IN_L_CPW, IN_H_CPW, "30", nullptr, 1, 60, 0, EGP_UINT, EGP_SLIDER | EGP_ADVANCED},
#endif
  {"cpm_mode", IN_L_CMM, nullptr, "3", IN_O_CMM, 0, 4, 0, EGP_ENUM, EGP_ADVANCED},
  // Tube identity
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {"geiger_model", IN_L_GMD, IN_H_GMD, GEIGER_MODEL, nullptr, 0, 0, 32, EGP_STRING, 0},
  {"_tube_hdr", IN_L_THD, nullptr, nullptr, nullptr, 0, 0, 0, EGP_LABEL, 0},
  {"tube_alpha", IN_L_TBA, IN_H_TBA, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {"tube_beta",  IN_L_TBB, IN_H_TBB, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {"tube_gamma", IN_L_TBG, IN_H_TBG, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  {"dead_time_us", IN_L_DTU, IN_H_DTU, STR(GEIGER_DEAD_TIME_DEFAULT), nullptr, 0, 1000, 0, EGP_UINT, EGP_ADVANCED},
#endif
};

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  #define INPUT_GROUP_TITLE "Serial Input"
#elif GEIGER_IS_PULSE(GEIGER_TYPE)
  #define INPUT_GROUP_TITLE "Pulse Input"
#elif GEIGER_IS_UDPRX(GEIGER_TYPE)
  #define INPUT_GROUP_TITLE "UDP Input"
#elif GEIGER_IS_TEST(GEIGER_TYPE)
  #define INPUT_GROUP_TITLE "Test Input"
#else
  #define INPUT_GROUP_TITLE "Input"
#endif

static const EGPrefGroup INPUT_PREF_GROUP = {
  "input", INPUT_GROUP_TITLE, 1,
  INPUT_PREF_ITEMS,
  sizeof(INPUT_PREF_ITEMS) / sizeof(INPUT_PREF_ITEMS[0]),
  EGP_CAT_INPUT,
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
  set_pulse_width_us((int)EGPrefs::getUInt("input", "pulse_width_us"));
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
  gcounter.set_cpm_mode((uint8_t)EGPrefs::getUInt("input", "cpm_mode"));
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
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  udprx_notify_prefs_saved();   // reread mode/chipid/group/port, rebind UDP
#endif
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
  uint8_t display_order() override { return 14; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static LedPrefs ledprefs;
EG_REGISTER_MODULE(ledprefs)

EG_PSTR(LD_L_BLP, "Blip LED");
EG_PSTR(LD_H_BLP, "Flash LED on each count");
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266))
  #ifdef GEIGER_BLIPLED
EG_PSTR(LD_L_BRT, "Internal brightness");
  #else
EG_PSTR(LD_L_BRT, "Blip brightness");
  #endif
EG_PSTR(LD_H_BRT, "LED brightness (0-100%)");
#endif
#ifndef ESPGEIGER_HW
EG_PSTR(LD_L_MOD, "Mode");
EG_PSTR(LD_O_MOD, "Pulse|Fade");
EG_PSTR(LD_L_FAD, "Fade rate");
EG_PSTR(LD_O_FAD, "Fast|Medium|Slow");
EG_PSTR(LD_L_QFR, "Quiet from");
EG_PSTR(LD_H_QFR, "Silence blip LED + beeper from (blank = off)");
EG_PSTR(LD_L_QTO, "Quiet to");
EG_PSTR(LD_H_QTO, "End of quiet window; crosses midnight if from > to");
#endif
EG_PSTR(LD_L_PUL, "Pulse width \xc2\xb5s");
EG_PSTR(LD_H_PUL, "100-50000");

static const EGPref LED_PREF_ITEMS[] = {
  {"blip_led",    LD_L_BLP, LD_H_BLP, "1",    nullptr, 0, 1,     0, EGP_BOOL, 0},
#ifdef ESPGEIGER_HW
  // Only Pulse mode is available - HV holds Timer1 so tone()/PWM is unusable.
  {"pulse_us",    LD_L_PUL, LD_H_PUL, "2000",  nullptr, 100, 50000, 0, EGP_UINT, EGP_ADVANCED},
#else
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266))
  // Fade needs Timer1; ESP8266 test builds use it for the fake pulse gen.
  {"mode",        LD_L_MOD, nullptr,  "0",     LD_O_MOD, 0,  1,     0, EGP_ENUM, EGP_ADVANCED},
#endif
  {"pulse_us",    LD_L_PUL, LD_H_PUL, "20000", nullptr, 100, 50000, 0, EGP_UINT, EGP_ADVANCED},
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266))
  {"fade_shift",  LD_L_FAD, nullptr,  "1",     LD_O_FAD, 0,  2,     0, EGP_ENUM, EGP_ADVANCED},
  {"blip_bright", LD_L_BRT, LD_H_BRT, "80",    nullptr, 0,   100,   0, EGP_UINT, EGP_SLIDER},
#endif
  {"quiet_from",  LD_L_QFR, LD_H_QFR, "",      nullptr, 0,   0,     5, EGP_STRING, EGP_TIME | EGP_ADVANCED},
  {"quiet_to",    LD_L_QTO, LD_H_QTO, "",      nullptr, 0,   0,     5, EGP_STRING, EGP_TIME | EGP_ADVANCED},
#endif
};

static const EGPrefGroup LED_PREF_GROUP = {
  "led", "Blip LED", 1,
  LED_PREF_ITEMS,
  sizeof(LED_PREF_ITEMS) / sizeof(LED_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM, "blip_led",
};

const EGPrefGroup* LedPrefs::prefs_group() { return &LED_PREF_GROUP; }

void LedPrefs::on_prefs_loaded() {
  gcounter.set_blip_led(EGPrefs::getBool("led", "blip_led"));
#if !(GEIGER_IS_TEST(GEIGER_TYPE) && defined(ESP8266)) && !defined(ESPGEIGER_HW)
  gcounter.set_blip_brightness((uint8_t)((EGPrefs::getUInt("led", "blip_bright") * 255 + 50) / 100));
#else
  gcounter.set_blip_brightness(255);
#endif
#ifdef ESPGEIGER_HW
  uint8_t  engine_mode = 0;
  uint8_t  fadeIdx     = 1;
#else
  // UI mode 1 (Fade) maps to engine MODE_FADE (=2); engine MODE_BURST is unused here.
  uint8_t engine_mode  = (EGPrefs::getUInt("led", "mode") == 1) ? 2 : 0;
  uint8_t fadeIdx      = (uint8_t)EGPrefs::getUInt("led", "fade_shift");
#endif
  LedSignal::setBlipEngineConfig(
    engine_mode,
    (uint16_t)EGPrefs::getUInt("led", "pulse_us"),
    3500, 1, fadeIdx,
    0
  );
#ifndef ESPGEIGER_HW
  gcounter.set_quiet_hours(
    EGPrefs::getString("led", "quiet_from"),
    EGPrefs::getString("led", "quiet_to")
  );
#endif
}
#endif

#if defined(ESPG_HV) && !defined(ESPGEIGER_HW)
#include "../HV/HV.h"

class HVPinPrefs : public EGModule {
public:
  const char* name() override { return "hvpin"; }
  uint8_t display_order() override { return 14; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static HVPinPrefs hvpinprefs;
EG_REGISTER_MODULE(hvpinprefs)

EG_PSTR(HV_L_PWM, "HV PWM Pin");
EG_PSTR(HV_H_PWM, "GPIO for HV generator PWM (-1 = HV disabled). Reboot to apply.");

static const EGPref HV_PIN_PREF_ITEMS[] = {
  {"pwm_pin", HV_L_PWM, HV_H_PWM, STR(GEIGER_PWMPIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, 0},
};

static const EGPrefGroup HV_PIN_PREF_GROUP = {
  "hvpin", "HV Hardware", 1,
  HV_PIN_PREF_ITEMS,
  sizeof(HV_PIN_PREF_ITEMS) / sizeof(HV_PIN_PREF_ITEMS[0]),
};

const EGPrefGroup* HVPinPrefs::prefs_group() { return &HV_PIN_PREF_GROUP; }

void HVPinPrefs::on_prefs_loaded() {
  int pin = (int)EGPrefs::getInt("hvpin", "pwm_pin");
  hv.set_pwm_pin(pin);
  Log::console(PSTR("HV: pwm_pin=%d"), pin);
}
#endif

