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
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "GeigerKeys.h"
#include "App/AppKeys.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/PinSafety.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Output/LedSignal.h"
#include "BlipLed/BlipLed.h"
#include "Corium/Output/PulseEngine.h"
#include "App/LedPatterns.h"
#include "Corium/Util/MathUtil.h"
#include "Counter/Counter.h"
#include "Counter/CounterMaths.h"
#include "Counter/Tube.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Platform/Chip.h"
#if GEIGER_IS_TEST(GEIGER_TYPE)
#include "GeigerInput/GeigerInputTest.h"
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
#include "GeigerInput/SerialFormat.h"
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
#include "GeigerInput/Type/UdpRx.h"
#endif

CORIUM_PSTR(K_WARN,           "warn");
CORIUM_PSTR(K_ALERT,          "alert");
CORIUM_PSTR(K_LIFE_EN,        "life_en");
CORIUM_PSTR(K_RX_PIN,         "rx_pin");
CORIUM_PSTR(K_TX_PIN,         "tx_pin");
CORIUM_PSTR(K_PCNT_FILTER,    "pcnt_filter");
CORIUM_PSTR(K_PCNT_PULL,      "pcnt_pull");
CORIUM_PSTR(K_COUNT_MODE,     "count_mode");
CORIUM_PSTR(K_COUNT_KEPT,     "count_kept");
CORIUM_PSTR(K_DEBOUNCE,       "debounce");
CORIUM_PSTR(K_PULSE_WIDTH_US, "pulse_width_us");
CORIUM_PSTR(K_CPM_WINDOW,     "cpm_window");
CORIUM_PSTR(K_CPM_MODE,       "cpm_mode");
CORIUM_PSTR(K_TUBE_HDR,       "_tube_hdr");
CORIUM_PSTR(K_DET_HDR,        "_det_hdr");
CORIUM_PSTR(K_DOSE_HDR,       "_dose_hdr");
CORIUM_PSTR(K_COUNT_HDR,      "_count_hdr");
CORIUM_PSTR(K_DEAD_TIME_US,   "dead_time_us");
CORIUM_PSTR(K_BLIP_LED,       "blip_led");
CORIUM_PSTR(K_BLIP_BRIGHT,    "blip_bright");
CORIUM_PSTR(K_PWM_PIN,        "pwm_pin");

// interrupt counting, debounce applies
#if GEIGER_IS_PULSE(GEIGER_TYPE) && (!defined(USE_PCNT) || defined(PCNT_SELECTABLE))
#define PREF_DEBOUNCE
#endif
// rows this build has
#ifndef RXPIN_BLOCKED
#define PREF_RX_PIN
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1 && !defined(TXPIN_BLOCKED)
#define PREF_TX_PIN
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TEST || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
#define PREF_PULSE_WIDTH
#endif
#if !defined(GEIGER_MODEL_FIXED) && !GEIGER_IS_TEST(GEIGER_TYPE)
#define PREF_MODEL
#endif

// Counter prefs, pin and serial type need a reboot

class CounterPrefs : public corium::Module {
public:
  const char* name() override { return PSTR("counter"); }
  uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE - 1; }  // counter pins claimed first
  uint8_t displayOrder() override { return 12; }
  const corium::PrefGroup* prefsGroup() override;
#ifdef PCNT_SELECTABLE
  const corium::PrefShow* prefsShow() override;
#endif
  void onPrefsLoaded() override;
  void onPrefsSaved() override;
  corium::LegacyRows legacyAliases() override;
private:
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  uint8_t _serial_type = 0;
#endif
};

static CounterPrefs counterprefs;
CORIUM_REGISTER_MODULE(counterprefs)

