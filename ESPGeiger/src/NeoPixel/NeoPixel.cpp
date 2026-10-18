/*
  NeoPixel.cpp - functions to handle NeoPixel

  Copyright (C) 2023 @steadramon

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
#ifdef GEIGER_NEOPIXEL

#include <Arduino.h>
#include "NeoPixel.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/System/PinSafety.h"

EG_PSTR(K_COLOR, "color");
EG_PSTR(K_FADE,  "fade");
EG_PSTR(K_SWAP,  "swap");

#define NPX_STR_(x) #x
#define NPX_STR(x) NPX_STR_(x)

NeoPixel neopixel;
EG_REGISTER_MODULE(neopixel)

EG_PSTR(NP_L_SWAP, "Swap R/G");
EG_PSTR(NP_H_SWAP, "Enable if green appears as red (RGB-ordered LED chips).");
EG_PSTR(NP_O_MODE, "Off|Blip|Blip+status|Trend pulse|Trend+status");
EG_PSTR(NP_L_COL,  "Blip colour");
EG_PSTR(NP_H_BLP,  "Blip mode only.");
EG_PSTR(NP_O_COL,  "Green|Red|Blue|Yellow|Cyan|Magenta|White|Orange");
EG_PSTR(NP_O_FAD,  "Off|Fast|Medium|Slow");
#ifndef NEOPIXEL_PIN_BLOCKED
EG_PSTR(NP_L_PIN,  "Pin");
EG_PSTR(NP_H_PIN,  "WS2812 data pin. -1 to disable. Reboot to apply.");
#endif

// esp8266 boards have RGB leds, swap on
#ifndef NPX_DEFAULT_SWAP
  #if defined(ESP32)
    #define NPX_DEFAULT_SWAP "0"
  #else
    #define NPX_DEFAULT_SWAP "1"
  #endif
#endif

static const EGPref NEOPIXEL_PREF_ITEMS[] PROGMEM = {
#ifndef NEOPIXEL_PIN_BLOCKED
  {EG_K_PIN,        NP_L_PIN,  NP_H_PIN,  NPX_STR(NEOPIXEL_PIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, EGP_ADVANCED | EGP_REBOOT | EGP_PIN_OUT},
#endif
  {EG_K_BRIGHTNESS, EG_L_BRT,  nullptr,   "15",              nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
  {EG_K_MODE,       EG_L_MOD,  nullptr,   "4",               NP_O_MODE, 0, 4, 0, EGP_ENUM, 0},
  {K_COLOR,         NP_L_COL,  NP_H_BLP,  "0",               NP_O_COL, 0, 7,  0, EGP_ENUM, 0},
  {K_FADE,          EG_L_FAD,  NP_H_BLP,  "0",               NP_O_FAD, 0, 3,  0, EGP_ENUM, EGP_ADVANCED},
  {K_SWAP,          NP_L_SWAP, NP_H_SWAP, NPX_DEFAULT_SWAP,  nullptr, 0, 0,   0, EGP_BOOL, EGP_ADVANCED},
};

static const EGPrefGroup NEOPIXEL_PREF_GROUP PROGMEM = {
  "neopixel", "NeoPixel", 1,
  NEOPIXEL_PREF_ITEMS,
  sizeof(NEOPIXEL_PREF_ITEMS) / sizeof(NEOPIXEL_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* NeoPixel::prefs_group() { return &NEOPIXEL_PREF_GROUP; }

// bit 2 R, 1 G, 0 B
static const uint8_t NAMED_MASK[8] PROGMEM = {
  0b010, 0b100, 0b001, 0b110, 0b011, 0b101, 0b111, 0b100,
};

void NeoPixel::precompute_blip() {
  uint8_t mask = pgm_read_byte(&NAMED_MASK[_blip_colour_idx]);
  _blip_r = (mask & 0b100) ? colorSaturation : 0;
  _blip_g = (mask & 0b010) ? colorSaturation : 0;
  _blip_b = (mask & 0b001) ? colorSaturation : 0;
  if (_blip_colour_idx == 7) _blip_g = colorSaturation >> 1;  // orange
}

#ifndef NEOPIXEL_PIN_BLOCKED
static int8_t claim_pin(int p) {
  if (p < 0) return -1;
  if (const char* why = PinSafety::claim_output(p, PSTR("NeoPx"))) {
    Log::console(PSTR("NeoPixel: pin=%d unsafe (%s) - disabled"), p, why);
    return -1;
  }
  return (int8_t)p;
}
#endif

void NeoPixel::on_prefs_loaded() {
#ifndef NEOPIXEL_PIN_BLOCKED
  // boot only, the controller keeps its pin
  static bool s_pin_read = false;
  if (!s_pin_read) {
    s_pin_read = true;
    _pin = claim_pin((int)EGPrefs::getInt("neopixel", EG_K_PIN));
    // setup() ran before EGPrefs::begin
    if (!controller_ && _pin >= 0) setup();
  }
#endif
  int b = (int)EGPrefs::getUInt("neopixel", EG_K_BRIGHTNESS);
  setBrightness(b);
  _swap_rg = EGPrefs::getBool("neopixel", K_SWAP);
  uint32_t m = EGPrefs::getUInt("neopixel", EG_K_MODE);
  neoPixelMode = m > 4 ? 4 : m;
  uint32_t c = EGPrefs::getUInt("neopixel", K_COLOR);
  _blip_colour_idx = c > 7 ? 0 : (uint8_t)c;
  uint32_t f = EGPrefs::getUInt("neopixel", K_FADE);
  _fade_rate = f > 3 ? 0 : (uint8_t)f;
  precompute_blip();
  bool active = (b > 0) && (neoPixelMode > 0) && (_pin >= 0);
  bool fast_loop = (neoPixelMode == 1) && (_fade_rate > 0);
  EGModuleRegistry::set_loop_interval(this, active ? (fast_loop ? 10 : 30) : -1);
}

RgbColor NeoPixel::color(uint8_t r, uint8_t g, uint8_t b) {
  return _swap_rg ? RgbColor(g, r, b) : RgbColor(r, g, b);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NEOPIXEL_LEGACY[] PROGMEM = {
  {"neopixelBrightness", "brightness"},
  {},
};
EGLegacyRows NeoPixel::legacy_aliases() { return NEOPIXEL_LEGACY; }
// === END LEGACY IMPORT ===

NeoPixel::NeoPixel() {
}

void NeoPixel::setup()
{
  if (_pin < 0) return;
  this->controller_ = new NeoController(1, (uint8_t)_pin);
  this->controller_->Begin();
  this->controller_->Show();
}

void NeoPixel::setBrightness(int input) {
  int pct = clamp(input, 0, 100);
  input = (int)brightness_curve((uint32_t)pct, 128);  // 0-128
  if (pct && !input) input = 1;
  if (input == 0 && controller_) {
    RgbColor black(0);
    this->controller_->SetPixelColor(0, black);
    this->controller_->Show();
  }
  colorSaturation = input;
}

// 3 and 4: blink faster when rising, colour by trend
RgbColor NeoPixel::trend_color()
{
  uint8_t s = (uint8_t)colorSaturation;
  float cpm = gcounter.get_cpmf();
  float cpm5 = gcounter.get_cpm5f();
  if (cpm5 <= 0 || cpm <= 0) {
    nextInterval = blinkInterval;
    return color(0, 0, s);                         // no data: blue
  }
  nextInterval = clamp((unsigned long)(blinkInterval * cpm5 / cpm), 100UL, 4000UL);
  float z = poisson_z(cpm, cpm5);
  if (z > 3.0f)  return color(s, 0, 0);            // rise: red
  if (z > 1.5f)  return color(s, s, 0);            // rising: yellow
  if (z < -1.5f) return color(s, 0, s);            // dropping: purple
  return color(0, s, 0);                           // stable: green
}

// 2 and 4: alarms win, blue when quiet
RgbColor NeoPixel::alarm_color(RgbColor rgb)
{
  uint8_t s = (uint8_t)colorSaturation;
  if (gcounter.is_alert())   return color(s, 0, 0);
  if (gcounter.is_warning()) return color(s, s, 0);
  if (gcounter.get_cpmf() < 0.01f && gcounter.get_cpm5f() < 0.01f) return color(0, 0, s);
  return rgb;
}

void NeoPixel::blink(uint16_t timer)
{
  if (colorSaturation == 0 || neoPixelMode == 0 || !controller_) {
    return;
  }
  RgbColor rgb;
  if (this->neoPixelMode == 1) {
    rgb = color(_blip_r, _blip_g, _blip_b);
  } else if (this->neoPixelMode >= 3) {
    rgb = trend_color();
  } else {
    rgb = color(0, colorSaturation, 0);
  }
  if (this->neoPixelMode == 2 || this->neoPixelMode == 4) rgb = alarm_color(rgb);

  this->controller_->SetPixelColor(0, rgb);
  this->controller_->Show();
  _is_off = false;
  onTime = fast_millis();
  offTime = timer;
}

void NeoPixel::loop(unsigned long now)
{
  if (colorSaturation == 0 || !controller_) {
    return;
  }
  if (!_is_off) {
    unsigned long elapsed = now - onTime;
    if (elapsed >= offTime) {
      RgbColor black(0);
      this->controller_->SetPixelColor(0, black);
      this->controller_->Show();
      _is_off = true;
    } else if (this->neoPixelMode == 1 && _fade_rate > 0) {
      // hold at peak, then gamma-2 decay
      const uint16_t FADE_HOLD_MS = 20;
      if (elapsed >= FADE_HOLD_MS && offTime > FADE_HOLD_MS) {
        uint32_t fe = elapsed - FADE_HOLD_MS;
        uint32_t fw = offTime - FADE_HOLD_MS;
        uint32_t lin = ((fw - fe) * 256u) / fw;
        uint32_t scale = (lin * lin) >> 8;
        RgbColor f(
          (uint8_t)((uint32_t)_blip_r * scale >> 8),
          (uint8_t)((uint32_t)_blip_g * scale >> 8),
          (uint8_t)((uint32_t)_blip_b * scale >> 8));
        this->controller_->SetPixelColor(0, f);
        this->controller_->Show();
      }
    }
  }
  if (this->neoPixelMode <= 2) {
    if (last_blip != gcounter.last_blip()) {
      last_blip = gcounter.last_blip();
      static const uint16_t FADE_MS[4] PROGMEM = {20, 80, 150, 300};
      blink(this->neoPixelMode == 1 ? pgm_read_word(&FADE_MS[_fade_rate]) : 20);
    }
  } else {
    if (now - onTime >= nextInterval) {
      blink(20);
    }
  }
}
#endif
