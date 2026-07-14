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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/FastMillis.h"
#include "../Util/MathUtil.h"
#include "../Util/PinSafety.h"

#define _NPX_STR(x) #x
#define NPX_STR(x) _NPX_STR(x)

NeoPixel neopixel;
EG_REGISTER_MODULE(neopixel)

EG_PSTR(NP_L_BRT,  "Brightness");
EG_PSTR(NP_L_SWAP, "Swap R/G");
EG_PSTR(NP_H_SWAP, "Enable if green appears as red (RGB-ordered LED chips).");
EG_PSTR(NP_L_MODE, "Mode");
EG_PSTR(NP_O_MODE, "Off|Blip|Status blip|Trend pulse|Trend+status");
EG_PSTR(NP_L_COL,  "Blip colour");
EG_PSTR(NP_H_COL,  "Blip mode only.");
EG_PSTR(NP_O_COL,  "Green|Red|Blue|Yellow|Cyan|Magenta|White|Orange");
EG_PSTR(NP_L_FAD,  "Fade rate");
EG_PSTR(NP_H_FAD,  "Blip mode only.");
EG_PSTR(NP_O_FAD,  "Off|Fast|Medium|Slow");
#ifndef NEOPIXEL_PIN_BLOCKED
EG_PSTR(NP_L_PIN,  "Pin");
EG_PSTR(NP_H_PIN,  "WS2812 data pin. -1 to disable. Reboot to apply.");
#endif

// Wire format is GRB on the chip side. ESP8266 boards have historically
// shipped with RGB-ordered LEDs, so default the swap on there to preserve
// existing behaviour; ESP32 boards (XH-S3E onboard pixel etc) use the
// standard GRB chip and need no swap.
#ifndef NPX_DEFAULT_SWAP
  #if defined(ESP32)
    #define NPX_DEFAULT_SWAP "0"
  #else
    #define NPX_DEFAULT_SWAP "1"
  #endif
#endif