#if GEIGER_IS_SERIAL(GEIGER_TYPE)
CORIUM_PSTR(IN_L_STY, "Serial type");
CORIUM_PSTR(IN_L_CPW, "CPM window (CPM-only counters)");
CORIUM_PSTR(IN_H_CPW, "Window (s). Lower=responsive, higher=smooth.");
#endif
#ifdef PREF_RX_PIN
CORIUM_PSTR(IN_L_RXP, "RX pin");
#endif
#ifdef PREF_TX_PIN
CORIUM_PSTR(IN_L_TXP, "TX pin");
#endif
#ifdef USE_PCNT
CORIUM_PSTR(IN_L_PCF, "PCNT filter");
CORIUM_PSTR(IN_H_PCF, "Pulse counter filter (0-1023, 0 = off)");
CORIUM_PSTR(IN_L_PCP, "PCNT pull");
CORIUM_PSTR(IN_O_PCP, "None|Pull-up|Pull-down");
#endif
#ifdef PCNT_SELECTABLE
CORIUM_PSTR(IN_L_CNT, "Count with");
CORIUM_PSTR(IN_O_CNT, "Hardware|Interrupt");
CORIUM_PSTR(IN_H_CNT, "Interrupt has the debounce dead time, reads low at high CPM.");
#ifdef NO_PCNT_ENV
#define COUNT_MODE_DEF "1"   // _no_pcnt envs
#else
#define COUNT_MODE_DEF "0"
#endif
#endif
#ifdef PREF_DEBOUNCE
CORIUM_PSTR(IN_L_DBN, "Debounce");
CORIUM_PSTR(IN_H_DBN, "Pulse debounce (us). Filters electrical noise. 100-300 typical.");
#endif
#ifdef PREF_PULSE_WIDTH
CORIUM_PSTR(IN_L_PWU, "Pulse width");
CORIUM_PSTR(IN_H_PWU, "Simulated pulse width (us)");
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
CORIUM_PSTR(IN_L_DTU, "Tube dead time");
CORIUM_PSTR(IN_H_DTU, "GM tube dead time (us), from the datasheet. 0 = off. SBM-20=190.");
#endif
CORIUM_PSTR(IN_L_CMM, "CPM mode");
CORIUM_PSTR(IN_O_CMM, "Fast start|Live ring|Fixed 60s|Bucket|Adaptive fast");
CORIUM_PSTR(IN_L_DET, "Detector");
CORIUM_PSTR(IN_L_DOSE, "Dose &amp; alarms");
CORIUM_PSTR(IN_L_COUNT, "Counting");
#ifdef PREF_MODEL
CORIUM_PSTR(IN_L_GMD, "Tube model");
CORIUM_PSTR(IN_H_GMD, "Connected counter/tube model (e.g., SBM-20, J305)");
CORIUM_PSTR(IN_L_THD, "Tube detects");
CORIUM_PSTR(IN_L_TBA, "\xCE\xB1 Alpha");
CORIUM_PSTR(IN_H_TBA, "Detects alpha radiation");
CORIUM_PSTR(IN_L_TBB, "\xCE\xB2 Beta");
CORIUM_PSTR(IN_H_TBB, "Detects beta radiation");
CORIUM_PSTR(IN_L_TBG, "\xCE\xB3 Gamma");
CORIUM_PSTR(IN_H_TBG, "Detects gamma radiation");
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
CORIUM_PSTR(IN_L_URMD, "Source mode");
CORIUM_PSTR(IN_O_URMD, "Specific device|Sum all sources");
CORIUM_PSTR(IN_H_URMD, "Specific: source chip ID below, blank = first source heard");
CORIUM_PSTR(IN_L_URCH, "Source chip ID");
CORIUM_PSTR(IN_H_URCH, "6-hex chipid of the source, Specific mode only");
CORIUM_PSTR(IN_P_URCH, UDPRX_CHIPID_PAT);
CORIUM_PSTR(IN_L_URGP, "Group / listen IP");
CORIUM_PSTR(IN_H_URGP, "239.x = multicast, else unicast listen.");
CORIUM_PSTR(IN_H_URPT, "UDP port (default 57340)");
CORIUM_PSTR(IN_L_URRM, "RX sleep");
CORIUM_PSTR(IN_O_URRM, "Light (low power)|Modem (balanced)|None (always on)");
// modem sleep where multicast still gets in
#if CORIUM_HAS_SLEEP_MCAST
#define UDPRX_RXMODE_DEFAULT "1"
#else
#define UDPRX_RXMODE_DEFAULT "2"
#endif
// in UdpRx.cpp, keeps WiFiUDP.h out
extern "C" void udprx_notify_prefs_saved();
#endif
CORIUM_PSTR(CT_L_RAT, "Dose ratio");
CORIUM_PSTR(CT_H_RAT, "CPM to uSv/h factor");
CORIUM_PSTR(CT_L_WRN, "Warning CPM");
CORIUM_PSTR(CT_H_WRN, "Warning trigger (CPM), 0 = off");
CORIUM_PSTR(CT_L_ALR, "Alert CPM");
CORIUM_PSTR(CT_H_ALR, "Alert trigger (CPM), 0 = off");
CORIUM_PSTR(CT_L_LE,  "Track lifetime");
CORIUM_PSTR(CT_H_LE,  "Keep lifetime clicks and dose across restarts.");

