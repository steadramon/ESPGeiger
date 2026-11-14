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

#include "NeoPixel.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "App/AppKeys.h"
#include "App/AppMath.h"
#include "App/FeatureBits.h"
#include "Corium/System/PinSafety.h"

CORIUM_PSTR(K_COLOR, "color");
CORIUM_PSTR(K_FADE,  "fade");
CORIUM_PSTR(K_SWAP,  "swap");


NeoPixel neopixel;
CORIUM_REGISTER_FEATURE(neopixel, FEAT_NEOPIXEL)

CORIUM_PSTR(NP_L_SWAP, "Swap R/G");
CORIUM_PSTR(NP_H_SWAP, "On if green shows as red (RGB-ordered chips)");
CORIUM_PSTR(NP_O_MODE, "Off|Blip|Blip+status|Trend pulse|Trend+status");
enum : uint8_t { NP_OFF, NP_BLIP, NP_BLIP_STATUS, NP_TREND, NP_TREND_STATUS };
static bool trend(uint32_t m)  { return m >= NP_TREND; }
static bool alarms(uint32_t m) { return m == NP_BLIP_STATUS || m == NP_TREND_STATUS; }
CORIUM_PSTR(NP_L_COL,  "Blip colour");
CORIUM_PSTR(NP_H_BLP,  "Blip mode only.");
CORIUM_PSTR(NP_O_COL,  "Green|Red|Blue|Yellow|Cyan|Magenta|White|Orange");
CORIUM_PSTR(NP_O_FAD,  "Off|Fast|Medium|Slow");
#ifndef NEOPIXEL_PIN_BLOCKED
CORIUM_PSTR(NP_L_PIN,  "Pin");
CORIUM_PSTR(NP_H_PIN,  "WS2812 data pin (-1 = disabled)");
#endif

// esp8266 boards have RGB leds, swap on
#ifndef NPX_DEFAULT_SWAP
  #if defined(ESP32)
    #define NPX_DEFAULT_SWAP "0"
  #else
    #define NPX_DEFAULT_SWAP "1"
  #endif
#endif

