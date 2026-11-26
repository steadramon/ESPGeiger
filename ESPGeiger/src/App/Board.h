/*
  Board.h - which revision of the board this is, read once at boot.

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
#ifndef BOARD_H
#define BOARD_H

// Board names, not the chip. setup() hands them to Corium
#if defined(ESPGEIGER_HW32)
  #define APP_HOME_NAME "ESPGeiger-HW32"
  #define APP_BOARD "ESPG-HW32"
#elif defined(ESPGEIGER_HW)
  #ifndef ESP8266
    #error "ESPGEIGER_HW on an ESP32 is the HW32: add -DESPGEIGER_HW32"
  #endif
  #define APP_HOME_NAME "ESPGeiger-HW"
  #define APP_BOARD "ESPG-HW"
#elif defined(ESPGEIGER_LT)
  #define APP_HOME_NAME "ESPGeiger-Log"
  #if defined(ESP8266) && defined(GEIGER_SDCARD)
    #define APP_BOARD "ESPG-LOG"
  #elif defined(ESP8266)
    #define APP_BOARD "ESPG-LT"
  #endif
#endif

#include <stdint.h>
#include "Corium/System/PinSafety.h"
#include "GeigerInput/GeigerInput.h"
#if defined(ESPGEIGER_HW) || defined(ESPG_HV_ADC)
#include "HV/HV.h"
#endif
#if defined(GEIGER_PUSHBUTTON) && defined(BTN_PIN_BLOCKED)
#include "PushButton/PushButton.h"
#endif
#if defined(GEIGER_NEOPIXEL) && defined(NEOPIXEL_PIN_BLOCKED)
#include "NeoPixel/NeoPixel.h"
#endif
#ifdef I2C_PINS_FIXED
#include "Corium/System/I2CBus.h"
#endif
#if defined(AUDIO_TICK) && defined(AUDIO_TICK_PINS_BLOCKED)
#include "AudioTick/AudioTick.h"
#endif
#ifdef ESPGEIGER_HW32
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/PinProbe.h"
#include "Corium/Output/Buzzer.h"
#endif

namespace Board {

  constexpr int8_t UART0_TX = 1;   // esp8266 + esp32
  constexpr int8_t UART0_RX = 3;

  static const char P_HV[]    PROGMEM = "HV gate";
  static const char P_HVFB[]  PROGMEM = "HV feedback";
  static const char P_RX[]    PROGMEM = "RX";
  static const char P_TX[]    PROGMEM = "TX";
  static const char P_BLIP[]  PROGMEM = "Blip LED";
  static const char P_NEOPX[] PROGMEM = "NeoPx";
  static const char P_SD[]    PROGMEM = "SD";
  static const char P_BTN[]   PROGMEM = "Btn";
  static const char P_UART0[] PROGMEM = "UART0";
  static const char P_BUZZ[]  PROGMEM = "Buzzer";
  static const char P_I2S[]   PROGMEM = "AudioI2S";
  static const char P_MUTE[]  PROGMEM = "AudioMute";

  // Pins wired on the board, the owner is filled in as each module claims one
  constexpr corium::PinSafety::Fixed PINS[] PROGMEM = {
#ifdef ESPGEIGER_HW
    {P_HV, GEIGER_PWMPIN, corium::PinSafety::USE_OUT},
#endif
#if defined(ESPG_HV_ADC) && !defined(ESP8266)
    {P_HVFB, GEIGER_VFEEDBACKPIN, corium::PinSafety::USE_IN},   // esp8266 A0 isn't a gpio
#endif
#if !GEIGER_IS_UDPRX(GEIGER_TYPE)
#if defined(RXPIN_BLOCKED) && !defined(RXPIN_UNUSED)
    {P_RX, GEIGER_RXPIN, corium::PinSafety::USE_IN},
#endif
#if defined(TXPIN_BLOCKED) && defined(GEIGER_TXPIN)
    {P_TX, GEIGER_TXPIN, corium::PinSafety::USE_OUT},
#endif
#endif
#ifdef GEIGER_BLIPLED
    {P_BLIP, GEIGER_BLIPLED, corium::PinSafety::USE_OUT},
#endif
#if defined(GEIGER_NEOPIXEL) && defined(NEOPIXEL_PIN_BLOCKED)
    {P_NEOPX, NEOPIXEL_PIN, corium::PinSafety::USE_OUT},
#endif
#if defined(GEIGER_SDCARD) && defined(GEIGER_SDCARD_SCK)
    // the board's own spi pins
    {P_SD, GEIGER_SDCARD_MISO, corium::PinSafety::USE_IN},
    {P_SD, GEIGER_SDCARD_MOSI, corium::PinSafety::USE_OUT},
    {P_SD, GEIGER_SDCARD_SCK, corium::PinSafety::USE_OUT},
    {P_SD, GEIGER_SDCARD_CS, corium::PinSafety::USE_OUT},
#elif defined(GEIGER_SDCARD)
    {P_SD, MISO, corium::PinSafety::USE_IN},
    {P_SD, MOSI, corium::PinSafety::USE_OUT},
    {P_SD, SCK, corium::PinSafety::USE_OUT},
    {P_SD, GEIGER_SDCARD_CS, corium::PinSafety::USE_OUT},
#endif
#if defined(GEIGER_PUSHBUTTON) && defined(BTN_PIN_BLOCKED) && defined(I2C_PINS_FIXED)
    {P_BTN, PUSHBUTTON_PIN, corium::PinSafety::USE_BUTTON},
#if PUSHBUTTON_PIN2 >= 0
    {P_BTN, PUSHBUTTON_PIN2, corium::PinSafety::USE_BUTTON},
#endif
#endif
#ifdef GEIGER_BUZZER
    {P_BUZZ, GEIGER_BUZZER, corium::PinSafety::USE_OUT},
#endif
#if defined(AUDIO_TICK) && defined(AUDIO_TICK_PINS_BLOCKED)
    {P_I2S, AUDIO_TICK_BCLK, corium::PinSafety::USE_OUT},
    {P_I2S, AUDIO_TICK_WS, corium::PinSafety::USE_OUT},
    {P_I2S, AUDIO_TICK_DOUT, corium::PinSafety::USE_OUT},
#endif
#if defined(AUDIO_TICK) && defined(AUDIO_MUTE_PIN)
    {P_MUTE, AUDIO_MUTE_PIN, corium::PinSafety::USE_IN},
#endif
#ifdef I2C_PINS_FIXED
    {corium::PinSafety::I2C, I2C_DEFAULT_SDA, corium::PinSafety::USE_OUT},
    {corium::PinSafety::I2C, I2C_DEFAULT_SCL, corium::PinSafety::USE_OUT},
#endif
    {nullptr, -1, corium::PinSafety::USE_OUT},
  };

  // A pref may still use these, so they're only noted
  constexpr corium::PinSafety::Fixed SHARED[] PROGMEM = {
#if defined(GEIGER_PUSHBUTTON) && defined(BTN_PIN_BLOCKED) && !defined(I2C_PINS_FIXED)
    {P_BTN, PUSHBUTTON_PIN, corium::PinSafety::USE_BUTTON},
#if PUSHBUTTON_PIN2 >= 0
    {P_BTN, PUSHBUTTON_PIN2, corium::PinSafety::USE_BUTTON},
#endif
#endif
#if defined(ESP8266) || defined(CONFIG_IDF_TARGET_ESP32)
    {P_UART0, UART0_TX, corium::PinSafety::USE_OUT},
#ifndef DISABLE_SERIALRX
    {P_UART0, UART0_RX, corium::PinSafety::USE_IN},
#endif
#endif
    {nullptr, -1, corium::PinSafety::USE_OUT},
  };

  // Before prefs load
  inline void claim() {
    corium::PinSafety::reserve(PINS);
    corium::PinSafety::share(SHARED);
  }

#ifdef ESPGEIGER_HW32
  inline uint8_t& rev_slot() { static uint8_t r = 0; return r; }
  inline uint8_t rev() { return rev_slot(); }

  // after the boot button is let go
  inline void detect() {
    // Only rev 2 has the button pull-up. A misread falls back to rev 1, the gentler limits
    rev_slot() = corium::PinProbe::pulled(PUSHBUTTON_PIN) == corium::PinProbe::UP ? 2 : 1;
    pinMode(PUSHBUTTON_PIN, INPUT_PULLUP);
    corium::DeviceInfo::setBoardName(rev_slot() == 2 ? "HW32 v1" : "HW32 Proto");
    corium::Log::console(PSTR("Board: %s"), corium::DeviceInfo::boardName());
  }

  // Proto drives the coil DC: 25 %, 2 s then rest. v1 is AC coupled
  inline corium::Buzzer::Limits buzzer() {
    corium::Buzzer::Limits l = corium::Buzzer::DEFAULTS;
    l.dutyPct = rev() == 2 ? 50 : 25;
    l.runMs = rev() == 2 ? 0 : 2000;
    l.breakMs = 20;
    l.restMs = 2000;
    return l;
  }
#else
  // one revision only
  inline uint8_t rev() { return 0; }
  inline void detect() {}
#endif

}

#endif