// a -D one is the row default too
#if defined(USE_PCNT) && !defined(PCNT_FILTER)
#define PCNT_FILTER 200
#endif

static const corium::Pref COUNTER_PREF_ITEMS[] PROGMEM = {
  {K_DET_HDR,         IN_L_DET, nullptr, nullptr, nullptr, 0, 0, 0, corium::P_HEADER, 0},
  // Source selector
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {EG_K_SERIAL_TYPE,  IN_L_STY, nullptr, STR(GEIGER_SERIALTYPE), SerialFormat::TYPE_OPTS,
   GEIGER_STYPE_GC10, GEIGER_STYPE_TEMPLATE, 0, corium::P_ENUM, corium::P_REBOOT},
#endif
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {EG_K_UDPRX_MODE,   IN_L_URMD, IN_H_URMD, "0",                  IN_O_URMD,   0, 1,     0,  corium::P_ENUM,   0},
#endif
  // Pins
#ifdef PREF_RX_PIN
  {K_RX_PIN,          IN_L_RXP, nullptr, STR(GEIGER_RXPIN), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_REBOOT | corium::P_PIN | corium::P_PIN_IRQ},
#endif
#ifdef PREF_TX_PIN
  {K_TX_PIN,          IN_L_TXP, nullptr, STR(GEIGER_TXPIN), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
#endif
  // UDP source target
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  {EG_K_UDPRX_CHIPID, IN_L_URCH, IN_H_URCH, "",                   IN_P_URCH,   0, 0,     CHIPID_LEN,  corium::P_STRING, 0},
  {EG_K_UDPRX_GROUP,  IN_L_URGP, IN_H_URGP, UDPRX_DEFAULT_GROUP, EG_P_IP,      0, 0,     CORIUM_IPV4_MAX, corium::P_STRING, corium::P_ADVANCED},
  {EG_K_UDPRX_PORT,   EG_L_PRT,  IN_H_URPT, STR(UDPRX_DEFAULT_PORT), nullptr,  1, 65535, 0,  corium::P_UINT,   corium::P_ADVANCED},
  {EG_K_UDPRX_RXMODE, IN_L_URRM, nullptr,   UDPRX_RXMODE_DEFAULT, IN_O_URRM,   0, 2,     0,  corium::P_ENUM,   corium::P_ADVANCED},
#endif
  // Tube identity
#ifdef PREF_MODEL
  {EG_K_GEIGER_MODEL, IN_L_GMD, IN_H_GMD, GEIGER_MODEL, nullptr, 0, 0, CORIUM_NAME_MAX, corium::P_STRING, 0},
  {K_TUBE_HDR,        IN_L_THD, nullptr, nullptr, nullptr, 0, 0, 0, corium::P_LABEL, 0},
  {EG_K_TUBE_ALPHA,   IN_L_TBA, IN_H_TBA, "0", nullptr, 0, 0, 0, corium::P_BOOL, corium::P_INLINE},
  {EG_K_TUBE_BETA,    IN_L_TBB, IN_H_TBB, "0", nullptr, 0, 0, 0, corium::P_BOOL, corium::P_INLINE},
  {EG_K_TUBE_GAMMA,   IN_L_TBG, IN_H_TBG, "0", nullptr, 0, 0, 0, corium::P_BOOL, corium::P_INLINE},
#endif
  // Conversion and alarms
  {K_DOSE_HDR,        IN_L_DOSE, nullptr, nullptr, nullptr, 0, 0, 0, corium::P_HEADER, 0},
  {EG_K_RATIO,        CT_L_RAT, CT_H_RAT, STR(GEIGER_RATIO), nullptr, 1, 100000, 0, corium::P_FLOAT, 0},
  {K_WARN,            CT_L_WRN, CT_H_WRN, STR(GEIGER_WARN_CPM), nullptr, 0, 9999, 0,  corium::P_UINT,   0},
  {K_ALERT,           CT_L_ALR, CT_H_ALR, STR(GEIGER_ALERT_CPM), nullptr, 0, 9999, 0,  corium::P_UINT,   0},
  {K_LIFE_EN,         CT_L_LE,  CT_H_LE,  "1",     nullptr, 0, 1,    0,  corium::P_BOOL,   corium::P_ADVANCED},
  // Signal conditioning, all advanced
  {K_COUNT_HDR,       IN_L_COUNT, nullptr, nullptr, nullptr, 0, 0, 0, corium::P_HEADER, corium::P_ADVANCED},
#ifdef PCNT_SELECTABLE
  {K_COUNT_MODE,      IN_L_CNT, IN_H_CNT, COUNT_MODE_DEF, IN_O_CNT, 0, 1, 0, corium::P_ENUM, corium::P_ADVANCED | corium::P_REBOOT},
#endif
#ifdef NO_PCNT_ENV
  {K_COUNT_KEPT,      nullptr,  nullptr,  "0", nullptr, 0, 1, 0, corium::P_BOOL, corium::P_HIDDEN | corium::P_LOCAL},
#endif
#ifdef USE_PCNT
  {K_PCNT_FILTER,     IN_L_PCF, IN_H_PCF, STR(PCNT_FILTER), nullptr, 0, 1023, 0, corium::P_UINT, corium::P_ADVANCED},
  {K_PCNT_PULL,       IN_L_PCP, nullptr,  STR(PCNT_PIN_PULL_DEFAULT), IN_O_PCP, 0, 2, 0, corium::P_ENUM, corium::P_ADVANCED},
#endif
#ifdef PREF_DEBOUNCE
  {K_DEBOUNCE,       IN_L_DBN, IN_H_DBN, STR(GEIGER_DEBOUNCE), nullptr, 0, 10000, 0, corium::P_UINT, corium::P_ADVANCED},
#endif
#ifdef PREF_PULSE_WIDTH
  {K_PULSE_WIDTH_US,  IN_L_PWU, IN_H_PWU, STR(GEIGER_PULSE_WIDTH), nullptr, 10, 2000, 0, corium::P_UINT, corium::P_ADVANCED},
#endif
  // CPM calculation
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  {K_CPM_WINDOW,      IN_L_CPW, IN_H_CPW, "30", nullptr, 1, 60, 0, corium::P_UINT, corium::P_SLIDER | corium::P_ADVANCED | corium::P_REBOOT},
#endif
  {K_CPM_MODE,        IN_L_CMM, nullptr, "3", IN_O_CMM, 0, 4, 0, corium::P_ENUM, corium::P_ADVANCED},
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  {K_DEAD_TIME_US,    IN_L_DTU, IN_H_DTU, STR(GEIGER_DEAD_TIME_DEFAULT), nullptr, 0, 1000, 0, corium::P_UINT, corium::P_ADVANCED},
#endif
};

static const corium::PrefGroup COUNTER_PREF_GROUP PROGMEM = {
  "counter", "Counter", 1,
  COUNTER_PREF_ITEMS,
  sizeof(COUNTER_PREF_ITEMS) / sizeof(COUNTER_PREF_ITEMS[0]),
  corium::P_CAT_INPUT,
};

const corium::PrefGroup* CounterPrefs::prefsGroup() { return &COUNTER_PREF_GROUP; }

#ifdef PCNT_SELECTABLE
// rows per count mode
CORIUM_PSTR(IN_W_CNT, "counter.count_mode");
static const uint16_t SHOW_PCNT = 1u << 0;
static const uint16_t SHOW_ISR  = 1u << 1;

static const corium::PrefShow COUNTER_SHOW[] PROGMEM = {
  {K_PCNT_FILTER, IN_W_CNT, SHOW_PCNT},
  {K_DEBOUNCE,    IN_W_CNT, SHOW_ISR},
  {},
};

const corium::PrefShow* CounterPrefs::prefsShow() { return COUNTER_SHOW; }
#endif

// pins and count mode read at boot only, the isr and port keep theirs
#if defined(PREF_RX_PIN) || defined(PCNT_SELECTABLE) || defined(PREF_TX_PIN)
static void boot_pins() {
  static bool s_set = false;
  if (s_set) return;
  s_set = true;
#ifdef PCNT_SELECTABLE
  gcounter.input()->use_pcnt(corium::Prefs::getUInt("counter", K_COUNT_MODE) == 0);
#endif
#ifdef PREF_RX_PIN
  gcounter.set_rx_pin((int)corium::Prefs::getInt("counter", K_RX_PIN));
#endif
#ifdef PREF_TX_PIN
  gcounter.set_tx_pin((int)corium::Prefs::getInt("counter", K_TX_PIN));
#endif
}
#else
static void boot_pins() {}   // none on this build
#endif

#ifdef NO_PCNT_ENV
// count_mode into the file once, so a plain bin keeps the interrupt; boot only, a save's callback can't commit
static void keep_count_mode() {
  static bool s_done = false;
  if (s_done) return;
  s_done = true;
  if (corium::Prefs::getBool("counter", K_COUNT_KEPT)) return;
  corium::Prefs::put("counter", K_COUNT_KEPT, "1");
  corium::Prefs::commit(false);
}
#endif

void CounterPrefs::onPrefsLoaded() {
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
  _serial_type = (uint8_t)corium::Prefs::getUInt("counter", EG_K_SERIAL_TYPE);
  uint8_t window = (uint8_t)corium::Prefs::getUInt("counter", K_CPM_WINDOW);
  if (SerialFormat::has_cps(_serial_type)) window = GEIGER_CPM_COUNT;
  gcounter.set_cpm_window(window);
#endif
  boot_pins();
#ifdef NO_PCNT_ENV
  keep_count_mode();
#endif
#ifdef USE_PCNT
  gcounter.set_pcnt_filter((int)corium::Prefs::getUInt("counter", K_PCNT_FILTER));
  gcounter.apply_pcnt_filter();
  gcounter.set_pin_pull((int)corium::Prefs::getUInt("counter", K_PCNT_PULL));
#endif
#ifdef PREF_DEBOUNCE
  gcounter.set_debounce((int)corium::Prefs::getUInt("counter", K_DEBOUNCE));
#endif
#ifdef PREF_PULSE_WIDTH
  set_pulse_width_us((int)corium::Prefs::getUInt("counter", K_PULSE_WIDTH_US));
#endif
#ifdef PREF_MODEL
  {
    const char* m = corium::Prefs::getString("counter", EG_K_GEIGER_MODEL);
    if (m[0] != '\0') Tube::set_model(m);
  }
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE)
  {
    uint32_t dead = corium::Prefs::getUInt("counter", K_DEAD_TIME_US);
#ifdef PREF_DEBOUNCE
    // interrupt: nothing counts inside the debounce either, 0 stays off
    uint32_t dbn = corium::Prefs::getUInt("counter", K_DEBOUNCE);
    if (dead && dbn > dead && !gcounter.has_pcnt()) dead = dbn;
#endif
    gcounter.set_dead_time_us((uint16_t)dead);
  }
#endif
  gcounter.set_cpm_mode((uint8_t)corium::Prefs::getUInt("counter", K_CPM_MODE));
  gcounter.set_ratio(corium::Prefs::getFloat("counter", EG_K_RATIO));
  gcounter.set_warning((int)corium::Prefs::getUInt("counter", K_WARN));
  gcounter.set_alert((int)corium::Prefs::getUInt("counter", K_ALERT));
  gcounter.set_lifetime_enabled(corium::Prefs::getUInt("counter", K_LIFE_EN) != 0);
}