static const corium::Pref NEOPIXEL_PREF_ITEMS[] PROGMEM = {
#ifndef NEOPIXEL_PIN_BLOCKED
  {EG_K_PIN,        NP_L_PIN,  NP_H_PIN,  STR(NEOPIXEL_PIN), nullptr, -1, MAX_GPIO_PIN, 0, corium::P_INT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
#endif
  {EG_K_BRIGHTNESS, EG_L_BRT,  nullptr,   "15",              nullptr, 0, 100, 0, corium::P_UINT, corium::P_SLIDER},
  {EG_K_MODE,       EG_L_MOD,  nullptr,   "4",               NP_O_MODE, 0, 4, 0, corium::P_ENUM, 0},
  {K_COLOR,         NP_L_COL,  NP_H_BLP,  "0",               NP_O_COL, 0, 7,  0, corium::P_ENUM, 0},
  {K_FADE,          EG_L_FAD,  NP_H_BLP,  "0",               NP_O_FAD, 0, 3,  0, corium::P_ENUM, corium::P_ADVANCED},
  {K_SWAP,          NP_L_SWAP, NP_H_SWAP, NPX_DEFAULT_SWAP,  nullptr, 0, 0,   0, corium::P_BOOL, corium::P_ADVANCED},
};

static const corium::PrefGroup NEOPIXEL_PREF_GROUP PROGMEM = {
  "neopixel", "NeoPixel", 1,
  NEOPIXEL_PREF_ITEMS,
  sizeof(NEOPIXEL_PREF_ITEMS) / sizeof(NEOPIXEL_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT,
};

const corium::PrefGroup* NeoPixel::prefsGroup() { return &NEOPIXEL_PREF_GROUP; }

// bit 2 R, 1 G, 0 B
static const uint8_t NAMED_MASK[8] PROGMEM = {
  0b010, 0b100, 0b001, 0b110, 0b011, 0b101, 0b111, 0b100,
};

void NeoPixel::precompute_blip() {
  uint8_t mask = pgm_read_byte(&NAMED_MASK[_blip_colour_idx]);
  _blip_r = (mask & 0b100) ? _level : 0;
  _blip_g = (mask & 0b010) ? _level : 0;
  _blip_b = (mask & 0b001) ? _level : 0;
  if (_blip_colour_idx == 7) _blip_g = _level >> 1;  // orange
}

#ifndef NEOPIXEL_PIN_BLOCKED
static int8_t claim_pin(int p) {
  if (p < 0) return -1;
  if (const char* why = corium::PinSafety::claimOutput(p, PSTR("NeoPx"))) {
    corium::Log::console(PSTR("NeoPixel: pin=%d unsafe (%s) - disabled"), p, why);
    return -1;
  }
  return (int8_t)p;
}
#endif

void NeoPixel::onPrefsLoaded() {
#ifndef NEOPIXEL_PIN_BLOCKED
  // boot only, the controller keeps its pin
  static bool s_pin_read = false;
  if (!s_pin_read) {
    s_pin_read = true;
    _pin = claim_pin((int)corium::Prefs::getInt("neopixel", EG_K_PIN));
    // setup() ran before corium::Prefs::begin
    if (!_bus && _pin >= 0) setup();
  }
#endif
  int b = (int)corium::Prefs::getUInt("neopixel", EG_K_BRIGHTNESS);
  setBrightness(b);
  _swap_rg = corium::Prefs::getBool("neopixel", K_SWAP);
  uint32_t m = corium::Prefs::getUInt("neopixel", EG_K_MODE);
  neoPixelMode = m > NP_TREND_STATUS ? NP_TREND_STATUS : m;
  uint32_t c = corium::Prefs::getUInt("neopixel", K_COLOR);
  _blip_colour_idx = c > 7 ? 0 : (uint8_t)c;
  uint32_t f = corium::Prefs::getUInt("neopixel", K_FADE);
  _fade_rate = f > 3 ? 0 : (uint8_t)f;
  precompute_blip();
  bool active = (b > 0) && (neoPixelMode != NP_OFF) && (_pin >= 0);
  bool fast_loop = (neoPixelMode == NP_BLIP) && (_fade_rate > 0);
  corium::Registry::every(this, active ? (fast_loop ? FAST_LOOP_MS : LOOP_MS) : 0);
}

RgbColor NeoPixel::color(uint8_t r, uint8_t g, uint8_t b) {
  return _swap_rg ? RgbColor(g, r, b) : RgbColor(r, g, b);
}

// legacy import
static const corium::LegacyAlias NEOPIXEL_LEGACY[] PROGMEM = {
  {"neopixelBrightness", "brightness"},
  {},
};
corium::LegacyRows NeoPixel::legacyAliases() { return NEOPIXEL_LEGACY; }

NeoPixel::NeoPixel() {
}

// loop blinks it
void NeoPixel::on_click(uint32_t) { neopixel._blip = true; }

// found once, read each blink
static corium::MRead s_cpm = nullptr, s_cpm5 = nullptr, s_alert = nullptr, s_warn = nullptr;

void NeoPixel::begin() {
  gcounter.on_click(on_click);
  gcounter.on_rx_click(on_click);
  s_cpm   = corium::m_core_read(PSTR("cpm"),   corium::M_F32);
  s_cpm5  = corium::m_core_read(PSTR("cpm5"),  corium::M_F32);
  s_alert = corium::m_core_read(PSTR("alert"), corium::M_BOOL);
  s_warn  = corium::m_core_read(PSTR("warn"),  corium::M_BOOL);
}

// safe mode skips preWifi and onPrefsLoaded: clear the pre-crash colour
void NeoPixel::park() {
#ifndef NEOPIXEL_PIN_BLOCKED
  _pin = claim_pin((int)corium::Prefs::getInt("neopixel", EG_K_PIN));
#endif
  setup();
}

void NeoPixel::setup()
{
  if (_pin < 0) return;
  this->_bus = new NeoController(1, (uint8_t)_pin);
  this->_bus->Begin();
  this->_bus->Show();
}

static void show(NeoController* c, RgbColor rgb) {
  c->SetPixelColor(0, rgb);
  c->Show();
}

void NeoPixel::setBrightness(int pct) {
  int level = (int)brightnessCurve((uint32_t)pct, 128);  // 0-128
  if (pct && !level) level = 1;
  if (level == 0 && _bus) show(_bus, RgbColor(0));
  _level = level;
}

// trend modes: blink faster when rising, colour by trend
RgbColor NeoPixel::trend_color()
{
  uint8_t s = (uint8_t)_level;
  float cpm = corium::m_read_f(s_cpm);
  float cpm5 = corium::m_read_f(s_cpm5);
  if (cpm5 <= 0 || cpm <= 0) {
    _next_ms = _blink_ms;
    return color(0, 0, s);                         // no data: blue
  }
  _next_ms = clamp((unsigned long)(_blink_ms * cpm5 / cpm), 100UL, 4000UL);
  float z = poisson_z(cpm, cpm5);
  if (z > 3.0f)  return color(s, 0, 0);            // rise: red
  if (z > 1.5f)  return color(s, s, 0);            // rising: yellow
  if (z < -1.5f) return color(s, 0, s);            // dropping: purple
  return color(0, s, 0);                           // stable: green
}

// status modes: alarms win, blue when quiet
RgbColor NeoPixel::alarm_color(RgbColor rgb)
{
  uint8_t s = (uint8_t)_level;
  if (corium::m_read_b(s_alert)) return color(s, 0, 0);
  if (corium::m_read_b(s_warn))  return color(s, s, 0);
  if (corium::m_read_f(s_cpm) < 0.01f && corium::m_read_f(s_cpm5) < 0.01f) return color(0, 0, s);
  return rgb;
}

void NeoPixel::blink(uint16_t ms)
{
  if (_level == 0 || neoPixelMode == NP_OFF || !_bus) {
    return;
  }
  RgbColor rgb;
  if (this->neoPixelMode == NP_BLIP) {
    rgb = color(_blip_r, _blip_g, _blip_b);
  } else if (trend(neoPixelMode)) {
    rgb = trend_color();
  } else {
    rgb = color(0, _level, 0);
  }
  if (alarms(neoPixelMode)) rgb = alarm_color(rgb);

  show(_bus, rgb);
  _is_off = false;
  _on_ms = corium::fastMillis();
  _lit_ms = ms;
}

void NeoPixel::loop(unsigned long now)
{
  if (_level == 0 || !_bus) {
    return;
  }
  if (!_is_off) {
    unsigned long elapsed = now - _on_ms;
    if (elapsed >= _lit_ms) {
      show(_bus, RgbColor(0));
      _is_off = true;
    } else if (this->neoPixelMode == NP_BLIP && _fade_rate > 0) {
      // hold at peak, then gamma-2 decay
      const uint16_t FADE_HOLD_MS = 20;
      if (elapsed >= FADE_HOLD_MS && _lit_ms > FADE_HOLD_MS) {
        uint32_t fe = elapsed - FADE_HOLD_MS;
        uint32_t fw = _lit_ms - FADE_HOLD_MS;
        uint32_t lin = ((fw - fe) * 256u) / fw;
        uint32_t scale = (lin * lin) >> 8;
        RgbColor f = color(
          (uint8_t)((uint32_t)_blip_r * scale >> 8),
          (uint8_t)((uint32_t)_blip_g * scale >> 8),
          (uint8_t)((uint32_t)_blip_b * scale >> 8));
        show(_bus, f);
      }
    }
  }
  if (!trend(neoPixelMode)) {
    if (_blip) {
      _blip = false;
      static const uint16_t FADE_MS[4] PROGMEM = {20, 80, 150, 300};
      blink(this->neoPixelMode == NP_BLIP ? pgm_read_word(&FADE_MS[_fade_rate]) : 20);
    }
  } else {
    if (now - _on_ms >= _next_ms) {
      blink(20);
    }
  }
}
#endif
