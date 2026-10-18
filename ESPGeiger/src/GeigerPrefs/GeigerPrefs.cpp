/*
  GeigerPrefs.cpp - ESPGeiger's own prefs: counter, lifetime, LED, HV pin.

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
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "GeigerKeys.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Output/PulseEngine.h"
#include "Corium/Util/MathUtil.h"
#include "Counter/Counter.h"
#include "Counter/Tube.h"
#include "GeigerInput/GeigerInput.h"
#if GEIGER_IS_TEST(GEIGER_TYPE)
#include "GeigerInput/GeigerInputTest.h"
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
#include "GeigerInput/SerialFormat.h"
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
#include "GeigerInput/Type/UdpRx.h"
#endif

EG_PSTR(K_WARN,           "warn");
EG_PSTR(K_ALERT,          "alert");
EG_PSTR(K_LIFE_EN,        "life_en");
EG_PSTR(K_RX_PIN,         "rx_pin");
EG_PSTR(K_TX_PIN,         "tx_pin");
EG_PSTR(K_PCNT_FILTER,    "pcnt_filter");
EG_PSTR(K_PCNT_PULL,      "pcnt_pull");
EG_PSTR(K_DEBOUNCE,       "debounce");
EG_PSTR(K_PULSE_WIDTH_US, "pulse_width_us");
EG_PSTR(K_CPM_WINDOW,     "cpm_window");
EG_PSTR(K_CPM_MODE,       "cpm_mode");
EG_PSTR(K_TUBE_HDR,       "_tube_hdr");
EG_PSTR(K_DEAD_TIME_US,   "dead_time_us");
EG_PSTR(K_BLIP_LED,       "blip_led");
EG_PSTR(K_BLIP_BRIGHT,    "blip_bright");
EG_PSTR(K_PWM_PIN,        "pwm_pin");

// Counter prefs, pin and serial type need a reboot

class CounterPrefs : public EGModule {
public:
  const char* name() override { return PSTR("counter"); }
  uint8_t priority() override { return EG_PRIORITY_HARDWARE - 1; }  // counter pins claimed first
  uint8_t display_order() override { return 12; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
  void on_prefs_saved() override;
  EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
private:
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  uint8_t _serial_type = 0;
#endif
};

static CounterPrefs counterprefs;
EG_REGISTER_MODULE(counterprefs)

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
EG_PSTR(IN_H_DTU, "GM tube dead time (us), from the datasheet. 0=disabled. SBM-20=190.");
#endif
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
EG_PSTR(IN_P_URCH, "[0-9a-fA-F]{" STR(CHIPID_LEN) "}");   // blank allowed
EG_PSTR(IN_L_URGP, "Group / listen IP");
EG_PSTR(IN_H_URGP, "239.x = multicast, else unicast listen.");
EG_PSTR(IN_H_URPT, "UDP port (default 57340)");
EG_PSTR(IN_L_URRM, "RX sleep");
EG_PSTR(IN_O_URRM, "Light (low power)|Modem (balanced)|None (always on)");
// classic esp32 misses multicast on modem sleep, c3/s3 don't
#ifdef ESP32
#include <sdkconfig.h>
#endif
#if CONFIG_IDF_TARGET_ESP32
#define UDPRX_RXMODE_DEFAULT "2"
#else
#define UDPRX_RXMODE_DEFAULT "1"
#endif
// in UdpRx.cpp, keeps WiFiUDP.h out
extern "C" void udprx_notify_prefs_saved();
#endif
EG_PSTR(CT_L_RAT, "uSv/h Ratio");
EG_PSTR(CT_H_RAT, "CPM to uSv/h factor");
EG_PSTR(CT_L_WRN, "Warning CPM");
EG_PSTR(CT_H_WRN, "Warning trigger (CPM)");
EG_PSTR(CT_L_ALR, "Alert CPM");
EG_PSTR(CT_H_ALR, "Alert trigger (CPM)");
EG_PSTR(CT_L_LE,  "Track lifetime");
EG_PSTR(CT_H_LE,  "Persist total clicks + exposure across reboots.");

static const EGPref COUNTER_PREF_ITEMS[] PROGMEM = {
  // Source selector
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {EG_K_SERIAL_TYPE,  IN_L_STY, nullptr, STR(GEIGER_SERIALTYPE), SerialFormat::TYPE_OPTS,
   GEIGER_STYPE_GC10, GEIGER_STYPE_TEMPLATE, 0, EGP_ENUM, EGP_REBOOT},
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {EG_K_UDPRX_MODE,   IN_L_URMD, IN_H_URMD, "0",                  IN_O_URMD,   0, 1,     0,  EGP_ENUM,   0},
#endif
  // Pins
#ifndef RXPIN_BLOCKED
  {K_RX_PIN,          IN_L_RXP, nullptr, STR(GEIGER_RXPIN), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, EGP_REBOOT | EGP_PIN},
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  {K_TX_PIN,          IN_L_TXP, nullptr, STR(GEIGER_TXPIN), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, EGP_ADVANCED | EGP_REBOOT | EGP_PIN_OUT},
#endif
  // UDP source target
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {EG_K_UDPRX_CHIPID, IN_L_URCH, IN_H_URCH, "",                   IN_P_URCH,   0, 0,     CHIPID_LEN,  EGP_STRING, 0},
  {EG_K_UDPRX_GROUP,  IN_L_URGP, IN_H_URGP, UDPRX_DEFAULT_GROUP, EG_P_IP,      0, 0,     EG_IPV4_MAX, EGP_STRING, EGP_ADVANCED},
  {EG_K_UDPRX_PORT,   EG_L_PRT,  IN_H_URPT, STR(UDPRX_DEFAULT_PORT), nullptr,  1, 65535, 0,  EGP_UINT,   EGP_ADVANCED},
  {EG_K_UDPRX_RXMODE, IN_L_URRM, nullptr,   UDPRX_RXMODE_DEFAULT, IN_O_URRM,   0, 2,     0,  EGP_ENUM,   EGP_ADVANCED},
#endif
  // Signal conditioning
#ifdef USE_PCNT
  {K_PCNT_FILTER,     IN_L_PCF, IN_H_PCF, "200", nullptr, 0, 1023, 0, EGP_UINT, EGP_ADVANCED},
  {K_PCNT_PULL,       IN_L_PCP, nullptr,  STR(PCNT_PIN_PULL_DEFAULT), IN_O_PCP, 0, 2, 0, EGP_ENUM, EGP_ADVANCED},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  {K_DEBOUNCE,        IN_L_DBN, IN_H_DBN, STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, EGP_UINT, EGP_ADVANCED},
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  {K_PULSE_WIDTH_US,  IN_L_PWU, IN_H_PWU, STR(GEIGER_PULSE_WIDTH), nullptr, 10, 2000, 0, EGP_UINT, EGP_ADVANCED},
#endif
  // CPM calculation
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {K_CPM_WINDOW,      IN_L_CPW, IN_H_CPW, "30", nullptr, 1, 60, 0, EGP_UINT, EGP_SLIDER | EGP_ADVANCED},
#endif
  {K_CPM_MODE,        IN_L_CMM, nullptr, "3", IN_O_CMM, 0, 4, 0, EGP_ENUM, EGP_ADVANCED},
  // Tube identity
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {EG_K_GEIGER_MODEL, IN_L_GMD, IN_H_GMD, GEIGER_MODEL, nullptr, 0, 0, EG_NAME_MAX, EGP_STRING, 0},
  {K_TUBE_HDR,        IN_L_THD, nullptr, nullptr, nullptr, 0, 0, 0, EGP_LABEL, 0},
  {EG_K_TUBE_ALPHA,   IN_L_TBA, IN_H_TBA, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {EG_K_TUBE_BETA,    IN_L_TBB, IN_H_TBB, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
  {EG_K_TUBE_GAMMA,   IN_L_TBG, IN_H_TBG, "0", nullptr, 0, 0, 0, EGP_BOOL, EGP_INLINE},
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  {K_DEAD_TIME_US,    IN_L_DTU, IN_H_DTU, STR(GEIGER_DEAD_TIME_DEFAULT), nullptr, 0, 1000, 0, EGP_UINT, EGP_ADVANCED},
#endif
  // Conversion and alarms
  {EG_K_RATIO,        CT_L_RAT, CT_H_RAT, STR(GEIGER_RATIO), nullptr, 1, 100000, 0, EGP_FLOAT, 0},
  {K_WARN,            CT_L_WRN, CT_H_WRN, "50",    nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {K_ALERT,           CT_L_ALR, CT_H_ALR, "100",   nullptr, 0, 9999, 0,  EGP_UINT,   0},
  {K_LIFE_EN,         CT_L_LE,  CT_H_LE,  "1",     nullptr, 0, 1,    0,  EGP_BOOL,   EGP_ADVANCED},
};

static const EGPrefGroup COUNTER_PREF_GROUP PROGMEM = {
  "counter", "Counter", 1,
  COUNTER_PREF_ITEMS,
  sizeof(COUNTER_PREF_ITEMS) / sizeof(COUNTER_PREF_ITEMS[0]),
  EGP_CAT_INPUT,
};

const EGPrefGroup* CounterPrefs::prefs_group() { return &COUNTER_PREF_GROUP; }

void CounterPrefs::on_prefs_loaded() {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  _serial_type = (uint8_t)EGPrefs::getUInt("counter", EG_K_SERIAL_TYPE);
  uint8_t cw = (uint8_t)EGPrefs::getUInt("counter", K_CPM_WINDOW);
  if (SerialFormat::has_cps(_serial_type)) cw = GEIGER_CPM_COUNT;
  gcounter.set_cpm_window(cw);
#endif
#ifndef RXPIN_BLOCKED
  gcounter.set_rx_pin((int)EGPrefs::getUInt("counter", K_RX_PIN));
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
  gcounter.set_tx_pin((int)EGPrefs::getUInt("counter", K_TX_PIN));
#endif
#ifdef USE_PCNT
  gcounter.set_pcnt_filter((int)EGPrefs::getUInt("counter", K_PCNT_FILTER));
  gcounter.apply_pcnt_filter();
  gcounter.set_pin_pull((int)EGPrefs::getUInt("counter", K_PCNT_PULL));
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  gcounter.set_debounce((int)EGPrefs::getUInt("counter", K_DEBOUNCE));
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  set_pulse_width_us((int)EGPrefs::getUInt("counter", K_PULSE_WIDTH_US));
#endif
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {
    const char* m = EGPrefs::getString("counter", EG_K_GEIGER_MODEL);
    if (m[0] != '\0') Tube::set_model(m);
  }
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  gcounter.set_dead_time_us((uint16_t)EGPrefs::getUInt("counter", K_DEAD_TIME_US));
#endif
  gcounter.set_cpm_mode((uint8_t)EGPrefs::getUInt("counter", K_CPM_MODE));
  gcounter.set_ratio(EGPrefs::getFloat("counter", EG_K_RATIO));
  gcounter.set_warning((int)EGPrefs::getUInt("counter", K_WARN));
  gcounter.set_alert((int)EGPrefs::getUInt("counter", K_ALERT));
  gcounter.set_lifetime_enabled(EGPrefs::getUInt("counter", K_LIFE_EN) != 0);
}

// pins and serial type are EGP_REBOOT
void CounterPrefs::on_prefs_saved() {
  bool need_reboot = false;
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  // only formats without their own cps
  if (!SerialFormat::has_cps(_serial_type) &&
      gcounter.get_cpm_window() != (uint8_t)EGPrefs::getUInt("counter", K_CPM_WINDOW)) need_reboot = true;
#endif
  on_prefs_loaded();
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  udprx_notify_prefs_saved();
#endif
  if (need_reboot) EGPrefs::request_restart();
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias COUNTER_LEGACY[] PROGMEM = {
  {"geigerRatio", "ratio"},
  {"geigerWarn",  "warn"},
  {"geigerAlert", "alert"},
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
  {},
};
EGLegacyRows CounterPrefs::legacy_aliases() { return COUNTER_LEGACY; }
// === END LEGACY IMPORT ===

// Lifetime prefs, own group

class LifePrefs : public EGModule {
public:
  const char* name() override { return PSTR("life"); }
  uint8_t display_order() override { return 0; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static LifePrefs lifeprefs;
EG_REGISTER_MODULE(lifeprefs)

static const EGPref LIFE_PREF_ITEMS[] PROGMEM = {
  {EG_K_CLK,  nullptr, nullptr, "0", nullptr, 0, 0, EG_U32_MAX_LEN, EGP_STRING, EGP_HIDDEN},
  {EG_K_CLKR, nullptr, nullptr, "0", nullptr, 0, 0, EG_U32_MAX_LEN, EGP_STRING, EGP_HIDDEN},
  {EG_K_FBT,  nullptr, nullptr, "0", nullptr, 0, 0, EG_U32_MAX_LEN, EGP_STRING, EGP_HIDDEN},
  {EG_K_SECS, nullptr, nullptr, "0", nullptr, 0, 0, EG_U32_MAX_LEN, EGP_STRING, EGP_HIDDEN},
};

static const EGPrefGroup LIFE_PREF_GROUP PROGMEM = {
  "life", "Lifetime", 1,
  LIFE_PREF_ITEMS,
  sizeof(LIFE_PREF_ITEMS) / sizeof(LIFE_PREF_ITEMS[0]),
};

const EGPrefGroup* LifePrefs::prefs_group() { return &LIFE_PREF_GROUP; }

void LifePrefs::on_prefs_loaded() {
  const char* lc = EGPrefs::getString("life", EG_K_CLK);
  gcounter.set_lifetime_clicks(lc ? strtoul(lc, nullptr, 10) : 0);
  const char* lcr = EGPrefs::getString("life", EG_K_CLKR);
  gcounter.set_lifetime_rollover(lcr ? strtoul(lcr, nullptr, 10) : 0);
  const char* fbt = EGPrefs::getString("life", EG_K_FBT);
  gcounter.set_first_boot_ts(fbt ? (uint32_t)strtoul(fbt, nullptr, 10) : 0);
  const char* secs = EGPrefs::getString("life", EG_K_SECS);
  gcounter.set_lifetime_seconds(secs ? strtoul(secs, nullptr, 10) : 0);
}

#if !defined(DISABLE_INTERNAL_BLIP) || defined(GEIGER_BLIPLED)
#define LED_BLIP
#endif

// every build, led.pin
class LedPrefs : public EGModule {
public:
  const char* name() override { return PSTR("led"); }
  uint8_t display_order() override { return 14; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static LedPrefs ledprefs;
EG_REGISTER_MODULE(ledprefs)

#ifndef LED_PIN_BLOCKED
EG_PSTR(LD_L_PIN, "Status LED");
EG_PSTR(LD_H_PIN, "GPIO of the board's own LED, -1 = none. Reboot to apply.");
#if LED_SEND_RECEIVE_ON == LOW
#define LD_POL_DEF "1"
#else
#define LD_POL_DEF "0"
#endif
#endif
#ifdef LED_BLIP
EG_PSTR(LD_L_BLP, "Blip LED");
EG_PSTR(LD_H_BLP, "Flash LED on each count");
#ifdef GEIGER_BLIPLED
EG_PSTR(LD_L_BRT, "Internal brightness");
#else
EG_PSTR(LD_L_BRT, "Blip brightness");
#endif
EG_PSTR(LD_H_BRT, "LED brightness (0-100%)");
EG_PSTR(LD_O_MOD, "Pulse|Fade");
#ifdef ESPGEIGER_HW
#define LD_PULSE_DEF "2"
#define LD_BRT_DEF   "100"
#else
#define LD_PULSE_DEF "20"
#define LD_BRT_DEF   "80"
EG_PSTR(LD_H_QFR, "Silence blip LED + beeper from (blank = off)");
#endif
#define LED_LABEL  "Blip LED"
#define LED_ENABLE K_BLIP_LED
#else
#define LED_LABEL  "Status LED"
#define LED_ENABLE nullptr
#endif

static const EGPref LED_PREF_ITEMS[] PROGMEM = {
#ifdef LED_BLIP
  {K_BLIP_LED,      LD_L_BLP, LD_H_BLP, "1",    nullptr, 0, 1,     0, EGP_BOOL, 0},
  {EG_K_MODE,       EG_L_MOD, nullptr,  "0",     LD_O_MOD, 0,  1,     0, EGP_ENUM, EGP_ADVANCED},
  {EG_K_PULSE_MS,   EG_L_PUL, EG_H_PUL, LD_PULSE_DEF, nullptr, 1, 50,  0, EGP_UINT, EGP_ADVANCED},
  {EG_K_FADE_SHIFT, EG_L_FAD, nullptr,  "1",     EG_O_FAD, 0,  2,     0, EGP_ENUM, EGP_ADVANCED},
  {K_BLIP_BRIGHT,   LD_L_BRT, LD_H_BRT, LD_BRT_DEF, nullptr, 0, 100,  0, EGP_UINT, EGP_SLIDER},
#ifndef ESPGEIGER_HW
  {EG_K_QUIET_FROM, EG_L_QFR, LD_H_QFR, "",      nullptr, 0,   0,     EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
  {EG_K_QUIET_TO,   EG_L_QTO, EG_H_QTO, "",      nullptr, 0,   0,     EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
#endif
#endif
#ifndef LED_PIN_BLOCKED
  {EG_K_PIN,        LD_L_PIN, LD_H_PIN, STR(LED_SEND_RECEIVE), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, EGP_ADVANCED | EGP_REBOOT | EGP_PIN_OUT},
  {EG_K_POLARITY,   EG_L_POL, nullptr,  LD_POL_DEF, EG_O_POL, 0, 1, 0, EGP_ENUM, EGP_ADVANCED | EGP_REBOOT},
#endif
};

static const EGPrefGroup LED_PREF_GROUP PROGMEM = {
  "led", LED_LABEL, 1,
  LED_PREF_ITEMS,
  sizeof(LED_PREF_ITEMS) / sizeof(LED_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM, LED_ENABLE,
};

const EGPrefGroup* LedPrefs::prefs_group() { return &LED_PREF_GROUP; }

void LedPrefs::on_prefs_loaded() {
  // boot only, attach() takes it once
  static bool s_pin_read = false;
  if (!s_pin_read) {
    s_pin_read = true;
#ifdef LED_PIN_BLOCKED
    LedSignal::use_pin(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON == LOW);
#else
    LedSignal::use_pin((int)EGPrefs::getInt("led", EG_K_PIN),
                       EGPrefs::getUInt("led", EG_K_POLARITY) == 1);
#endif
  }
#ifdef LED_BLIP
  gcounter.set_blip_led(EGPrefs::getBool("led", K_BLIP_LED));
  uint32_t blip_pct = EGPrefs::getUInt("led", K_BLIP_BRIGHT);
  uint32_t blip_level = brightness_curve(blip_pct, 255);
  if (blip_pct && !blip_level) blip_level = 1;
  gcounter.set_blip_brightness((uint8_t)blip_level);
  // UI is Pulse|Fade
  bool fade            = (EGPrefs::getUInt("led", EG_K_MODE) == 1);
  uint8_t engine_mode  = fade ? PulseEngine::MODE_FADE : PulseEngine::MODE_PULSE;
  uint8_t fadeIdx      = (uint8_t)EGPrefs::getUInt("led", EG_K_FADE_SHIFT);
  LedSignal::setBlipEngineConfig(
    engine_mode,
    (uint16_t)(EGPrefs::getUInt("led", EG_K_PULSE_MS) * 1000),
    fadeIdx,
    0
  );
#ifndef ESPGEIGER_HW
  gcounter.set_quiet_hours(
    EGPrefs::getString("led", EG_K_QUIET_FROM),
    EGPrefs::getString("led", EG_K_QUIET_TO)
  );
#endif
#endif
}

#if defined(ESPG_HV) && !defined(ESPGEIGER_HW)
#include "HV/HV.h"

class HVPinPrefs : public EGModule {
public:
  const char* name() override { return PSTR("hvpin"); }
  uint8_t display_order() override { return 14; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static HVPinPrefs hvpinprefs;
EG_REGISTER_MODULE(hvpinprefs)

EG_PSTR(HV_L_PWM, "HV PWM Pin");
EG_PSTR(HV_H_PWM, "GPIO for HV generator PWM (-1 = HV disabled). Reboot to apply.");

static const EGPref HV_PIN_PREF_ITEMS[] PROGMEM = {
  {K_PWM_PIN, HV_L_PWM, HV_H_PWM, STR(GEIGER_PWMPIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, EGP_REBOOT | EGP_PIN_OUT},
};

static const EGPrefGroup HV_PIN_PREF_GROUP PROGMEM = {
  "hvpin", "HV Hardware", 1,
  HV_PIN_PREF_ITEMS,
  sizeof(HV_PIN_PREF_ITEMS) / sizeof(HV_PIN_PREF_ITEMS[0]),
};

const EGPrefGroup* HVPinPrefs::prefs_group() { return &HV_PIN_PREF_GROUP; }

// boot only, live -1 leaves pwm unguarded
void HVPinPrefs::on_prefs_loaded() {
  static bool s_set = false;
  if (s_set) return;
  s_set = true;
  int pin = (int)EGPrefs::getInt("hvpin", K_PWM_PIN);
  hv.set_pwm_pin(pin);
  Log::console(PSTR("HV: pwm_pin=%d"), pin);
}
#endif