// pins, serial type and cpm window are corium::P_REBOOT
void CounterPrefs::onPrefsSaved() {
  onPrefsLoaded();
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  udprx_notify_prefs_saved();
#endif
}

// legacy import
static const corium::LegacyAlias COUNTER_LEGACY[] PROGMEM = {
  {"geigerRatio", "ratio"},
  {"geigerWarn",  "warn"},
  {"geigerAlert", "alert"},
#ifdef PREF_RX_PIN
  {"geigerRX", "rx_pin"},
#endif
#ifdef PREF_TX_PIN
  {"geigerTX", "tx_pin"},
#endif
#ifdef USE_PCNT
  {"pcntFilter",    "pcnt_filter"},
  {"pcntPull",      "pcnt_pull"},
#endif
#ifdef PREF_DEBOUNCE
  {"geigerDebounce","debounce"},
#endif
  {},
};
corium::LegacyRows CounterPrefs::legacyAliases() { return COUNTER_LEGACY; }

// Lifetime prefs, own group

class LifePrefs : public corium::Module {
public:
  const char* name() override { return PSTR("life"); }
  uint8_t displayOrder() override { return 0; }
  const corium::PrefGroup* prefsGroup() override;
  void onPrefsLoaded() override;
  void onPrefsSaved() override {}   // live counts stay
};

