/*
  Max7219.cpp - MAX7219 display backend (7-seg, round and LED matrix).

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
#include "Max7219.h"
#ifdef MAX7219_DISPLAY
#include "../Corium/System/Globals.h"
#include <EGFormat.h>
#include "../Corium/System/FastMillis.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Logger/Logger.h"
#include "../Corium/Measure/EGMeasure.h"
#include "../Corium/System/PinSafety.h"
#include "../Corium/Util/MathUtil.h"
#include "../Corium/Util/Words.h"
#include "Display.h"

EG_PSTR(M_CPM,   "cpm");
EG_PSTR(M_CPM5,  "cpm5");
EG_PSTR(M_CPM15, "cpm15");
EG_PSTR(M_USV,   "usv");
EG_PSTR(M_ALERT, "alert");
EG_PSTR(M_TEMP,  "t");
EG_PSTR(M_HUM,   "h");
EG_PSTR(M_BLIP,  "blip");
EG_PSTR(K_SYS_WARN,  "warn");
EG_PSTR(K_SYS_ALERT, "alert");

Max7219Display max7219;
EG_REGISTER_MODULE(max7219)

enum { PAGE_CPM = 0, PAGE_USV = 1, PAGE_TEMP = 2, PAGE_HUM = 3, PAGE_GRAPH = 4, PAGE_VU = 5 };
static const uint8_t MAX_PAGES = 5;

// found once at start, an egm_f walks every row
static EGMRead s_cpm, s_cpm5, s_cpm15, s_usv, s_alert, s_blip;
// slow reads, see slow_reads()
static float s_t = NAN, s_h = NAN;
static float s_warn = 0, s_alert_cpm = 0;
// matrix: a still page draws once a second
static bool s_still = false;
static bool s_redraw = true;

static float rf(EGMRead r) {
  EGMValue v;
  return (r && r(&v)) ? v.f : NAN;
}

static bool rb(EGMRead r) {
  EGMValue v;
  return r && r(&v) && v.b;
}

// all every 30 s, the shown page each second
static void slow_reads(uint8_t page) {
  static uint8_t s_age = 0;
  bool all = s_age == 0;
  if (++s_age >= 30) s_age = 0;
  if (all) {
    s_warn      = (float)EGPrefs::getUInt("counter", K_SYS_WARN);
    s_alert_cpm = (float)EGPrefs::getUInt("counter", K_SYS_ALERT);
  }
  if (all || page == PAGE_TEMP) s_t = egm_f(M_TEMP);
  if (all || page == PAGE_HUM)  s_h = egm_f(M_HUM);
}

// rows, bit x = column x (xantorohara editor)
static const uint8_t MX_TREFOIL[8] PROGMEM =
  { 0x3C, 0x18, 0x00, 0x18, 0xDB, 0xC3, 0xE7, 0x66 };

// 5 wide so "123 CPM" fits 32, rows LSB = top, bit 0 = left
static const uint64_t MX_FONT_DIGITS[10] PROGMEM = {
  0x000e1b1b1b1b1b0e, 0x001f0c0c0c0c0e0c, 0x001f03060c181b0e, 0x000e1b180e181b0e,
  0x0018181f1b1e1c18, 0x000e1b18180f031f, 0x000e1b1b0f03060c, 0x000606060c181b1f,
  0x000e1b1b0e1b1b0e, 0x00060c181e1b1b0e
};

// ~ = micro, ^ = degree
static const char     MX_SYM_CHARS[]        = ".!?@~/-%^";
static const uint64_t MX_FONT_SYMBOLS[] PROGMEM = {
  0x000c0c0000000000,   // .
  0x00180018183c3c18,   // !
  0x000c000c1830331e,   // ?
  0x001e037b7b7b633e,   // @
  0x03063e6666660000,   // ~ micro
  0x000103060c183060,   // /
  0x000000003e000000,   // -
  0x0000000000000000,   // % blank
  0x0000000000000000,   // ^ blank
};

// MatrixChunky6 by Trip5, CC-BY, github.com/trip5/Matrix-Fonts
static const uint64_t MX_SMALL_DIGITS[10] PROGMEM = {
  0x0007050505070000, 0x0007020203020000, 0x0007010704070000, 0x0007040704070000,
  0x0004040705050000, 0x0007040701070000, 0x0007050701070000, 0x0004040404070000,
  0x0007050705070000, 0x0007040705070000
};
static const uint64_t MX_SMALL_LETTERS[52] PROGMEM = {
  0x0005050705070000, 0x0007050305070000, 0x0007050105070000, 0x0007050505030000,
  0x0007010701070000, 0x0001010701070000, 0x000f090d01070000, 0x0009090f09090000,
  0x0007020202070000, 0x0007050404040000, 0x0005050305050000, 0x0007010101010000,
  0x001111151b110000, 0x0009090d0b090000, 0x000f0909090f0000, 0x0001010705070000,
  0x000b0509090f0000, 0x0005050305070000, 0x0003040701060000, 0x0002020202070000,
  0x0007050505050000, 0x0002070505050000, 0x000a1f1515150000, 0x0005050205050000,
  0x0002020705050000, 0x0007010204070000, 0x0007050607000000, 0x0007050701010000,
  0x0007010107000000, 0x0007050704040000, 0x0007030507000000, 0x0002020702060000,
  0x0604070507000000, 0x0005050701010000, 0x0007020300020000, 0x0705040400040000,
  0x0005030505010000, 0x0007020202030000, 0x001515151f000000, 0x0005050507000000,
  0x0007050507000000, 0x0107050507000000, 0x0407050507000000, 0x0001010507000000,
  0x0007060107000000, 0x0006020207020000, 0x0007050505000000, 0x0002070505000000,
  0x000a1f1515000000, 0x0005030605000000, 0x0604070505000000, 0x0007030607000000
};
// order as MX_SYM_CHARS
static const uint64_t MX_SMALL_SYMBOLS[9] PROGMEM = {
  0x0001000000000000, 0x0001000101010000, 0x0002000204070000, 0x0007010d0b0e0000,
  0x0107050500000000, 0x0001010204040000, 0x0000000700000000,
  0x0005010204050000,   // %
  0x0000000705070000    // ^ degree
};

static bool mxGlyph(char c, uint8_t* out8, bool small) {
  const uint64_t* digits  = small ? MX_SMALL_DIGITS  : MX_FONT_DIGITS;
  const uint64_t* symbols = small ? MX_SMALL_SYMBOLS : MX_FONT_SYMBOLS;
  const uint64_t* g = nullptr;
  if      (c >= '0' && c <= '9') g = &digits[c - '0'];
  else if (small && c >= 'A' && c <= 'Z') g = &MX_SMALL_LETTERS[c - 'A'];
  else if (small && c >= 'a' && c <= 'z') g = &MX_SMALL_LETTERS[26 + (c - 'a')];
  else {
    for (uint8_t i = 0; MX_SYM_CHARS[i]; i++)
      if (MX_SYM_CHARS[i] == c) { g = &symbols[i]; break; }
    if (!g) return false;
  }
  const uint8_t* p = (const uint8_t*)g;   // little-endian, byte y = row y
  for (uint8_t y = 0; y < 8; y++) out8[y] = pgm_read_byte(p + y);
  return true;
}

EG_PSTR(K_DIN,      "din");
EG_PSTR(K_CS,       "cs");
EG_PSTR(K_COUNT,    "count");
EG_PSTR(K_PANELS,   "panels");
EG_PSTR(K_DECIMALS, "decimals");
EG_PSTR(K_BAR,      "bar");
EG_PSTR(K_BAR_GAIN, "bar_gain");
EG_PSTR(K_BAR_WIN,  "bar_win");
EG_PSTR(K_ORIENT,   "orient");
EG_PSTR(K_REVERSE,  "reverse");
EG_PSTR(K_POINTS,   "points");

EG_PSTR(MX_L_DIN, "Data Pin (DIN/DIO)");
EG_PSTR(MX_L_CLK, "CLK Pin");
EG_PSTR(MX_L_CS,  "CS Pin");
EG_PSTR(MX_L_PTS, "TM1637 points");
EG_PSTR(MX_O_PTS, "Dots|Colon only");
EG_PSTR(MX_H_PTS, "Colon only: page letters (C U t H), the colon is the uSv point");
EG_PSTR(MX_L_MODE, "Pages");
EG_PSTR(MX_O_MODE, "Cycle|Fixed");
EG_PSTR(MX_H_MODE, "Cycle auto-advances; Fixed holds until the button is pressed");
EG_PSTR(MX_L_CNT, "Digits");
EG_PSTR(MX_L_PNL, "Panels");
EG_PSTR(MX_H_PNL, "8x8 panels in the chain");
EG_PSTR(MX_L_DEC, "uSv decimal places");
EG_PSTR(MX_L_BAR, "Level bar (decimal-point row)");
EG_PSTR(MX_H_BAR, "Light the decimal points as a level bar (7-seg, CPM only)");
EG_PSTR(MX_L_BGN, "Bar sensitivity");
EG_PSTR(MX_H_BGN, "Bar fills at this multiple of the recent average");
EG_PSTR(MX_L_BWN, "Bar baseline");
EG_PSTR(MX_O_BWN, "5 min|15 min");
EG_PSTR(MX_L_ORI, "Matrix module");
EG_PSTR(MX_H_ORI, "FC-16 is the common 4-in-1 strip; Generic the single 8x8 kit. Upside down for a flipped strip");
EG_PSTR(MX_O_ORI, "FC-16|Generic|FC-16 upside down|Generic upside down");
EG_PSTR(MX_L_REV, "Reverse digits");
EG_PSTR(MX_H_REV, "7-seg: for modules that show 56781234");

static const EGPref MAX7219_PREF_ITEMS[] PROGMEM = {
  {K_DIN,      MX_L_DIN,  EG_H_RBA,  "-1", nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT,  EGP_REBOOT | EGP_PIN_OUT},
  {EG_K_CLK,   MX_L_CLK,  EG_H_RBA,  "-1", nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT,  EGP_REBOOT | EGP_PIN_OUT},
  {K_CS,       MX_L_CS,   EG_H_RBA,  "-1", nullptr, -1, MAX_GPIO_PIN, 0, EGP_INT,  EGP_REBOOT | EGP_PIN_OUT},
  {EG_K_MODE,  MX_L_MODE, MX_H_MODE, "1", MX_O_MODE, 0, 1, 0, EGP_ENUM, 0},
  {K_COUNT,    MX_L_CNT,  nullptr,   "8", nullptr,  1, 8,  0, EGP_UINT, EGP_ADVANCED},
  {K_PANELS,   MX_L_PNL,  MX_H_PNL,  "4", nullptr,  1, Max7219Driver::MAX_CHIPS, 0, EGP_UINT, EGP_ADVANCED},
  {K_DECIMALS, MX_L_DEC,  nullptr,   "2", nullptr,  0, 3,  0, EGP_UINT, EGP_ADVANCED},
  {K_BAR,      MX_L_BAR,  MX_H_BAR,  "1", nullptr,  0, 1,  0, EGP_BOOL, EGP_ADVANCED},
  {K_BAR_GAIN, MX_L_BGN,  MX_H_BGN,  "2", nullptr,  1, 10, 0, EGP_UINT, EGP_ADVANCED},
  {K_BAR_WIN,  MX_L_BWN,  nullptr,   "1", MX_O_BWN, 0, 1,  0, EGP_ENUM, EGP_ADVANCED},
  {K_ORIENT,   MX_L_ORI,  MX_H_ORI,  "0", MX_O_ORI, 0, 3,  0, EGP_ENUM, EGP_ADVANCED},
  {K_REVERSE,  MX_L_REV,  MX_H_REV,  "0", nullptr,  0, 1,  0, EGP_BOOL, EGP_ADVANCED},
  {K_POINTS,   MX_L_PTS,  MX_H_PTS,  "0", MX_O_PTS, 0, 1,  0, EGP_ENUM, 0},
};

// on once DIN set
static const EGPrefGroup MAX7219_PREF_GROUP PROGMEM = {
  "max7219", "7-Seg / Matrix", 1,
  MAX7219_PREF_ITEMS,
  sizeof(MAX7219_PREF_ITEMS) / sizeof(MAX7219_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM, K_DIN,
};

const EGPrefGroup* Max7219Display::prefs_group() { return &MAX7219_PREF_GROUP; }

// rows per display.type
EG_PSTR(MX_W_TYPE, "display.type");
static const uint16_t SHOW_7SEG   = 1u << DISP_MAX7219_7SEG;
static const uint16_t SHOW_ROUND  = 1u << DISP_MAX7219_ROUND;
static const uint16_t SHOW_MATRIX = 1u << DISP_MAX7219_MATRIX;
static const uint16_t SHOW_TM     = 1u << DISP_TM1637;
static const uint16_t SHOW_MAX    = SHOW_7SEG | SHOW_ROUND | SHOW_MATRIX;

static const EGPrefShow MAX7219_SHOW[] PROGMEM = {
  {nullptr,    MX_W_TYPE, SHOW_MAX | SHOW_TM},
  {K_CS,       MX_W_TYPE, SHOW_MAX},
  {K_COUNT,    MX_W_TYPE, SHOW_7SEG | SHOW_TM},
  {K_PANELS,   MX_W_TYPE, SHOW_MATRIX},
  {K_BAR,      MX_W_TYPE, SHOW_7SEG | SHOW_TM},
  {K_BAR_GAIN, MX_W_TYPE, SHOW_7SEG | SHOW_TM},
  {K_BAR_WIN,  MX_W_TYPE, SHOW_7SEG | SHOW_TM},
  {K_ORIENT,   MX_W_TYPE, SHOW_MATRIX},
  {K_REVERSE,  MX_W_TYPE, SHOW_7SEG},
  {K_POINTS,   MX_W_TYPE, SHOW_TM},
  {},
};

const EGPrefShow* Max7219Display::prefs_show() { return MAX7219_SHOW; }

// always on the page, so always posted
uint8_t Max7219Display::display_order() { return 18; }

// not the display: no tick, no loop
void Max7219Display::begin() {
  if (display.backend() == this) return;
  EGModuleRegistry::set_tick_enabled(this, false);
  EGModuleRegistry::set_loop_interval(this, -1);
}

void Max7219Display::start() {
  _layout = (uint8_t)EGPrefs::getUInt("display", EG_K_TYPE);
  _active = true;
  s_cpm   = egm_core_read(M_CPM,   EGM_F32);
  s_cpm5  = egm_core_read(M_CPM5,  EGM_F32);
  s_cpm15 = egm_core_read(M_CPM15, EGM_F32);
  s_usv   = egm_core_read(M_USV,   EGM_F32);
  s_alert = egm_core_read(M_ALERT, EGM_BOOL);
  s_blip  = egm_core_read(M_BLIP,  EGM_U32);
  // boot only, EGP_REBOOT
  _din = (int8_t)EGPrefs::getInt("max7219", K_DIN);
  _clk = (int8_t)EGPrefs::getInt("max7219", EG_K_CLK);
  _cs  = (int8_t)EGPrefs::getInt("max7219", K_CS);
  read_prefs();
  apply();
}

// display.type waits for the reboot
void Max7219Display::reload() {
  apply();
}

// nothing ticks in safe mode, one frame
void Max7219Display::safe_mode() {
  start();
  if (tm()) {
    uint8_t codes[8], segs[8];
    bool dps[8] = {};
    memset(codes, DIGIT_DASH, sizeof(codes));
    codesToSegs(codes, dps, _count, segs);
    tmSend(segs, _count);
  } else if (_layout == DISP_MAX7219_MATRIX) {
    memset(_mfb, 0, sizeof(_mfb));
    mxDraw(_mfb, _mfb_w, 0, "SAFE", MX_SMALL);
    _drv.drawMatrix(_mfb, _orient);
  } else {
    uint8_t n = _layout == DISP_MAX7219_ROUND ? 4 : _count;
    for (uint8_t i = 0; i < n; i++) _drv.writeDigit(i, DIGIT_DASH, false);
  }
}

void Max7219Display::on_prefs_loaded() {
  read_prefs();
  if (_active) apply();
}

void Max7219Display::read_prefs() {
  uint8_t mode = (uint8_t)EGPrefs::getUInt("max7219", EG_K_MODE);
  if (mode != _mode) _page = 0;   // mode change starts on CPM
  _mode = mode;
  _count = (uint8_t)EGPrefs::getUInt("max7219",
                                     _layout == DISP_MAX7219_MATRIX ? K_PANELS : K_COUNT);
  _decimals = (uint8_t)EGPrefs::getUInt("max7219", K_DECIMALS);
  if (_decimals >= _count) _decimals = _count - 1;
  _bar = EGPrefs::getBool("max7219", K_BAR);
  _bar_gain = (uint8_t)EGPrefs::getUInt("max7219", K_BAR_GAIN);
  _bar_win = (uint8_t)EGPrefs::getUInt("max7219", K_BAR_WIN);
  _orient = (uint8_t)EGPrefs::getUInt("max7219", K_ORIENT);
  _reverse = EGPrefs::getBool("max7219", K_REVERSE);
  _colon = EGPrefs::getUInt("max7219", K_POINTS) == 1;
}

bool Max7219Display::claim_pins() {
  const int8_t pins[] = {_din, _clk, tm() ? (int8_t)-1 : _cs};
  for (int8_t p : pins) {
    if (const char* why = PinSafety::claim_output(p, PSTR("Max7219"))) {
      Log::console(PSTR("MAX7219: pin=%d unsafe (%s) - disabled"), p, why);
      return false;
    }
  }
  return true;
}

static uint8_t brightness(uint8_t top) {
  return (uint8_t)brightness_curve(EGPrefs::getUInt("display", EG_K_BRIGHTNESS), top);
}

void Max7219Display::applyTm() {
  // default 8 is the MAX's, TM1637 modules are 4
  if (_count > Tm1637Driver::MAX_DIGITS) _count = 4;
  if (_count < 2) _count = 2;   // a letter and a digit
  if (_decimals >= _count) _decimals = _count - 1;
  if (_tm_off) return;
  _tm.begin(claim_pins() ? _clk : -1, _din);
  _lit = true;
  _tm.light(brightness(7), true);
  if (_tm.ready() && !_swept) {
    bootSweep();
    _swept = true;
    if (_tm_acked) {
      Log::console(PSTR("TM1637: display up (clk=%d dio=%d)"), _clk, _din);
    } else {
      _tm_off = true;
      Log::console(PSTR("TM1637: no ack (clk=%d dio=%d), off until reboot"), _clk, _din);
    }
  }
}

void Max7219Display::apply() {
  if (tm()) {
    applyTm();
    return;
  }
  // same layout, only the light: a re-init flickers
  if (_drv.ready() && _layout == _set_layout && _count == _set_count) {
    _drv.setIntensity(brightness(15));
    return;
  }
  _set_layout = _layout;
  _set_count = _count;
  // round: digits 1-4 BCD, 5-7 arc
  uint8_t chips = 1;
  uint8_t decode = 0xFF;
  uint8_t scan = (uint8_t)(_count - 1);
  if (_layout == DISP_MAX7219_ROUND) {
    decode = 0x0F;
    scan = 6;
  } else if (_layout == DISP_MAX7219_MATRIX) {
    _panels = _count;
    _mfb_w = (uint16_t)_panels * 8;
    chips = _panels;
    decode = 0x00;
    scan = 7;
  }
  int8_t din = claim_pins() ? _din : -1;
  _drv.begin(din, _clk, _cs, chips, decode, scan);   // wakes the chip
  _lit = true;
  _drv.setIntensity(brightness(15));
  if (_drv.ready() && !_swept) {
    Log::console(PSTR("MAX7219: display up (type=%u din=%d clk=%d cs=%d)"),
                 _layout, _din, _clk, _cs);
    bootSweep();
    _swept = true;
  }
}

float Max7219Display::currentCPM() {
  float cpm = rf(s_cpm);
  return (cpm < 10.0f) ? rf(s_cpm5) : cpm;
}

#define MX_PAGE_DWELL_MS 5000UL

uint8_t Max7219Display::buildPages(uint8_t* list) {
  uint8_t n = 0;
  if (_layout == DISP_MAX7219_ROUND)  list[n++] = PAGE_VU;   // round starts on VU
  list[n++] = PAGE_CPM;
  list[n++] = PAGE_USV;
  if (_layout == DISP_MAX7219_MATRIX) list[n++] = PAGE_GRAPH;
  if (!isnan(s_t)) list[n++] = PAGE_TEMP;
  if (!isnan(s_h)) list[n++] = PAGE_HUM;
  return n;
}

void Max7219Display::updatePage() {
  uint8_t list[MAX_PAGES];
  uint8_t n = buildPages(list);
  if (_page >= n) _page = 0;
  if (_mode == 0) {                         // 0 = cycle
    unsigned long now = fast_millis();
    if ((now - _page_since) >= MX_PAGE_DWELL_MS) {
      _page = (uint8_t)((_page + 1) % n);
      _page_since = now;
    }
  }
  _cur_page = list[_page];
}

void Max7219Display::update_power(unsigned long now) {
  bool on = display.sleep(now) == Display::AWAKE;
  if (on == _lit) return;
  _lit = on;
  s_redraw = true;
  if (tm()) { if (!_tm_off) _tm.light(brightness(7), on); }
  else      _drv.shutdown(!on);
}

void Max7219Display::onButtonTap(unsigned long now, int8_t dir) {
  s_redraw = true;
  if (!_lit) {   // first tap only wakes
    update_power(now);
    return;
  }
  if (fxTap(now)) return;
  uint8_t list[MAX_PAGES];
  uint8_t n = buildPages(list);
  if (n <= 1) return;
  _page = (uint8_t)((_page + (dir > 0 ? 1 : n - 1)) % n);
  _page_since = fast_millis();
  _cur_page = list[_page];
}

// env.unit
static const char* temp_unit() {
  uint8_t u = egm_temp_unit();
  if (u == EGMT_F) return PSTR("^F");
  if (u == EGMT_K) return PSTR("K");
  return PSTR("^C");
}

void Max7219Display::pageInfo(uint8_t page, float& value, uint8_t& dec, const char*& unit) {
  switch (page) {
    case PAGE_USV:  value = rf(s_usv);     dec = _decimals; unit = PSTR("~Sv/h"); break;
    case PAGE_TEMP: value = egm_celsius_to(s_t, egm_temp_unit()); dec = 1; unit = temp_unit(); break;
    case PAGE_HUM:  value = s_h;           dec = 0;         unit = PSTR("%");     break;
    default:        value = currentCPM(); dec = value < 10.0f ? 1 : 0; unit = PSTR("CPM"); break;   // as the OLED
  }
}

uint8_t Max7219Display::adaptiveLevel(uint8_t maxLevel) {
  if (rb(s_alert)) return maxLevel;
  float cur  = currentCPM();
  float base = rf(_bar_win == 0 ? s_cpm5 : s_cpm15);
  float full = base * (float)_bar_gain;
  const float MIN_FULL = 20.0f;   // cpm
  if (full < MIN_FULL) full = MIN_FULL;
  long lvl = (long)((cur / full) * (float)maxLevel + 0.5f);
  if (lvl < 0) lvl = 0;
  if (lvl > maxLevel) lvl = maxLevel;
  return (uint8_t)lvl;
}

void Max7219Display::pushNumber(uint8_t first_digit, uint8_t ndigits, float value,
                                uint8_t decimals, uint8_t dp_bar_level) {
  uint8_t codes[8];
  bool    dps[8];
  computeDigits(value, ndigits, decimals, dp_bar_level, codes, dps);
  for (uint8_t i = 0; i < ndigits; i++) {
    uint8_t d = _reverse ? (uint8_t)(ndigits - 1 - i) : i;
    _drv.writeDigit(first_digit + d, codes[i], dps[i]);
  }
}

// by PAGE_*
static const uint8_t PAGE_LETTER[] = { SEG_C, SEG_U, SEG_t, SEG_H };

// the page left to right, for the TM1637 and /screen
void Max7219Display::pageSegs(uint8_t* segs) {
  float v; uint8_t dec; const char* u;
  pageInfo(_cur_page, v, dec, u);
  if (tm() && _colon) {
    dec = _cur_page == PAGE_USV ? (uint8_t)(_count - 2) : 0;   // point on the colon
    letteredSegs(PAGE_LETTER[_cur_page], v, _count, dec, segs);
    return;
  }
  uint8_t dp_bar = (_bar && _cur_page == PAGE_CPM) ? adaptiveLevel(_count) : 0;
  uint8_t codes[8];
  bool    dps[8];
  computeDigits(v, _count, dec, dp_bar, codes, dps);
  codesToSegs(codes, dps, _count, segs);
}

void Max7219Display::tmSend(const uint8_t* segs, uint8_t n) {
  bool ok = _tm.show(segs, n);
  if (!ok && _tm_acked) Log::console(PSTR("TM1637: no ack, check wiring and power"));
  _tm_acked = ok;
  if (ok) {
    _tm_fails = 0;
  } else if (++_tm_fails >= TM_GIVE_UP) {
    _tm_off = true;
    Log::console(PSTR("TM1637: no ack, off until reboot"));
  }
}

// regs 5-7, from the top
void Max7219Display::pushArc(uint8_t level) {
  for (uint8_t s = 0; s < 3; s++) {
    int k = (int)level - 8 * (int)s;
    if (k < 0) k = 0;
    if (k > 8) k = 8;
    uint8_t fill = k ? (uint8_t)(0xFF << (8 - k)) : 0;
    _drv.write(0, Max7219Driver::REG_DIGIT0 + 4 + s, fill);
  }
}

void Max7219Display::render7seg() {
  if (tm()) {
    uint8_t segs[8];
    pageSegs(segs);
    tmSend(segs, _count);
    return;
  }
  float v; uint8_t dec; const char* u;
  pageInfo(_cur_page, v, dec, u);
  uint8_t dp_bar = (_bar && _cur_page == PAGE_CPM) ? adaptiveLevel(_count) : 0;
  pushNumber(0, _count, v, dec, dp_bar);
}

bool Max7219Display::vu() const {
  return _layout == DISP_MAX7219_ROUND && _cur_page == PAGE_VU;
}

// round board: digit 0 is the left one
void Max7219Display::roundDigits(float v, uint8_t dec) {
  uint8_t codes[4];
  bool    dps[4];
  computeDigits(v, 4, dec, 0, codes, dps);
  for (uint8_t i = 0; i < 4; i++) _drv.writeDigit(3 - i, codes[i], dps[i]);
}

void Max7219Display::renderRound() {
  float v; uint8_t dec; const char* u;
  pageInfo(_cur_page, v, dec, u);   // VU page shows CPM
  roundDigits(v, dec);
  if (!vu()) pushArc(arcLevel());   // VU: loop() moves the arc
}

// the board's 6 green, 9 orange, 9 red, scaled to the warn and alert CPM
uint8_t Max7219Display::arcLevel() {
  return zoneLevel(currentCPM(), s_warn, s_alert_cpm, 6, 9, 9);
}

// /screen preview for 7-seg and round

static inline void fbPix(uint8_t* fb, int W, int H, int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  fb[(y >> 3) * W + x] |= (uint8_t)(1 << (y & 7));
}

// {dx, dy, horizontal} for a-g, each 3 px long
static const uint8_t SMALL_SEG[7][3] PROGMEM = {   // 3x5
  {0,0,1}, {2,0,0}, {2,2,0}, {0,4,1}, {0,2,0}, {0,0,0}, {0,2,1}
};
static const uint8_t BIG_SEG[7][3] PROGMEM = {     // 5x9
  {1,0,1}, {4,1,0}, {4,5,0}, {1,8,1}, {0,5,0}, {0,1,0}, {1,4,1}
};

struct Fb {
  uint8_t* p;
  int w, h;
};

static void drawCell(const Fb& f, int x, int y, uint8_t seg, bool big) {
  const uint8_t (*g)[3] = big ? BIG_SEG : SMALL_SEG;
  for (uint8_t s = 0; s < 7; s++) {
    if (!(seg & (1 << s))) continue;
    int sx = x + pgm_read_byte(&g[s][0]);
    int sy = y + pgm_read_byte(&g[s][1]);
    bool h = pgm_read_byte(&g[s][2]);
    for (int i = 0; i < 3; i++) fbPix(f.p, f.w, f.h, sx + (h ? i : 0), sy + (h ? 0 : i));
  }
}

// colon or DP dot, in the gap after a digit
static void drawPoint(const Fb& f, int gx, int y0, bool big, bool colon) {
  int x = big ? gx + 1 : gx;
  if (colon) {
    fbPix(f.p, f.w, f.h, x, y0 + 2);
    fbPix(f.p, f.w, f.h, x, y0 + 6);
  } else {
    fbPix(f.p, f.w, f.h, x, y0 + (big ? 8 : 4));
  }
}

// up to 4 digits big, more small
void Max7219Display::drawRep7seg(uint8_t* fb, uint16_t& w, uint16_t& h) {
  uint8_t segs[8];
  pageSegs(segs);
  bool big = _count <= 4;
  int cw = big ? 5 : 3;
  int pitch = big ? 8 : 4;
  w = big ? 32 : (uint16_t)(_count * 4 > 32 ? 32 : _count * 4);
  h = big ? 16 : 8;
  int W = w, H = h;
  int x0 = big ? (W - (_count * pitch - 3)) / 2 : 0;
  int y0 = big ? 3 : 0;
  for (uint8_t p = 0; p < _count; p++) {
    int x = x0 + p * pitch;
    if (x + cw > W) break;
    drawCell({fb, W, H}, x, y0, segs[p], big);
    if (segs[p] & 0x80) drawPoint({fb, W, H}, x + cw, y0, big, tm() && _colon);
  }
}

// 4 digits (bit 7 = DP), then lit LEDs
void Max7219Display::drawRepRound(uint8_t* fb, uint16_t& w, uint16_t& h) {
  float v; uint8_t dec; const char* u;
  pageInfo(_cur_page, v, dec, u);
  uint8_t codes[4];
  bool    dps[4];
  computeDigits(v, 4, dec, 0, codes, dps);
  for (uint8_t p = 0; p < 4; p++) {
    uint8_t i = 3 - p;
    fb[p] = (uint8_t)(codeToSeg(codes[i]) | (dps[i] ? 0x80 : 0));
  }
  fb[4] = vu() ? (uint8_t)(_vu.shown / 16) : arcLevel();
  w = 5; h = 0;
}

bool Max7219Display::screenBuffer(const uint8_t*& buf, uint16_t& w, uint16_t& h) {
  if (!_active) return false;
  // the matrix, or 32x16 for 7-seg
  static uint8_t s_repfb[sizeof(_mfb) > 64 ? sizeof(_mfb) : 64];
  if (_layout == DISP_MAX7219_MATRIX) {
    // copy, async send races render
    uint16_t mw = _mfb_w ? _mfb_w : (uint16_t)_panels * 8;
    if (mw > sizeof(s_repfb)) mw = sizeof(s_repfb);
    memcpy(s_repfb, _mfb, mw);
    buf = s_repfb;
    w = mw;
    h = 8;
    return true;
  }
  updatePage();
  for (uint8_t i = 0; i < sizeof(s_repfb); i++) s_repfb[i] = 0;
  if (_layout == DISP_MAX7219_ROUND) drawRepRound(s_repfb, w, h);
  else                               drawRep7seg(s_repfb, w, h);
  buf = s_repfb;
  return true;
}

// lamp test, chip can't be probed
void Max7219Display::bootSweep() {
  if (tm()) {
    // TM1637 can, it acks
    uint8_t all[Tm1637Driver::MAX_DIGITS];
    memset(all, 0xFF, sizeof(all));
    _tm_acked = _tm.show(all, _count);
    delay(300);
    return;
  }
  if (_layout == DISP_MAX7219_ROUND) {   // its own, no all-on
    roundBoot();
    return;
  }
  _drv.writeAll(Max7219Driver::REG_DISPLAYTEST, 0x01);
  delay(300);
  _drv.writeAll(Max7219Driver::REG_DISPLAYTEST, 0x00);
  if (_layout == DISP_MAX7219_MATRIX) {
    drawMatrixIcon(MX_TREFOIL);
    delay(900);
    memset(_mfb, 0, sizeof(_mfb));
    _drv.drawMatrix(_mfb, _orient);
  }
}

void Max7219Display::pushArcMask(uint32_t mask) {
  for (uint8_t s = 0; s < 3; s++) {
    uint8_t b = 0;
    for (uint8_t j = 0; j < 8; j++) {
      if (mask & (1UL << (8 * s + j))) b |= (uint8_t)(0x80 >> j);
    }
    _drv.write(0, Max7219Driver::REG_DIGIT0 + 4 + s, b);
  }
}

// raw segments, no decode: A-F round the edge
static const uint8_t SPIN[6] = { 0x40, 0x20, 0x10, 0x08, 0x04, 0x02 };
// a, ab, abc ... abcdefg
static const uint8_t BUILD[8] = { 0x00, 0x40, 0x60, 0x70, 0x78, 0x7C, 0x7E, 0x7F };

// comet, then a needle sweep
void Max7219Display::roundBoot() {
  _drv.setDecode(0x00);
  for (uint8_t head = 0; head < 24 + 3; head++) {
    pushArcMask((0x7UL << head >> 2) & 0xFFFFFFUL);   // 3 led tail
    for (uint8_t p = 0; p < 4; p++) _drv.write(0, Max7219Driver::REG_DIGIT0 + p, SPIN[(head + p) % 6]);
    delay(20);
  }
  // dots at 0, 25, 50, 75%
  auto frame = [this](uint8_t l) {
    pushArc(l);
    for (uint8_t p = 0; p < 4; p++) {
      _drv.write(0, Max7219Driver::REG_DIGIT0 + p, (uint8_t)(BUILD[l * 7 / 24] | (l >= p * 6 ? 0x80 : 0)));
    }
  };
  for (uint8_t l = 0; l <= 24; l++) { frame(l); delay(10 + l); }   // slows near the top
  delay(300);
  for (int l = 24; l >= 0; l--) { frame((uint8_t)l); delay(16); }
  _drv.setDecode(0x0F);
  roundDigits(0, 0);
}

void Max7219Display::s_tick(unsigned long now) {
  (void)now;   // seconds
  if (!_active) return;
  slow_reads(_cur_page);
  s_redraw = true;   // /screen draws the matrix without pins too
  if (!(tm() ? _tm.ready() : _drv.ready())) return;
  update_power(fast_millis());
  if (!tm()) _drv.heal();
  if (_layout == DISP_MAX7219_MATRIX || !_lit) return;
  // no ack, try every 10 s
  if (tm() && (_tm_off || (!_tm_acked && ++_tm_wait < TM_RETRY_S))) return;
  _tm_wait = 0;
  updatePage();
  if (tm())                               _tm_due = true;   // esp8266 ticker is SYS
  else if (_layout == DISP_MAX7219_ROUND) renderRound();
  else                                    render7seg();
}

void Max7219Display::matrixValueUnit(char* val, size_t vc, char* unit, size_t uc) {
  float v; uint8_t dec; const char* u;
  pageInfo(_cur_page, v, dec, u);
  if (dec == 0) snprintf_P(val, vc, PSTR("%ld"), (long)lroundf(v));
  else          format_f(val, vc, v, dec);
  strncpy_P(unit, u, uc - 1);
  unit[uc - 1] = '\0';
}

// works without pins, for /screen
void Max7219Display::renderMatrixContent(unsigned long now) {
  _mfb_w = (uint16_t)_panels * 8;
  if (_mfb_w > sizeof(_mfb)) _mfb_w = sizeof(_mfb);
  memset(_mfb, 0, _mfb_w);
  int W = _mfb_w;
  s_still = true;
  if (_cur_page == PAGE_GRAPH) {
    int hist[sizeof(_mfb)];
    barColumns(hist, (int)egm_history(hist, (size_t)W), _mfb, W);
    return;
  }

  char value[16], unit[8];
  matrixValueUnit(value, sizeof(value), unit, sizeof(unit));

  const int gap = 2;
  int wVal  = mxWidth(value, MX_BIG);
  int wUnit = mxWidth(unit, MX_SMALL);
  int wTot  = wVal + gap + wUnit;

  if (wTot <= W) {                            // right aligned, unit stays put
    mxDraw(_mfb, W, W - wTot, value, MX_BIG);
    mxDraw(_mfb, W, W - wUnit, unit, MX_SMALL);
    _scroll_x = 0;
    return;
  }
  s_still = false;
  if (wVal <= W) {                            // value fits, alternate
    bool showUnit = ((now / 2500) & 1);
    if (showUnit) mxDraw(_mfb, W, (W - wUnit) / 2, unit,  MX_SMALL);
    else          mxDraw(_mfb, W, (W - wVal)  / 2, value, MX_BIG);
    _scroll_x = 0;
    return;
  }
  int xe = mxDraw(_mfb, W, _scroll_x, value, MX_BIG);  // too wide, scroll
  mxDraw(_mfb, W, xe + gap, unit, MX_SMALL);
  if (--_scroll_x <= -(wTot + 2)) _scroll_x = W;
}

void Max7219Display::drawMatrixIcon(const uint8_t* rows8) {
  int x0 = (_mfb_w - 8) / 2;
  memset(_mfb, 0, sizeof(_mfb));
  for (int y = 0; y < 8; y++) {
    uint8_t r = pgm_read_byte(&rows8[y]);
    for (int x = 0; x < 8; x++)
      if (r & (1 << x)) _mfb[x0 + x] |= (uint8_t)(1 << y);
  }
  _drv.drawMatrix(_mfb, _orient);
}

static bool glyphSpan(const uint8_t* g, int& first, int& last) {
  first = 7; last = 0;
  for (int y = 0; y < 8; y++)
    for (int b = 0; b < 8; b++)
      if (g[y] & (1 << b)) { if (b < first) first = b; if (b > last) last = b; }
  return first <= last;
}

static void blitGlyph(uint8_t* fb, int W, int x, const uint8_t* g, int first, int last) {
  for (int y = 0; y < 8; y++)
    for (int b = first; b <= last; b++) {
      int px = x + (b - first);
      if ((g[y] & (1 << b)) && px >= 0 && px < W) fb[px] |= (uint8_t)(1 << y);
    }
}

// digits fixed width, rest proportional
int Max7219Display::mxText(uint8_t* fb, int W, int x, const char* s, bool draw, bool small) {
  const int digit_w = small ? 3 : 5;   // widest in each font
  int start = x;
  while (*s) {
    char c = *s++;
    uint8_t g[8];
    int first, last;
    if (c == ' ') { x += (small ? 2 : 3); continue; }
    if (!mxGlyph(c, g, small) || !glyphSpan(g, first, last)) { x += 3; continue; }
    int w = last - first + 1;
    int pad = 0;
    if (c >= '0' && c <= '9' && w < digit_w) {
      pad = (digit_w - w) / 2;
      w = digit_w;
    }
    if (draw) blitGlyph(fb, W, x + pad, g, first, last);
    x += w + 1;
  }
  return x > start ? x - 1 : x;
}

void Max7219Display::loop(unsigned long now) {
  if (!_active || !_lit) return;
  if (_tm_due) {
    _tm_due = false;
    render7seg();
  }
  if (vu()) vuFrame();
  if (_layout != DISP_MAX7219_MATRIX) return;
  if (!_fx_on && s_still && !s_redraw) return;
  s_redraw = false;
  updatePage();
  if (_fx_on) fxFrame(_mfb_w);
  else        renderMatrixContent(now);   // for /screen too
  _drv.drawMatrix(_mfb, _orient);   // no-op without pins
}

bool Max7219Display::fxTap(unsigned long now) {
  _taps = (now - _tap_ms < 500) ? _taps + 1 : 1;
  _tap_ms = now;
  if (_fx_on) {
    _fx_on = false;
    return true;
  }
  if (_taps < 5 || _layout != DISP_MAX7219_MATRIX) return false;
  _fx_on = true;
  _fx_part = 0;
  _fx = 0;
  _taps = 0;
  return true;
}

// arc in 1/16 LEDs
void Max7219Display::vuFrame() {
  EGMValue b;
  uint32_t blip = (s_blip && s_blip(&b)) ? b.u : 0;
  unsigned long now = fast_millis();
  bool fresh = now - _vu_ms > 1000;   // page just shown, no backlog kick
  _vu_ms = now;
  uint32_t counts = fresh ? 0 : blip - _vu_blip;
  _vu_blip = blip;
  float kick = 0;
  if (counts) {
    kick = 8 * 16;   // one count, 8 LEDs at most
    float perFrame = currentCPM() * loop_interval_ms() / 60000.0f;
    // at the last green LED the level falls 9 a frame
    if (perFrame > 0 && 9 / perFrame < kick) kick = 9 / perFrame;
  }
  vuStep(_vu, counts, kick, 3 * 16, 24 * 16);
  pushArc((uint8_t)(_vu.shown / 16));
}

static const uint8_t SINE16[64] PROGMEM = {
  8, 8, 9, 10, 10, 11, 12, 12, 13, 13, 14, 14, 14, 15, 15, 15,
  15, 15, 15, 15, 14, 14, 14, 13, 13, 12, 12, 11, 10, 10, 9, 8,
  8, 7, 6, 5, 5, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0, 0,
  0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7
};

static uint8_t wave(int i) { return pgm_read_byte(&SINE16[i & 63]); }

void Max7219Display::fxFrame(int W) {
  memset(_mfb, 0, sizeof(_mfb));
  if (_fx_part == 1) fxPlasma(W);
  else if (fxScroll(W)) {
    _fx_part = 1;
    _fx = 0;
  }
  _fx++;
}

bool Max7219Display::fxScroll(int W) {
  char text[12] = {};
  strncpy_P(text, W_APP_NAME, sizeof(text) - 1);
  int x = W - (int)_fx;
  int end = mxDraw(_mfb, W, x, text, MX_SMALL);
  for (int c = 0; c < W; c++) {
    int off = wave(c * 4 + _fx * 3) / 6 - 1;
    _mfb[c] = off > 0 ? (uint8_t)(_mfb[c] << off) : (uint8_t)(_mfb[c] >> -off);
  }
  return end < 0;
}

void Max7219Display::fxPlasma(int W) {
  int t = _fx / 2;
  for (int x = 0; x < W; x++) {
    uint8_t col = 0;
    for (int y = 0; y < 8; y++) {
      int v = wave(x * 5 + t * 2) + wave(y * 9 - t) + wave(x * 3 + y * 7 + t * 3);
      if (v > 23) col |= (uint8_t)(1 << y);
    }
    _mfb[x] = col;
  }
}

#endif