static const EGPref NEOPIXEL_PREF_ITEMS[] = {
#ifndef NEOPIXEL_PIN_BLOCKED
  {"pin",        NP_L_PIN,  NP_H_PIN,  NPX_STR(NEOPIXEL_PIN), nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT, EGP_ADVANCED},
#endif
  {"brightness", NP_L_BRT,  nullptr,   "15",              nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
  {"mode",       NP_L_MODE, nullptr,   "4",               NP_O_MODE, 0, 4, 0, EGP_ENUM, 0},
  {"color",      NP_L_COL,  NP_H_COL,  "0",               NP_O_COL, 0, 7,  0, EGP_ENUM, 0},
  {"fade",       NP_L_FAD,  NP_H_FAD,  "0",               NP_O_FAD, 0, 3,  0, EGP_ENUM, EGP_ADVANCED},
  {"swap",       NP_L_SWAP, NP_H_SWAP, NPX_DEFAULT_SWAP,  nullptr, 0, 0,   0, EGP_BOOL, EGP_ADVANCED},
};

static const EGPrefGroup NEOPIXEL_PREF_GROUP = {
  "neopixel", "NeoPixel", 1,
  NEOPIXEL_PREF_ITEMS,
  sizeof(NEOPIXEL_PREF_ITEMS) / sizeof(NEOPIXEL_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* NeoPixel::prefs_group() { return &NEOPIXEL_PREF_GROUP; }

// Channel mask per named colour, bit 2=R bit 1=G bit 0=B. 7 of the 8
// entries are pure on/off per channel so the blink path is just a memcpy
// of three precomputed bytes. Orange handled inline as a half-G special.
static const uint8_t NAMED_MASK[8] = {
  0b010, 0b100, 0b001, 0b110, 0b011, 0b101, 0b111, 0b100,
};

void NeoPixel::on_prefs_loaded() {
#ifndef NEOPIXEL_PIN_BLOCKED
  int p = (int)EGPrefs::getInt("neopixel", "pin");
  if (p >= 0) {
    if (const char* why = PinSafety::claim_output(p, PSTR("NeoPx"))) {
      Log::console(PSTR("NeoPixel: pin=%d unsafe (%s) - disabled"), p, why);
      _pin = -1;
    } else {
      _pin = (int8_t)p;
    }
  } else {
    _pin = -1;
  }
  // pre_wifi -> setup() runs before EGPrefs::begin; pick up _pin now.
  if (!controller_ && _pin >= 0) setup();
#endif
  int b = (int)EGPrefs::getUInt("neopixel", "brightness");
  setBrightness(b);
  _swap_rg = EGPrefs::getBool("neopixel", "swap");
  uint32_t m = EGPrefs::getUInt("neopixel", "mode");
  neoPixelMode = m > 4 ? 4 : m;
  uint32_t c = EGPrefs::getUInt("neopixel", "color");
  _blip_colour_idx = c > 7 ? 0 : (uint8_t)c;
  uint32_t f = EGPrefs::getUInt("neopixel", "fade");
  _fade_rate = f > 3 ? 0 : (uint8_t)f;
  // Precompute the blip RGB so blink() does zero scaling work.
  uint8_t mask = NAMED_MASK[_blip_colour_idx];
  _blip_r = (mask & 0b100) ? colorSaturation : 0;
  _blip_g = (mask & 0b010) ? colorSaturation : 0;
  _blip_b = (mask & 0b001) ? colorSaturation : 0;
  if (_blip_colour_idx == 7) _blip_g = colorSaturation >> 1;  // orange G ~50%
  // Self-disable when off (mode 0), brightness 0, or no pin wired.
  // Tighter loop while fade is active so decay has enough frames.
  bool active = (b > 0) && (neoPixelMode > 0) && (_pin >= 0);
  bool fast_loop = (neoPixelMode == 1) && (_fade_rate > 0);
  EGModuleRegistry::set_loop_interval(this, active ? (fast_loop ? 10 : 30) : -1);
}

RgbColor NeoPixel::color(uint8_t r, uint8_t g, uint8_t b) {
  return _swap_rg ? RgbColor(g, r, b) : RgbColor(r, g, b);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NEOPIXEL_LEGACY[] = {
  {"neopixelBrightness", "brightness"},
  {nullptr, nullptr},
};
const EGLegacyAlias* NeoPixel::legacy_aliases() { return NEOPIXEL_LEGACY; }
// === END LEGACY IMPORT ===

NeoPixel::NeoPixel() {
}

void NeoPixel::setup()
{
  if (_pin < 0) return;
#if !defined(ESP32) && !defined(NEOPIXEL_BITBANG)
  // ESP8266 DMA mode locks to GPIO3 (RX0).
  this->controller_ = new NeoController(1, 1);
#else
  this->controller_ = new NeoController(1, (uint8_t)_pin);
#endif
  this->controller_->Begin();
  this->controller_->Show();
}

void NeoPixel::setBrightness(int input) {
  input = clamp(input * 128 / 100, 0, 128);  // 0-100% pref to 0-128 hw, int math
  if (input == 0 && controller_) {
    RgbColor black(0);
    this->controller_->SetPixelColor(0, black);
    this->controller_->Show();
  }
  colorSaturation = input;
}

void NeoPixel::blink(uint16_t timer)
{
  if (colorSaturation == 0 || neoPixelMode == 0 || !controller_) {
    return;
  }
  RgbColor rgb;

  if (this->neoPixelMode == 1) {
    // Precomputed in on_prefs_loaded; no scaling here.
    rgb = color(_blip_r, _blip_g, _blip_b);
  } else {
    float our_cpm = gcounter.get_cpmf();
    float our_5cpm = gcounter.get_cpm5f();
    rgb = color(0, colorSaturation, 0);
    if ((our_5cpm > 0) && (our_cpm > 0)) {
      // Equivalent to blinkInterval / (our_cpm / our_5cpm) but one divide.
      nextInterval = clamp((unsigned long)(blinkInterval * our_5cpm / our_cpm),
                           100UL, 4000UL);
      if (this->neoPixelMode >= 3) {
        // Noise-aware z-score: low rates need bigger deltas to trip.
        float z = poisson_z(our_cpm, our_5cpm);
        if (z > 3.0f) {
          rgb = color(colorSaturation, 0, 0);               // rise: red
        } else if (z > 1.5f) {
          rgb = color(colorSaturation, colorSaturation, 0); // rising: yellow
        } else if (z < -1.5f) {
          rgb = color(colorSaturation, 0, colorSaturation); // dropping: purple
        } else {
          rgb = color(0, colorSaturation, 0);               // stable: green
        }
      }
    } else {
      nextInterval = blinkInterval;
      if (this->neoPixelMode >= 3) {
        rgb = color(0, 0, colorSaturation);                 // no data: blue
      }
    }
    if (this->neoPixelMode == 2 || this->neoPixelMode == 4) {
      if (gcounter.is_alert()) {
        rgb = color(colorSaturation, 0, 0);
      } else if (gcounter.is_warning()) {
        rgb = color(colorSaturation, colorSaturation, 0);
      } else if (our_cpm < 0.01 && our_5cpm < 0.01) {
        rgb = color(0, 0, colorSaturation);
      }
    }
  }

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
      // Hold at peak, then gamma-2 decay across the rest of the envelope.
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
    // Per-click for Blip + Status blip; works on receiver builds too via
    // dispatchReceiverBlip updating last_blip.
    if (last_blip != gcounter.last_blip()) {
      last_blip = gcounter.last_blip();
      // Envelope ms per fade rate. Modes 2-4 stay binary at 20.
      static const uint16_t FADE_MS[4] = {20, 80, 150, 300};
      blink(this->neoPixelMode == 1 ? FADE_MS[_fade_rate] : 20);
    }
  } else {
    if (now - onTime >= nextInterval) {
      blink(20);
    }
  }
}
#endif