static LifePrefs lifeprefs;
CORIUM_REGISTER_MODULE(lifeprefs)

static const corium::Pref LIFE_PREF_ITEMS[] PROGMEM = {
  {EG_K_CLK,  nullptr, nullptr, "0", nullptr, 0, 0, CORIUM_U32_MAX_LEN, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL | corium::P_KEEP},
  {EG_K_CLKR, nullptr, nullptr, "0", nullptr, 0, 0, CORIUM_U32_MAX_LEN, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL | corium::P_KEEP},
  {EG_K_FBT,  nullptr, nullptr, "0", nullptr, 0, 0, CORIUM_U32_MAX_LEN, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL | corium::P_KEEP},
  {EG_K_SECS, nullptr, nullptr, "0", nullptr, 0, 0, CORIUM_U32_MAX_LEN, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL | corium::P_KEEP},
};

static const corium::PrefGroup LIFE_PREF_GROUP PROGMEM = {
  "life", "Lifetime", 1,
  LIFE_PREF_ITEMS,
  sizeof(LIFE_PREF_ITEMS) / sizeof(LIFE_PREF_ITEMS[0]),
};

const corium::PrefGroup* LifePrefs::prefsGroup() { return &LIFE_PREF_GROUP; }

void LifePrefs::onPrefsLoaded() {
  gcounter.set_lifetime_clicks(corium::Prefs::getUInt("life", EG_K_CLK));
  gcounter.set_lifetime_rollover(corium::Prefs::getUInt("life", EG_K_CLKR));
  gcounter.set_first_boot_ts(corium::Prefs::getUInt("life", EG_K_FBT));
  gcounter.set_lifetime_seconds(corium::Prefs::getUInt("life", EG_K_SECS));
}

// default for led.pin, -1 = none
#ifndef LED_SEND_RECEIVE
#if defined(ESP8266) || defined(CONFIG_IDF_TARGET_ESP32)
#define LED_SEND_RECEIVE 2
#else
#define LED_SEND_RECEIVE -1   // C3/S3 boards have no plain LED on 2
#endif
#endif
#ifdef ESP8266
#define LED_PIN_BLOCKED   // every esp8266 module has it on 2
#endif
#ifndef LED_SEND_RECEIVE_ON
#ifdef ESP8266
#define LED_SEND_RECEIVE_ON LOW
#else
#define LED_SEND_RECEIVE_ON HIGH
#endif
#endif

#if !defined(DISABLE_INTERNAL_BLIP) || defined(GEIGER_BLIPLED)
#define LED_BLIP
#endif

// every build, led.pin, the blink on a send, the alert flashes
class LedPrefs : public corium::Module {
public:
  const char* name() override { return PSTR("led"); }
  uint8_t displayOrder() override { return 14; }
  const corium::PrefGroup* prefsGroup() override;
  void onPrefsLoaded() override;
  void begin() override {
    corium::Registry::on(TOPIC_SENT, this);
    corium::Registry::on(Counter::TOPIC_ALARM, this);
  }
  void loop(unsigned long now) override;
private:
  static constexpr uint32_t ALERT_MS = 1000;
  bool _alert = false;
  uint32_t _alert_at = 0;
};

// alert, not snoozed: the pattern each second
void LedPrefs::loop(unsigned long now) {
  if (corium::Registry::events(this) & (1u << TOPIC_SENT)) corium::LedSignal::play(LED_ACTIVITY);
  if (!gcounter.is_alert() || gcounter.is_snoozed()) {
    _alert = false;
    return;
  }
  if (!_alert) {
    _alert = true;
    _alert_at = now;
  }
  if (corium::dueEvery(now, _alert_at, ALERT_MS)) corium::LedSignal::play(LED_ALERT);
  corium::Registry::dueAt(this, _alert_at);
}

static LedPrefs ledprefs;
CORIUM_REGISTER_MODULE(ledprefs)

void led_hold_tick() {
#ifdef LED_PIN_BLOCKED
  // the board's pin: up before Boot::end
  static bool s_attached = false;
  if (!s_attached) {
    s_attached = true;
    corium::LedSignal::attach();
  }
  corium::LedSignal::play(LED_ACK);
#endif
}

#ifndef LED_PIN_BLOCKED
CORIUM_PSTR(LD_L_PIN, "Status LED pin");
CORIUM_PSTR(LD_H_PIN, "GPIO of the onboard LED (-1 = disabled)");
#if LED_SEND_RECEIVE_ON == LOW
#define LD_POL_DEF "1"
#else
#define LD_POL_DEF "0"
#endif
#endif
#ifdef LED_BLIP
CORIUM_PSTR(LD_L_BLP, "Blip LED");
CORIUM_PSTR(LD_H_BLP, "Flash LED on each count");
#ifdef GEIGER_BLIPLED
CORIUM_PSTR(LD_L_BRT, "Internal brightness");
#else
CORIUM_PSTR(LD_L_BRT, "Blip brightness");
#endif
CORIUM_PSTR(LD_H_BRT, "LED brightness (0-100%)");
CORIUM_PSTR(LD_O_MOD, "Pulse|Fade");
#ifdef ESPGEIGER_HW
#define LD_PULSE_DEF "2"
#define LD_BRT_DEF   "100"
#else
#define LD_PULSE_DEF "20"
#define LD_BRT_DEF   "80"
#endif
// not the 8266 HW's inside LED, hw32 has the beeper
#if !defined(ESPGEIGER_HW) || defined(ESPGEIGER_HW32)
#define LED_QUIET
CORIUM_PSTR(LD_H_QFR, "Silence blip LED + beeper from (blank = off), alarms still sound");
#endif
#define LED_LABEL  "Blip LED"
#define LED_ENABLE K_BLIP_LED
#else
#define LED_LABEL  "Status LED"
#define LED_ENABLE nullptr
#endif

static const corium::Pref LED_PREF_ITEMS[] PROGMEM = {
#ifdef LED_BLIP
  {K_BLIP_LED,      LD_L_BLP, LD_H_BLP, "1",    nullptr, 0, 1,     0, corium::P_BOOL, 0},
  {EG_K_MODE,       EG_L_MOD, nullptr,  "0",     LD_O_MOD, 0,  1,     0, corium::P_ENUM, corium::P_ADVANCED},
  {EG_K_PULSE_MS,   EG_L_PUL, EG_H_PUL, LD_PULSE_DEF, nullptr, 1, 50,  0, corium::P_UINT, corium::P_ADVANCED},
  {EG_K_FADE_SHIFT, EG_L_FAD, nullptr,  "1",     EG_O_FAD, 0,  2,     0, corium::P_ENUM, corium::P_ADVANCED},
  {K_BLIP_BRIGHT,   LD_L_BRT, LD_H_BRT, LD_BRT_DEF, nullptr, 0, 100,  0, corium::P_UINT, corium::P_SLIDER},
#ifdef LED_QUIET
  {EG_K_QUIET_FROM, EG_L_QFR, LD_H_QFR, "",      nullptr, 0,   0,     CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
  {EG_K_QUIET_TO,   EG_L_QTO, EG_H_QTO, "",      nullptr, 0,   0,     CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
#endif
#endif
#ifndef LED_PIN_BLOCKED
  {EG_K_PIN,        LD_L_PIN, LD_H_PIN, STR(LED_SEND_RECEIVE), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
  {EG_K_POLARITY,   EG_L_POL, nullptr, LD_POL_DEF, EG_O_POL, 0, 1, 0, corium::P_ENUM, corium::P_ADVANCED | corium::P_REBOOT},
#endif
};

static const corium::PrefGroup LED_PREF_GROUP PROGMEM = {
  "led", LED_LABEL, 1,
  LED_PREF_ITEMS,
  sizeof(LED_PREF_ITEMS) / sizeof(LED_PREF_ITEMS[0]),
  corium::P_CAT_SYSTEM, LED_ENABLE, 0,
};

const corium::PrefGroup* LedPrefs::prefsGroup() { return &LED_PREF_GROUP; }

void LedPrefs::onPrefsLoaded() {
  // boot only, attach() takes it once
  static bool s_pin_read = false;
  if (!s_pin_read) {
    s_pin_read = true;
#ifdef LED_PIN_BLOCKED
    corium::LedSignal::usePin(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON == LOW);
#else
    corium::LedSignal::usePin((int)corium::Prefs::getInt("led", EG_K_PIN),
                       corium::Prefs::getUInt("led", EG_K_POLARITY) == 1);
#endif
  }
#ifdef LED_BLIP
  gcounter.set_blip_led(corium::Prefs::getBool("led", K_BLIP_LED));
  uint32_t blip_pct = corium::Prefs::getUInt("led", K_BLIP_BRIGHT);
  uint32_t blip_level = brightnessCurve(blip_pct, 255);
  if (blip_pct && !blip_level) blip_level = 1;
  gcounter.set_blip_brightness((uint8_t)blip_level);
  // UI is Pulse|Fade
  bool fade            = (corium::Prefs::getUInt("led", EG_K_MODE) == 1);
  uint8_t engine_mode  = fade ? corium::PulseEngine::MODE_FADE : corium::PulseEngine::MODE_PULSE;
  uint8_t fade_idx     = (uint8_t)corium::Prefs::getUInt("led", EG_K_FADE_SHIFT);
  uint16_t pulse_us    = (uint16_t)(corium::Prefs::getUInt("led", EG_K_PULSE_MS) * 1000);
#ifdef LED_FLASH_ENGINE
  BlipLed::setConfig(engine_mode, pulse_us, fade_idx, 0);
#else
  corium::LedSignal::setFlashConfig(engine_mode, pulse_us, fade_idx);
#endif
#ifdef LED_QUIET
  gcounter.set_quiet_hours(
    corium::Prefs::getString("led", EG_K_QUIET_FROM),
    corium::Prefs::getString("led", EG_K_QUIET_TO)
  );
#endif
#endif
}

#if defined(ESPG_HV) && !defined(ESPGEIGER_HW)
#include "HV/HV.h"

class HVPinPrefs : public corium::Module {
public:
  const char* name() override { return PSTR("hvpin"); }
  bool coreModule() override { return true; }   // the pin parks in safe mode
  uint8_t displayOrder() override { return 14; }
  const corium::PrefGroup* prefsGroup() override;
  void onPrefsLoaded() override;
};

static HVPinPrefs hvpinprefs;
CORIUM_REGISTER_MODULE(hvpinprefs)

CORIUM_PSTR(HV_L_PWM, "HV PWM pin");
CORIUM_PSTR(HV_H_PWM, "GPIO for HV generator PWM (-1 = disabled)");

static const corium::Pref HV_PIN_PREF_ITEMS[] PROGMEM = {
  {K_PWM_PIN, HV_L_PWM, HV_H_PWM, STR(GEIGER_PWMPIN), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_REBOOT | corium::P_PIN_OUT | corium::P_PIN_PWM},
};

static const corium::PrefGroup HV_PIN_PREF_GROUP PROGMEM = {
  "hvpin", "HV Hardware", 1,
  HV_PIN_PREF_ITEMS,
  sizeof(HV_PIN_PREF_ITEMS) / sizeof(HV_PIN_PREF_ITEMS[0]),
  corium::P_CAT_SYSTEM | corium::P_CAT_BOARD, nullptr, 0,
};

const corium::PrefGroup* HVPinPrefs::prefsGroup() { return &HV_PIN_PREF_GROUP; }

// boot only, live -1 leaves pwm unguarded
void HVPinPrefs::onPrefsLoaded() {
  static bool s_set = false;
  if (s_set) return;
  s_set = true;
  int pin = (int)corium::Prefs::getInt("hvpin", K_PWM_PIN);
  hv.set_pwm_pin(pin);
  corium::Log::console(PSTR("HV: pwm_pin=%d"), pin);
}
#endif
