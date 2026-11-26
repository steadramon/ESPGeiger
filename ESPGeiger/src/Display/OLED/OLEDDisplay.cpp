/*
  OLEDDisplay.cpp - OLEDDisplay connection class

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
#ifdef SSD1306_DISPLAY
#ifndef U8G2_USE_DYNAMIC_ALLOC
#error "OLED builds need ${u8g2_flags.build_flags}, the frame is malloc'd"
#endif
#include "OLEDDisplay.h"
#include "Counter/Counter.h"
#include <EGFormat.h>
#include <BarGraph.h>
#include "fonts.h"
#include "logo.h"
#include "Display/Display.h"
#include "Display/DisplayType.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Util/MathUtil.h"
#include "App/AppKeys.h"
#include "App/AppMath.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include "App/Notice.h"
#include "Corium/System/I2CBus.h"
#include "Corium/System/PinSafety.h"
#include "Corium/System/Uptime.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Counter/CounterMaths.h"
#include "GRNG/GRNG.h"
#include "GeigerInput/Type/UdpRx.h"

CORIUM_PSTR(K_RST,  "rst");
CORIUM_PSTR(K_FLIP, "flip");

static const uint8_t HIST = 45;   // seconds
static const uint8_t GRAPH_TOP = 35;
static const uint8_t GRAPH_BOT = 63;
static const uint8_t GRAPH_W   = 2 * HIST;
static const uint8_t GRAPH_H   = 24;
static const uint8_t LABEL_X   = GRAPH_W + 3;

static void format_cpm(char* buf, size_t bufsz, float cpm) {
  if (cpm < 10.0f) format_f(buf, bufsz, cpm, 1);
  else             snprintf_P(buf, bufsz, W_FMT_U, (unsigned)CounterMaths::to_int(cpm));
}

struct Drop { int16_t y; uint8_t x; uint8_t speed; uint8_t tail; uint8_t seed; bool alive; };
static const uint8_t MATRIX_DROPS = 32;
static Drop*    s_matrix_drops = nullptr;
static uint8_t* s_static_noise = nullptr;

// Loop rates in ms. With no panel only /screen reads it, every 500 ms
static const uint16_t LOOP_MS           = 20;
static const uint16_t SAVER_LOOP_MS     = 10;
static const uint16_t NOPANEL_LOOP_MS   = 250;
static const uint16_t NOPANEL_SAVER_MS  = 80;
static const uint16_t DARK_LOOP_MS      = 30000;
static const uint16_t LOCKED_LOOP_MS    = 1000;
// redraw rates in ms
static const unsigned long FRAME_MS         = 500;
static const unsigned long NOPANEL_FRAME_MS = 250;
static const unsigned long SAVER_FRAME_MS   = 80;
static const unsigned long MAIN_FULL_MS     = 10000;
static const unsigned long NET_MS           = 1000;
static const unsigned long ABOUT_MS         = 10000;
static const uint8_t TILE_COLS = OLED_WIDTH >> 3;
// how many sizes are built in, out of 128x64, 72x40 and 128x32
#define OLED_SIZES ((DISP_HAS(0) || DISP_HAS(1) || DISP_HAS(2)) + DISP_HAS_TINY + DISP_HAS_SHORT)
// The panel's size, setup_driver sets these when more than one size is built
#if OLED_SIZES > 1
static uint8_t s_tile_rows = OLED_HEIGHT >> 3;
#else
static constexpr uint8_t s_tile_rows = DISP_HAS_TINY ? 5 : DISP_HAS_SHORT ? 4 : OLED_HEIGHT >> 3;
#endif
#if OLED_SIZES > 1 && DISP_HAS_TINY
static uint8_t s_width = OLED_WIDTH;
#else
static constexpr uint8_t s_width = DISP_HAS_TINY ? 72 : OLED_WIDTH;
#endif

SSD1306Display oled;
CORIUM_REGISTER_FEATURE(oled, FEAT_OLED)
static bool s_added = Display::add(&oled);

// all but the led types
bool SSD1306Display::takes(uint8_t type) { return !DISP_IS_MAX7219(type); }

// stand-ins for ThingPulse ArialMT_Plain
#define U8G2_FONT_ARIAL_10  u8g2_font_helvR08_tr
#define U8G2_FONT_ARIAL_16  u8g2_font_helvR12_tr
#define U8G2_FONT_ARIAL_24  U8G2_FONT_ARIAL_16

static const char PIN_OWNER[] PROGMEM = "OLED";

static void draw_glyph(uint8_t* fb, int16_t cx, int16_t y_top, const uint8_t* glyph,
                       uint16_t byte_count, uint8_t bytes_per_col) {
  const uint8_t W = s_width;
  const uint8_t PAGES = s_tile_rows;
  uint8_t col = 0;
  uint8_t bc = 0;
  int8_t  page0 = y_top >> 3;
  uint8_t y_off = y_top & 7;
  int8_t  page  = page0;
  for (uint16_t i = 0; i < byte_count; i++) {
    uint8_t pixels = pgm_read_byte(glyph + i);
    int16_t tx = cx + col;
    if (pixels && (uint16_t)tx < W) {
      if ((uint8_t)page < PAGES) {
        fb[(uint16_t)page * W + tx] |= (uint8_t)(pixels << y_off);
      }
      if (y_off != 0 && (uint8_t)(page + 1) < PAGES) {
        fb[(uint16_t)(page + 1) * W + tx] |= (uint8_t)(pixels >> (8 - y_off));
      }
    }
    if (++bc >= bytes_per_col) {
      bc = 0; col++;
      page = page0;
    } else {
      page++;
    }
  }
}

// ThingPulse font layout is height, first char, count, the 4 byte jump rows, then glyphs as columns with lsb on top
static void drawTPString(SSD1306Display& d, int16_t x, int16_t y_top,
                         const uint8_t* font, const char* str) {
  if (!font || !str) return;
  uint8_t font_height   = pgm_read_byte(font + 1);
  uint8_t first_char    = pgm_read_byte(font + 2);
  uint8_t num_chars     = pgm_read_byte(font + 3);
  uint8_t bytes_per_col = (font_height + 7) >> 3;
  if (bytes_per_col == 0) return;

  const uint8_t* jump_table  = font + 4;
  const uint8_t* glyph_table = jump_table + ((uint16_t)num_chars << 2);
  uint8_t* fb = d.getBufferPtr();

  int16_t cx = x;
  for (const char* p = str; ; p++) {
    uint8_t c = pgm_read_byte(p);
    if (c == 0) break;
    if (c < first_char || (uint16_t)(c - first_char) >= num_chars) continue;
    const uint8_t* je = jump_table + ((uint16_t)(c - first_char) << 2);
    uint8_t msb        = pgm_read_byte(je);
    uint8_t lsb        = pgm_read_byte(je + 1);
    uint8_t byte_count = pgm_read_byte(je + 2);
    uint8_t advance    = pgm_read_byte(je + 3);

    if (msb == 0xFF) { cx += advance; continue; }
    uint16_t goff = ((uint16_t)msb << 8) | lsb;
    draw_glyph(fb, cx, y_top, glyph_table + goff, byte_count, bytes_per_col);
    cx += advance;
  }
}

void SSD1306Display::drawStrP(int16_t x, int16_t y, const char* s) {
  if (!s) return;
  // raw bytes, no utf-8
  int16_t cx = x;
  for (const char* p = s; ; p++) {
    uint8_t c = pgm_read_byte(p);
    if (c == 0) break;
    cx += u8g2_DrawGlyph(&_u8g2, cx, y, c);
  }
}

#ifndef OLED_PINS_BLOCKED
// Falls back to the default if the pin is unsafe, and claims that one instead
static uint8_t safe_pin(uint8_t want, uint8_t def, const char* owner, const char* fmt) {
  const char* why = corium::PinSafety::claimOutput((int)want, owner);
  if (!why) return want;
  corium::Log::console(fmt, want, why, def);
  if (def != 255) corium::PinSafety::claim((int)def, owner);
  return def;
}
#endif

void SSD1306Display::load_pins() {
#ifndef OLED_PINS_BLOCKED
  _pin_sda = safe_pin((uint8_t)corium::Prefs::getUInt("i2c", corium::K_SDA), _pin_sda, corium::PinSafety::I2C,
                      PSTR("OLED: sda=%u unsafe (%s) - keeping default %u"));
  _pin_scl = safe_pin((uint8_t)corium::Prefs::getUInt("i2c", corium::K_SCL), _pin_scl, corium::PinSafety::I2C,
                      PSTR("OLED: scl=%u unsafe (%s) - keeping default %u"));
  uint8_t rst = (uint8_t)corium::Prefs::getUInt("display", K_RST);
  _pin_rst = rst == 255 ? 255 : safe_pin(rst, _pin_rst, PIN_OWNER,
                                         PSTR("OLED: rst=%u unsafe (%s) - keeping default %u"));
#endif
#if DISP_MANY
  _type = (uint8_t)corium::Prefs::getUInt("display", EG_K_TYPE);
#else
  _type = OLED_TYPE;
#endif
  corium::I2CBus::pins(_pin_sda, _pin_scl);
}

void SSD1306Display::start() {
  load_pins();
  setup();
  if (!_frame) return;
  _started = true;
  reload();
  corium::Registry::on(Display::TOPIC_POWER, this);
  corium::Registry::on(TOPIC_SENT, this);
  corium::Registry::on(Topic::PING, this);
  corium::Registry::every(this, LOOP_MS);
}

void SSD1306Display::reload() {
  setBrightness((uint8_t)corium::Prefs::getUInt("display", EG_K_BRIGHTNESS));
}

void SSD1306Display::setBrightness(uint8_t brightness) {
  if (!_present) return;
  brightness = (uint8_t)(((uint16_t)brightness * 255 + 50) / 100);
  setContrast(brightness);
}

uint16_t SSD1306Display::loop_ms() const {
  bool saver = oled_page == PAGE_SAVER;
  if (_present) return saver ? SAVER_LOOP_MS : LOOP_MS;
  return saver ? NOPANEL_SAVER_MS : NOPANEL_LOOP_MS;
}

unsigned long SSD1306Display::frame_ms() const {
  if (oled_page == PAGE_SAVER) return SAVER_FRAME_MS;
  return _present ? FRAME_MS : NOPANEL_FRAME_MS;
}

// Due now. 0 would read as in the future for half the wrap
void SSD1306Display::page_changed(unsigned long now) {
  if (_last_page == PAGE_SAVER) {
    if (s_matrix_drops) { free(s_matrix_drops); s_matrix_drops = nullptr; }
    if (s_static_noise) { free(s_static_noise); s_static_noise = nullptr; }
  }
  _last_page = oled_page;
  _next_render_due = now;
  corium::Registry::every(this, loop_ms());
}

// false = asleep
bool SSD1306Display::awake(unsigned long now) {
  if (!_present) return true;
  if (!display.lit()) {
    if (oled_on) { clearBuffer(); setPowerSave(1); oled_on = false; _send_rows_left = 0; }
    corium::Registry::every(this, DARK_LOOP_MS);   // not 0, so power events still get through
    return false;
  }
  if (!oled_on) {
    setPowerSave(0);
    oled_page = PAGE_MAIN;
    oled_on = true;
    oled_last_update = now - MAIN_FULL_MS;
    corium::Registry::every(this, LOOP_MS);
  }
  return true;
}

// oled_last_update 0 = never drawn
bool SSD1306Display::due(unsigned long now, unsigned long every) {
  if (corium::fresh(now, oled_last_update, every)) return false;
  oled_last_update = now;
  return true;
}

uint8_t SSD1306Display::render_main(unsigned long now) {
  uint8_t dirty = DR_NONE;
  bool full = due(now, MAIN_FULL_MS);
  if (full) {
    page_one_clear();
    dirty = DR_FULL;
  }
  // Values and graph take turns
  _p1_alt = !_p1_alt;
  uint8_t top = DR_TOP;
  if (isShort()) top = DR_FULL;   // usv sits in the bottom half
  if ((_p1_alt || full) && page_one_values(now)) dirty |= top;
  if ((!_p1_alt || full) && page_one_graph()) dirty |= DR_BOT;
  return dirty;
}

void SSD1306Display::render_saver(unsigned long now) {
  if (oled_last_update == 0) {
    _page4_ms = 0;
    _page4_variant = random(2);
  }
  oled_last_update = now;
  if (_page4_variant == 0) page_four_static();
  else page_four_matrix();
}

#if DISP_HAS_TINY
void SSD1306Display::render_tiny() {
  if (oled_page < PAGE_MAIN || oled_page > PAGE_SAVER) oled_page = PAGE_MAIN;
  clearBuffer();
  switch (oled_page) {
    case 2:  page_tiny_dose();  break;
    case 3:  page_tiny_net();   break;
    case 4:  page_tiny_meter(); break;
    default: page_tiny();       break;
  }
  if (corium::Wifi::connected) drawPixel(71, 0);   // on every page, drawn over the top
}
#endif

// Returns DR_ flags for what needs sending
uint8_t SSD1306Display::render(unsigned long now) {
#ifdef APP_NOTICE
  if (_notice) {
    if (!due(now, ABOUT_MS)) return DR_NONE;
    clearBuffer();
    setFont(isTiny() ? U8G2_FONT_ARIAL_10 : U8G2_FONT_ARIAL_16);
    int16_t y = 20;
    if (isShort()) y = 8;
    drawStrP(0, y, _notice);
    return DR_FULL;
  }
#endif
#if DISP_HAS_TINY
  if (isTiny()) {
    render_tiny();
    return DR_FULL;
  }
#endif
  switch (oled_page) {
    case PAGE_MAIN:
      return render_main(now);
    case PAGE_NET:
      if (!due(now, NET_MS)) return DR_NONE;
      page_two_full();
      return DR_FULL;
    case PAGE_ABOUT:
      if (!due(now, ABOUT_MS)) return DR_NONE;
      page_three_full();
      return DR_FULL;
    case PAGE_SAVER:
      render_saver(now);
      return DR_FULL;
  }
  return DR_NONE;
}

void SSD1306Display::queue_rows(uint8_t dirty) {
#if DISP_HAS_TINY
  // tiny sends in one go, the row maths would run off the end
  if (isTiny()) {
    sendBuffer();
    return;
  }
#endif
  bool half = (dirty == DR_TOP || dirty == DR_BOT);
  _send_cur = 0;
  _send_ty = (dirty == DR_BOT) ? s_tile_rows / 2 : 0;
  _send_rows_left = half ? s_tile_rows / 2 : s_tile_rows;
}

void SSD1306Display::loop(unsigned long now) {
  if (!_started) {
    corium::Registry::every(this, 0);
    return;
  }
  if (_ota_lock) {
    corium::Registry::every(this, LOCKED_LOOP_MS);
    return;
  }
  // One row a loop
  if (_send_rows_left > 0) {
    if (_present) updateDisplayArea(0, _send_ty + _send_cur, TILE_COLS, 1);
    _send_cur++;
    _send_rows_left--;
    if (!_send_rows_left) next_frame(now);
    return;
  }
  take_events(now);
  if (!corium::dueEvery(now, _next_render_due, frame_ms())) {
    next_frame(now);
    return;
  }
  draw_frame(now);
}

void SSD1306Display::take_events(unsigned long now) {
  if (oled_page > OLED_PAGES) oled_page = PAGE_MAIN;
  if (oled_page != _last_page) page_changed(now);
  if (oled_page == PAGE_SAVER) display.wake(now);
#ifdef APP_NOTICE
  // A notice going up or down redraws the lot now
  const char* n = Notice::shown();
  if (n != _notice) {
    _notice = n;
    oled_last_update = 0;
    _next_render_due = now;
  }
#endif
  uint16_t ev = corium::Registry::events(this);
  if (ev & ~(1u << Display::TOPIC_POWER)) _sent_mark = 2;   // the other topics are all sends
  if (ev & (1u << Display::TOPIC_POWER)) _next_render_due = now;   // lit or dark
}

void SSD1306Display::draw_frame(unsigned long now) {
  if (!awake(now)) return;
  uint8_t dirty = render(now);
  if (dirty != DR_NONE && _present) queue_rows(dirty);
  if (!_send_rows_left) next_frame(now);
}

// While rows are going out it runs every loop, otherwise it sleeps until the next frame
void SSD1306Display::next_frame(unsigned long now) {
  uint32_t at = now + loop_ms();
  if (!corium::reached(at, _next_render_due)) at = _next_render_due;
  corium::Registry::dueAt(this, at);
}

// k is 10 for tenths, only under 10. false if there are no counts
static bool hist_range(int* h, uint16_t n, int32_t& lo, int32_t& hi, int& k) {
  k = display.tenths(h, n);
  int l, u;
  bool counts = barRange(h, n, k, k, l, u);
  if (k > 1) { l = l / 10 * 10; u = (u + 9) / 10 * 10; }
  lo = l;
  hi = u;
  return counts;
}

#if DISP_HAS_TINY
void SSD1306Display::page_tiny() {
  char buf[12];
  format_cpm(buf, sizeof(buf), display.shown_cpm());
  setFont(U8G2_FONT_ARIAL_16);
  drawStr(0, 0, buf);

  const int CHART_TOP = 18;
  const int CHART_BOT = 39;
  const int CHART_H   = CHART_BOT - CHART_TOP;
  const int CHART_W   = 72;
  int hist[HIST];
  uint16_t n = (uint16_t)egm_history(hist, HIST);
  if (n < 2) return;
  int32_t mn, mx;
  int k;
  hist_range(hist, n, mn, mx, k);
  int32_t span = mx - mn;
  int prev_x = 0, prev_y = 0;
  for (uint16_t i = 0; i < n; i++) {
    int v = hist[i];
    int x = (CHART_W - 1) - ((n - 1 - i) * (CHART_W - 1)) / (HIST - 1);
    int y = CHART_BOT - (int)(((int32_t)v - mn) * CHART_H / span);
    if (i > 0) drawLine(prev_x, prev_y, x, y);
    prev_x = x;
    prev_y = y;
  }
}

static const char* fit_str(SSD1306Display& d, const char* src, int max_w) {
  static char buf[24];
  if ((int)d.getStrWidth(src) <= max_w) return src;
  size_t n = strlen(src);
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  while (n > 0) {
    n--;
    snprintf(buf, sizeof(buf), "%.*s..", (int)n, src);
    if ((int)d.getStrWidth(buf) <= max_w) return buf;
  }
  buf[0] = '\0';
  return buf;
}

void SSD1306Display::page_tiny_dose() {
  char buf[12];
  format_f(buf, sizeof(buf), display.usv(), 2);
  setFont(U8G2_FONT_ARIAL_16);
  drawStr(0, 0, buf);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 16, PSTR("uSv/h"));
  char cpmbuf[8];
  format_cpm(cpmbuf, sizeof(cpmbuf), display.shown_cpm());
  snprintf_P(buf, sizeof(buf), PSTR("%s CPM"), cpmbuf);
  drawStr(0, 28, buf);
}

void SSD1306Display::page_tiny_net() {
  setFont(U8G2_FONT_ARIAL_10);
  char buf[20];
  if (corium::Wifi::connected) {
    corium::Wifi::formatIP(buf, sizeof(buf));
    drawStr(0, 2,  fit_str(*this, buf, 71));
    drawStr(0, 15, fit_str(*this, corium::Wifi::ssid, 71));
    snprintf_P(buf, sizeof(buf), PSTR("%d dBm"), (int)corium::Wifi::rssi);
    drawStr(0, 28, buf);
  } else if (corium::Wifi::ap) {
    // The hotspot's name is the hostname
    corium::Wifi::formatIP(buf, sizeof(buf));
    drawStrP(0, 2,  PSTR("Hotspot"));
    drawStr (0, 15, fit_str(*this, corium::DeviceInfo::hostname(), 71));
    drawStr (0, 28, fit_str(*this, buf, 71));
  } else {
    drawStrP(0, 2,  PSTR("Offline"));
    drawStr (0, 15, fit_str(*this, corium::DeviceInfo::hostname(), 71));
    drawStr (0, 28, fit_str(*this, corium::DeviceInfo::uptimeString(), 71));
  }
}

void SSD1306Display::page_tiny_meter() {
  // log10 cpm over 180 deg
  const int px = 36;
  const int py = 36;
  const int r  = 28;
  char buf[8];
  format_cpm(buf, sizeof(buf), display.shown_cpm());
  setFont(U8G2_FONT_ARIAL_10);
  drawStr(0, 0, buf);
  drawCircle(px, py, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  for (int i = 0; i <= 3; i++) {
    float t = (float)i / 3.0f;
    float ang = (float)M_PI * (1.0f - t);
    float c = cosf(ang), s = sinf(ang);
    int len = (i == 0 || i == 3) ? 6 : 4;
    drawLine(px + (int)(r * c),       py - (int)(r * s),
             px + (int)((r - len) * c), py - (int)((r - len) * s));
  }
  for (int i = 0; i < 3; i++) {
    float t = (float)i / 3.0f + (1.0f / 6.0f);
    float ang = (float)M_PI * (1.0f - t);
    float c = cosf(ang), s = sinf(ang);
    drawLine(px + (int)(r * c),       py - (int)(r * s),
             px + (int)((r - 2) * c), py - (int)((r - 2) * s));
  }
  float cpm = display.shown_cpm();
  if (cpm < 1.0f) cpm = 1.0f;
  float t = clamp(log10f(cpm) / 3.0f, 0.0f, 1.0f);
  float ang = (float)M_PI * (1.0f - t);
  float c = cosf(ang), s = sinf(ang);
  drawLine(px, py, px + (int)((r - 3) * c), py - (int)((r - 3) * s));
  drawDisc(px, py, 2);
}

#endif  // DISP_HAS_TINY (page_tiny)

// 128x32: cpm and icons on top, usv and the graph under
static const uint8_t S_GRAPH_X   = OLED_WIDTH - 1 - HIST;
static const uint8_t S_GRAPH_TOP = 17;
static const uint8_t S_GRAPH_BOT = 31;

// One bar a second, no labels
bool SSD1306Display::graph_short() {
  int hist[HIST];
  uint16_t n = (uint16_t)egm_history(hist, HIST);
  int32_t lo = 0;
  int32_t hi = 0;
  int k = 1;
  bool counts = n > 0 && hist_range(hist, n, lo, hi, k);

  setDrawColor(0);
  drawBox(S_GRAPH_X - 1, 16, OLED_WIDTH - S_GRAPH_X + 1, 16);
  setDrawColor(1);
  drawLine(S_GRAPH_X, S_GRAPH_BOT, OLED_WIDTH - 1, S_GRAPH_BOT);
  if (!counts) return true;
  int x = S_GRAPH_X + HIST - n;
  for (uint16_t i = 0; i < n; i++, x++) {
    int y = S_GRAPH_BOT - 1 - (int)map((long)hist[i], (long)lo, (long)hi, 0, S_GRAPH_BOT - 1 - S_GRAPH_TOP);
    drawLine(x, y, x, S_GRAPH_BOT - 1);
  }
  return true;
}

// three text lines, 128x32 packs them closer
void SSD1306Display::lines(int16_t x, const char* a, const char* b, const char* c) {
  int16_t y = isShort() ? 0 : 2, step = isShort() ? 11 : 15;
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(x, y, a);
  drawStrP(x, y + step, b);
  drawStrP(x, y + 2 * step, c);
}

void SSD1306Display::page_short_net() {
  clearBuffer();
  lines(0, corium::DeviceInfo::hostname(), PSTR("IP:"), corium::DeviceInfo::uptimeString());
  char ip[CORIUM_IPV4_MAX + 1];
  corium::Wifi::formatIP(ip, sizeof(ip));
  drawStrP(16, 11, ip);
  if (M_HAS_HV) {
    int32_t hv;
    if (m_hv_volts(&hv)) {
      char buf[12];
      snprintf_P(buf, sizeof(buf), PSTR("%ldV"), (long)hv);
      drawStr(OLED_WIDTH - getStrWidth(buf), 11, buf);
    }
  }
}

void SSD1306Display::page_two_full() {
  if (isShort()) {
    page_short_net();
    return;
  }
  clearBuffer();
  lines(0, corium::DeviceInfo::hostname(), PSTR("IP:"), nullptr);
  char ip[CORIUM_IPV4_MAX + 1];
  corium::Wifi::formatIP(ip, sizeof(ip));
  drawStrP(16, 17, ip);
  int uptime_y = 32;
  if (M_HAS_HV) {
    int32_t hv;
    if (m_hv_volts(&hv)) {
      drawStrP(0, uptime_y, PSTR("HV:"));
      char buf[12];
      format_f(buf, sizeof(buf), (float)hv);
      drawStrP(20, uptime_y, buf);
      uptime_y = 47;
    }
  }
  drawStrP(0, uptime_y, corium::DeviceInfo::uptimeString());
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  GeigerUdpRx* rx = gcounter.udp_rx();
  if (rx) {
    char buf[24];
    const char* chip = rx->locked_chipid();
    if (rx->summing()) {
      snprintf_P(buf, sizeof(buf), PSTR("Sum %u src"), (unsigned)rx->producer_count());
    } else if (chip) {
      snprintf_P(buf, sizeof(buf), PSTR("RX %s"), chip);
    } else {
      snprintf_P(buf, sizeof(buf), PSTR("RX waiting"));
    }
    drawStrP(0, uptime_y + 18, buf);

    uint16_t loss;
    if (rx->loss_pct_x10(&loss)) {
      snprintf_P(buf, sizeof(buf), PSTR("%u.%u%%"),
                 (unsigned)(loss / 10), (unsigned)(loss % 10));
      int16_t w = (int16_t)getStrWidth(buf);
      drawStrP((int16_t)OLED_WIDTH - w, uptime_y + 18, buf);
    }
  }
#endif
}

void SSD1306Display::page_one_clear() {
  clearBuffer();
  drawTPString(*this, 0,  5, DialogInput_plain_12, PSTR("CPM:"));
  drawXBMP(0, 20, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_mu);
  drawTPString(*this, EG_GLYPH_W, 20, DialogInput_plain_12, PSTR("Sv/h:"));
  _p1_last.cpm_x10 = 0xFFFFFFFFu;   // makes the values redraw, wifi icon and all
}

// After 3 bad transfers in a row nothing goes out, apart from one try a minute
static const uint8_t BUS_FAILS = 3;
static const uint32_t BUS_RETRY_MS = 60000;
static uint8_t s_bus_fails = 0;
static uint32_t s_bus_retry = 0;
static bool s_bus_skip = false;

static void bus_result(bool ok) {
  if (ok) {
    if (s_bus_fails >= BUS_FAILS) {
      corium::Log::console(PSTR("OLED: Back"));
      oled.oled_last_update = 0;   // full frame
    }
    s_bus_fails = 0;
    return;
  }
  s_bus_retry = corium::fastMillis() + BUS_RETRY_MS;
  if (s_bus_fails < BUS_FAILS && ++s_bus_fails == BUS_FAILS) {
    corium::Log::console(PSTR("OLED: Not answering, retry every 60 s"));
  }
}

// Same as u8g2's hw i2c, but without Wire.begin (I2CBus did that) and it keeps the result
static uint8_t oled_i2c(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      s_bus_skip = s_bus_fails >= BUS_FAILS && !corium::reached(corium::fastMillis(), s_bus_retry);
      if (s_bus_skip) break;   // esp32 Wire holds its lock until endTransmission
      Wire.setClock(u8x8->bus_clock);
      Wire.beginTransmission(u8x8_GetI2CAddress(u8x8) >> 1);
      break;
    case U8X8_MSG_BYTE_SEND:
      if (!s_bus_skip) Wire.write((uint8_t*)arg_ptr, arg_int);
      break;
    case U8X8_MSG_BYTE_END_TRANSFER:
      if (!s_bus_skip) bus_result(Wire.endTransmission() == 0);
      break;
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
      break;
    default:
      return 0;
  }
  return 1;
}

// u8g2's setup per display.type
#define OLED_U8G2_0 u8g2_Setup_ssd1306_i2c_128x64_noname_f
#define OLED_U8G2_1 u8g2_Setup_sh1106_i2c_128x64_noname_f
#define OLED_U8G2_2 u8g2_Setup_ssd1309_i2c_128x64_noname0_f
#define OLED_U8G2_3 u8g2_Setup_ssd1306_i2c_72x40_er_f
#define OLED_U8G2_4 u8g2_Setup_sh1106_i2c_72x40_wise_f
#define OLED_U8G2_9 u8g2_Setup_ssd1306_i2c_128x32_univision_f
#define OLED_U8G2_(t) OLED_U8G2_##t
#define OLED_U8G2(t)  OLED_U8G2_(t)
#define OLED_CASE(t)  (DISP_HAS(t) && (t) != OLED_TYPE)

// Only the types built in, the build's own type takes the rest
void SSD1306Display::setup_driver() {
  switch (_type) {
#if OLED_CASE(1)
    case DISP_SH1106:
      OLED_U8G2_1(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
#if OLED_CASE(2)
    case DISP_SSD1309:
      OLED_U8G2_2(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
#if OLED_CASE(3)
    case DISP_SSD1306_72X40:
      OLED_U8G2_3(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
#if OLED_CASE(4)
    case DISP_SH1106_72X40:
      OLED_U8G2_4(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
#if OLED_CASE(9)
    case DISP_SSD1306_128X32:
      OLED_U8G2_9(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
#if OLED_CASE(0)
    case DISP_SSD1306:
      OLED_U8G2_0(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
    default:
      OLED_U8G2(OLED_TYPE)(&_u8g2, U8G2_R0, oled_i2c, u8x8_gpio_and_delay_arduino);
      break;
  }
#if OLED_SIZES > 1
  s_tile_rows = u8g2_GetBufferTileHeight(&_u8g2);
#if DISP_HAS_TINY
  s_width = u8g2_GetBufferTileWidth(&_u8g2) << 3;
#endif
#endif
}

void SSD1306Display::reset_panel() {
  if (_pin_rst != 255) {
    pinMode(_pin_rst, OUTPUT);
    digitalWrite(_pin_rst, HIGH); delay(50);
    digitalWrite(_pin_rst, LOW);  delay(10);
    digitalWrite(_pin_rst, HIGH); delay(50);
  }

#if DISP_HAS_TINY
  // Tiny has no RST and can wedge the bus, so clock it 9 times then send a STOP.
  // No START, so other i2c devices aren't upset
  if (isTiny()) {
    pinMode(_pin_sda, INPUT_PULLUP);
    pinMode(_pin_scl, OUTPUT);
    digitalWrite(_pin_scl, HIGH); delayMicroseconds(10);
    for (uint8_t i = 0; i < 9; i++) {
      digitalWrite(_pin_scl, LOW);  delayMicroseconds(10);
      digitalWrite(_pin_scl, HIGH); delayMicroseconds(10);
    }
    digitalWrite(_pin_scl, LOW);  delayMicroseconds(10);
    pinMode(_pin_sda, OUTPUT);
    digitalWrite(_pin_sda, LOW);  delayMicroseconds(10);
    digitalWrite(_pin_scl, HIGH); delayMicroseconds(10);
    digitalWrite(_pin_sda, HIGH); delayMicroseconds(10);   // STOP
    pinMode(_pin_sda, INPUT_PULLUP);
  }
#endif
}

// SA0 low = 0x3C, high = 0x3D
static uint8_t oled_probe_addr() {
  static const uint8_t probe_addrs[] = { 0x3c, 0x3d };
  for (uint8_t i = 0; i < sizeof(probe_addrs); i++) {
    Wire.beginTransmission(probe_addrs[i]);
    if (Wire.endTransmission() == 0) return probe_addrs[i];
  }
  return (uint8_t)0;
}

void SSD1306Display::draw_logo(const char* line) {
#if DISP_HAS_TINY
  if (isTiny()) {
    drawXBMP(0, 0, 25, 25, ESPLogoTiny);
    setFont(U8G2_FONT_ARIAL_10);
    drawStrP(28, 7, RELEASE_VERSION);
    drawStrP(0, 28, line);
    return;
  }
#endif
#if DISP_HAS_SHORT
  if (isShort()) {
    drawXBMP(0, 3, 25, 25, ESPLogoTiny);
    lines(30, APP_NAME, RELEASE_VERSION, line);
    return;
  }
#endif
  drawXBMP(0, 0, 51, 51, ESPLogo);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(55, 10, APP_NAME);
  drawStrP(55, 24, RELEASE_VERSION);
  drawStrP(55, 42, line);
}

// Stops the row sends so the frame starts clear
bool SSD1306Display::take_screen() {
  if (!_present) return false;
  _send_rows_left = 0;
  clearBuffer();
  return true;
}

void SSD1306Display::draw_splash() {
  clearBuffer();
#if DISP_HAS_TINY
  if (isTiny()) {
    drawXBMP(0, 7, 25, 25, ESPLogoTiny);
    setFont(U8G2_FONT_ARIAL_10);
    drawStr(28, 14, RELEASE_VERSION);
  } else
#endif
  draw_logo(PSTR("Connecting .."));
  sendBuffer();
}

static void oled_lifecycle(corium::Lifecycle::Event e) {
  if (e == corium::Lifecycle::OTA_START) oled.showOTABanner();
  if (e == corium::Lifecycle::OTA_FAILED) oled.endOTABanner();
#if DISP_HAS_TINY
  // tiny keeps its image over a reset, so blank it
  if (e == corium::Lifecycle::SHUTDOWN && oled.present()) {
    oled.clearBuffer();
    oled.sendBuffer();
  }
#endif
}

void SSD1306Display::setup() {
  corium::Lifecycle::on(oled_lifecycle);
  setup_driver();
  // Only allocated when chosen, so a MAX7219 build keeps the 1 KB
  if (!_frame) _frame = (uint8_t*)malloc(u8g2_GetBufferSize(&_u8g2));
  if (!_frame) {
    corium::Log::console(PSTR("OLED: No memory for the frame"));
    return;
  }
  u8g2_SetBufferPtr(&_u8g2, _frame);
  reset_panel();

  uint8_t addr = corium::I2CBus::begin() ? oled_probe_addr() : 0;
  if (!addr) {
    _present = false;
    corium::Log::console(PSTR("OLED: No display detected at 0x3C/0x3D (sda=%d scl=%d)"),
                         _pin_sda, _pin_scl);
    corium::Registry::every(this, NOPANEL_LOOP_MS);
    setFontPosTop();
    return;
  }
  _present = true;
  corium::Log::console(PSTR("OLED: Detected at 0x%02X (type=%u)"), addr, _type);
  setI2CAddress(addr << 1);
  setBusClock(700000);
  u8g2_InitDisplay(&_u8g2);
  u8g2_SetPowerSave(&_u8g2, 0);
  // not ClearDisplay, tiny SSD1306 ignores it after a soft reset
  u8g2_ClearBuffer(&_u8g2);
  u8g2_SendBuffer(&_u8g2);
  setFontPosTop();
  bool flipped = corium::Prefs::getBool("display", K_FLIP);
  // Inverted compared to ThingPulse, u8g2's init is already flipped
  setFlipMode(flipped ? 0 : 1);
  setContrast(64);
  draw_splash();
}

// only called while lit, Display does the waking
void SSD1306Display::button(unsigned long now, int8_t direction) {
  if (direction > 0) {
    oled_page++;
    if (oled_page > PAGE_LAST) oled_page = PAGE_MAIN;
  } else {
    if (oled_page <= PAGE_MAIN) oled_page = PAGE_LAST;
    else oled_page--;
  }
  redraw(now);
}

void SSD1306Display::fx(unsigned long now) {
  oled_page = PAGE_SAVER;
  redraw(now);
}

void SSD1306Display::redraw(unsigned long now) {
  oled_last_update = 0;
  _send_rows_left = 0;
  _next_render_due = now;
  loop(now);
}

void SSD1306Display::setup_wifi(const char* s) {
  if (!take_screen()) return;
#if DISP_HAS_TINY
  if (isTiny()) {
    setFont(U8G2_FONT_ARIAL_10);
    drawStrP(0, 2,  PSTR("Setup portal"));
    const char* host = s ? s : "";
    const char* dash = strrchr(host, '-');
    char line[16];
    snprintf_P(line, sizeof(line), PSTR("AP: %s"), dash ? dash + 1 : host);
    drawStr(0, 15, line);
    sendBuffer();
    return;
  }
#endif
  if (isShort()) {
    lines(0, PSTR("Setup portal"), PSTR("Join WiFi:"), s);
    sendBuffer();
    return;
  }
  drawTPString(*this, 0, 10, DialogInput_plain_12, PSTR("Setup portal"));
  drawTPString(*this, 0, 24, DialogInput_plain_12, PSTR("Join WiFi:"));
  drawTPString(*this, 0, 38, DialogInput_plain_12, s);
  sendBuffer();
}

void SSD1306Display::safe_mode() {
  start();
  if (!_present) return;
  clearBuffer();
  drawTPString(*this, 0, 6, DialogInput_plain_12, PSTR("Safe mode"));
  sendBuffer();
}

void SSD1306Display::wifiDisabled() {
  if (!take_screen()) return;
  draw_logo(PSTR("Offline mode"));
  sendBuffer();
}

void SSD1306Display::wipeCountdown(int seconds_left) {
  if (!take_screen()) return;
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 0,  PSTR("Offline mode armed"));
  char line[24];
  if (isShort()) {
    drawStrP(0, 11, PSTR("Hold to factory reset"));
    snprintf_P(line, sizeof(line), PSTR("%ds"), seconds_left);
    drawStr(0, 22, line);
    sendBuffer();
    return;
  }
  drawStrP(0, 16, PSTR("Hold to factory reset"));
  snprintf_P(line, sizeof(line), PSTR("%ds"), seconds_left);
  setFont(U8G2_FONT_ARIAL_24);
  drawStrP(0, 32, line);
  sendBuffer();
}

void SSD1306Display::wipeReady() {
  if (!take_screen()) return;
  if (isShort()) {
    lines(0, PSTR("Release for factory reset"), PSTR("Keep holding"), PSTR("to cancel."));
    sendBuffer();
    return;
  }
  setFont(U8G2_FONT_ARIAL_16);
  drawStrP(0, 0,  PSTR("Release for"));
  drawStrP(0, 18, PSTR("factory reset"));
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 44, PSTR("Keep holding"));
  drawStrP(0, 54, PSTR("to cancel."));
  sendBuffer();
}

#ifdef GEIGER_PUSHBUTTON
void SSD1306Display::screen_mode(unsigned long now, bool auto_off) {
  if (!_started) return;
  _send_rows_left = 0;
  clearBuffer();
  const uint8_t* font = U8G2_FONT_ARIAL_16;
  int16_t y = 20;
#if DISP_HAS_TINY
  if (isTiny()) font = U8G2_FONT_ARIAL_10;
#endif
  if (isShort()) y = 16;
  setFont(font);
  drawStrP(0, 0,  EG_W_SCREEN);
  drawStrP(0, y, auto_off ? PSTR("auto off") : PSTR("always on"));
  if (_present) { setPowerSave(0); sendBuffer(); }
  oled_on = true;
  oled_last_update = 0;
  _next_render_due = now + 1500;   // the message stays up for 1.5 s
  corium::Registry::every(this, LOOP_MS);
}
#endif

bool SSD1306Display::page_one_graph() {
  if (isShort()) return graph_short();
  int hist[HIST];
  uint16_t n = (uint16_t)egm_history(hist, HIST);
  int32_t lo = 0;
  int32_t hi = 0;
  int k = 1;
  bool counts = n > 0 && hist_range(hist, n, lo, hi, k);

  setDrawColor(0);
  drawBox(0, GRAPH_TOP, OLED_WIDTH, GRAPH_BOT - GRAPH_TOP + 1);
  setDrawColor(1);
  drawLine(0, GRAPH_BOT, GRAPH_W, GRAPH_BOT);
  drawLine(GRAPH_W, GRAPH_TOP, GRAPH_W, GRAPH_BOT);
  if (n > 0) {
    int x_start = 2 * (HIST - n);
    char buf[8];
    if (!counts) {
      snprintf_P(buf, sizeof(buf), W_FMT_D, 0);
      drawTPString(*this, LABEL_X, 55, Open_Sans_Digits_10, buf);
      return true;
    }
    for (uint16_t i = 0; i < n; i++) {
      int y = ((map((long)hist[i], (long)lo, (long)hi, 0, GRAPH_H)) * (-1)) + GRAPH_BOT - 1;
      drawFrame(x_start + i * 2, y, 2, (GRAPH_BOT - y));
    }
    if (k > 1) { lo /= 10; hi /= 10; }
    snprintf_P(buf, sizeof(buf), W_FMT_D, (int)lo);
    drawTPString(*this, LABEL_X, 55, Open_Sans_Digits_10, buf);
    snprintf_P(buf, sizeof(buf), W_FMT_D, (int)hi);
    drawTPString(*this, LABEL_X, GRAPH_TOP, Open_Sans_Digits_10, buf);
  }
  return true;
}

// Compared with 5 s ago, a trend needs 1.5 sigma
static int8_t cpm_trend(uint32_t cpm) {
  int last5[5];
  if (egm_history(last5, 5) != 5) return 0;
  int32_t past = last5[0];
  int32_t diff = (int32_t)cpm - past;
  int32_t thresh = (int32_t)(poisson_std((float)past) * 1.5f + 0.5f);
  if (thresh < 2) thresh = 2;
  if (diff > thresh) return 1;
  if (diff < -thresh) return -1;
  return 0;
}

// false = nothing changed
bool SSD1306Display::page_one_values(unsigned long now) {
  float c = display.cpm(), c5 = c < 10.0f ? display.cpm5() : c;
  bool slow = use_cpm5(c, c5);
  float dcpm = slow ? c5 : c;
  float dratio = display.ratio();
  float dusv = (dratio > 0.0f) ? dcpm / dratio : 0.0f;
  uint32_t cpm_x10 = (uint32_t)CounterMaths::to_int(dcpm * 10.0f);
  int32_t  usv_x100 = CounterMaths::to_int(dusv * 100.0f);
  uint8_t mode = !display.warm() ? P1_WARMING : slow ? P1_SLOW : P1_LIVE;
  uint8_t flags = (corium::Wifi::disabled ? P1_WIFI_OFF : 0) | (corium::Wifi::connected ? P1_WIFI_UP : 0)
                | (display.no_counts() ? P1_NO_COUNTS : 0)
                | (display.alert() ? P1_ALERT : display.warn() ? P1_WARN : 0);
  P1 p = { cpm_x10, usv_x100, mode, _sent_mark, flags, cpm_trend(cpm_x10 / 10) };
  if (p == _p1_last) return false;
  _p1_last = p;
  draw_p1_values(p, dusv);
  return true;
}

// Where page one's values go: the clear boxes, glyph, cpm, usv and the W/S row
struct P1At { uint8_t b1w, b1h, b2x, b2y, b2w, b2h, gx, gy, cx; int8_t cy; uint8_t vx, vy, my; };
static const P1At P1_64 PROGMEM = {92, 20, 42, 20, 86, 12, 37, 2, 45, 0, 45, 20, 2};   // the colon of "Sv/h:" ends at 41
static const P1At P1_32 PROGMEM = {128, 16, 0, 16, 81, 16, 1, 0, 11, -3, 0, 16, 0};   // the graph starts at 82, page_one_clear's labels get covered

static const uint8_t* trend_glyph(bool warn, int8_t trend) {
  if (warn) return EGGlyph_warning;
  if (trend > 0) return EGGlyph_arrow_up;
  if (trend < 0) return EGGlyph_arrow_down;
  return nullptr;
}

// 128x32 adds the unit
static void draw_usv(SSD1306Display& d, bool none, float dusv, int16_t x, int16_t y, bool unit) {
  if (none) {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
    drawTPString(d, x, y, DialogInput_plain_12, PSTR("no signal?"));
#else
    drawTPString(d, x, y, DialogInput_plain_12, PSTR("check tube?"));
#endif
    return;
  }
  char buf[16];
  format_f(buf, sizeof(buf), dusv);
  drawTPString(d, x, y, DialogInput_plain_12, buf);
  if (!unit) return;
  x += (int16_t)strlen(buf) * pgm_read_byte(DialogInput_plain_12) + 3;
  d.drawXBMP(x, y, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_mu);
  drawTPString(d, x + EG_GLYPH_W, y, DialogInput_plain_12, PSTR("Sv/h"));
}

void SSD1306Display::draw_p1_values(const P1& p, float dusv) {
  P1At a;
  memcpy_P(&a, isShort() ? &P1_32 : &P1_64, sizeof(a));
  setDrawColor(0);
  drawBox(OLED_WIDTH - a.b1w, 0, a.b1w, a.b1h);
  drawBox(a.b2x, a.b2y, a.b2w, a.b2h);
  setDrawColor(1);
  const uint8_t* g = trend_glyph(p.flags & (P1_WARN | P1_ALERT), p.trend);
  if (p.flags & P1_ALERT) {
    drawBox(a.gx - 1, a.gy, EG_GLYPH_W + 2, EG_GLYPH_H);
    setDrawColor(0);   // solid bitmap, black on white
  }
  if (g) drawXBMP(a.gx, a.gy, EG_GLYPH_W, EG_GLYPH_H, g);
  setDrawColor(1);
  char buf[16];
  format_cpm(buf, sizeof(buf), p.cpm_x10 * 0.1f);
  drawTPString(*this, a.cx, a.cy, DialogInput_Digits_17, buf);
  draw_usv(*this, p.flags & P1_NO_COUNTS, dusv, a.vx, a.vy, isShort());
  if (isShort()) {
    setFont(U8G2_FONT_ARIAL_10);
    drawStrP(70, 3, PSTR("CPM"));
  }
  if (p.mode != P1_LIVE) {
    drawTPString(*this, 98, a.my, DialogInput_plain_12, p.mode == P1_WARMING ? PSTR("W") : PSTR("S"));
  }
  if (p.snd) {
    drawXBMP(110, 0, 8, 16, _iconimage_remotext);
    _sent_mark--;
  }
  if (!corium::Wifi::disabled) {
    drawXBMP(120, 0, 8, 16, corium::Wifi::connected ? _iconimage_connected : _iconimage_disconnected);
  }
}

void SSD1306Display::page_three_full() {
  static const char ME[] PROGMEM = "@ steadramon";
  clearBuffer();
  lines(0, GEIGER_MODEL, PSTR(RELEASE_VERSION " / " GIT_VERSION), corium::DeviceInfo::buildDate());
  if (!isShort()) {
    drawStrP(0, 47, ME);
    return;
  }
  // Right of the model, if it fits
  char at[16];
  flashToRam(ME, at, sizeof(at));   // getStrWidth reads plain bytes, not flash
  int16_t w = getStrWidth(at);
  if (getStrWidth(GEIGER_MODEL) + w + 4 <= OLED_WIDTH) drawStr(OLED_WIDTH - w, 0, at);
}

namespace {
static const unsigned long MTX_T1 = 100;
static const unsigned long MTX_T2 = 300;
static const unsigned long MTX_T3 = 800;
static const unsigned long MTX_T4 = 2000;

struct MatrixCtx {
  uint8_t cps_factor;
};

static MatrixCtx matrix_ctx(unsigned long typical) {
  MatrixCtx c;
  if      (typical < 200)  c.cps_factor = 8;
  else if (typical < 500)  c.cps_factor = 7;
  else if (typical < 1000) c.cps_factor = 6;
  else if (typical < 2000) c.cps_factor = 5;
  else                     c.cps_factor = 4;
  return c;
}

static void drop_speed(unsigned long delta_ms, const MatrixCtx& ctx,
                       uint8_t multiplier, uint32_t r,
                       uint8_t& out_speed, uint8_t& out_tail) {
  uint8_t s_min = 2, s_max = 3;
  if      (delta_ms < MTX_T1) { s_min = 6; s_max = 9; }
  else if (delta_ms < MTX_T2) { s_min = 5; s_max = 8; }
  else if (delta_ms < MTX_T3) { s_min = 5; s_max = 7; }
  else if (delta_ms < MTX_T4) { s_min = 3; s_max = 5; }
  // 0..range-1
  uint16_t r_band = (r >> 21) & 0x7FF;
  uint8_t band_speed = s_min + (uint8_t)(((uint32_t)r_band * (s_max - s_min + 1)) >> 11);
  uint16_t raw_spd = (uint16_t)band_speed * multiplier * ctx.cps_factor;
  uint8_t spd = (uint8_t)(raw_spd >> 3);
  spd = (spd + 4) & 0xF8;
  if (spd < 8) spd = 8;
  uint8_t tail_max = (spd > 30) ? 30 : spd;
  uint8_t tail_min = spd >> 3;
  if (tail_min < 2) tail_min = 2;
  if (tail_max < tail_min) tail_max = tail_min;
  uint8_t r_jitter = (uint8_t)(r >> 24);
  uint8_t tail = (tail_max == tail_min)
                 ? tail_min
                 : (tail_min + (uint8_t)(((uint16_t)r_jitter * (tail_max - tail_min + 1)) >> 8));
  out_speed = spd;
  out_tail  = tail;
}

static const unsigned long MTX_IDLE_MS = 1500;   // no counts yet
static const uint8_t MTX_SEED_DROPS = 6;
static const uint8_t MTX_FULL = 8;               // alive factor once 3 drops are running
static unsigned long s_mtx_blip_us = 0;
static uint8_t s_mtx_col = 255;                  // 255 = none yet

static void seed_drops(Drop* drops) {
  s_mtx_blip_us = display.blip();
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) drops[i].alive = false;
  int hist[HIST];
  size_t hist_n = egm_history(hist, HIST);
  float cps = display.cps();
  unsigned long typical = (cps > 0.05f) ? (unsigned long)(1000.0f / cps) : MTX_IDLE_MS;
  MatrixCtx ctx = matrix_ctx(typical);
  for (uint8_t i = 0; i < MTX_SEED_DROPS; i++) {
    uint32_t r = GRNG::fast_uint32();
    unsigned long sample_delta_ms = 5000;
    if (hist_n > 0) {
      int cpm_sample = hist[(r >> 12) % hist_n];
      if (cpm_sample > 0) sample_delta_ms = 60000UL / (unsigned long)cpm_sample;
    }
    uint8_t spd, tail;
    drop_speed(sample_delta_ms, ctx, MTX_FULL, r, spd, tail);
    drops[i].alive = true;
    drops[i].x = (uint8_t)((r >> 4) & 0x7F);
    drops[i].y = (int16_t)((r >> 26) & 0x3F);
    drops[i].speed = spd;
    drops[i].tail = tail;
    drops[i].seed = (uint8_t)GRNG::fast_uint32();
  }
}

// Average ms between clicks over the last 5 s
static unsigned long recent_gap_ms() {
  int recent[5];
  uint8_t  n   = (uint8_t)egm_history(recent, 5);
  uint32_t sum = 0;
  for (uint8_t k = 0; k < n; k++) sum += (uint32_t)recent[k];
  if (n == 0 || sum == 0) return MTX_IDLE_MS;
  uint32_t avg_cpm = sum / n;
  return avg_cpm ? 60000UL / avg_cpm : MTX_IDLE_MS;
}

static Drop* free_drop(Drop* drops) {
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) if (!drops[i].alive) return &drops[i];
  return nullptr;
}

static uint8_t pick_col(uint32_t r, uint8_t thresh, uint8_t shift) {
  uint8_t col = (uint8_t)(r & 0x7F);
  int8_t diff = (int8_t)col - (int8_t)s_mtx_col;
  if (diff < 0) diff = -diff;
  if (s_mtx_col < 128 && diff < thresh) col = (col + shift) & 0x7F;
  s_mtx_col = col;
  return col;
}

static void spawn_drops(Drop* drops, unsigned long blip_us) {
  unsigned long delta_ms = (blip_us - s_mtx_blip_us) / 1000;
  s_mtx_blip_us = blip_us;
  MatrixCtx ctx = matrix_ctx(recent_gap_ms());
  uint8_t alive_count = 0;
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) if (drops[i].alive) alive_count++;
  uint8_t alive_factor = (alive_count < 3) ? (4 + alive_count * 2) : MTX_FULL;
  uint8_t to_spawn = 1;
  if      (delta_ms < MTX_T1) to_spawn = 3;
  else if (delta_ms < MTX_T2) to_spawn = 2;
  uint8_t prox_thresh = (ctx.cps_factor >> 1);
  uint8_t prox_shift  = prox_thresh + 3;
  for (uint8_t s = 0; s < to_spawn; s++) {
    Drop* d = free_drop(drops);
    if (!d) return;
    uint32_t r = GRNG::fast_uint32();
    uint8_t col = pick_col(r, prox_thresh, prox_shift);
    uint8_t spd, tail;
    drop_speed(delta_ms, ctx, alive_factor, r, spd, tail);
    d->alive = true;
    d->x = col;
    d->y = (alive_factor < MTX_FULL) ? 0 : -(int16_t)((r >> 16) & 0x1F);
    d->speed = spd;
    d->tail  = tail;
    d->seed  = (uint8_t)(GRNG::fast_uint32() & 0xFF);
    alive_count++;
    if (alive_count >= 3) alive_factor = MTX_FULL;
  }
}

static void draw_drop(SSD1306Display& o, Drop& d) {
  const uint8_t BODY = 2;
  uint8_t fade_span = (d.tail > BODY) ? (d.tail - BODY) : 1;
  uint16_t skip_step = 256 / fade_span;
  for (uint8_t t = 0; t < d.tail; t++) {
    int16_t ty = d.y - (t << 1);
    if (ty < 0 || ty >= s_tile_rows << 3) continue;
    if (t >= BODY) {
      uint16_t skip_prob = (uint16_t)(t - BODY) * skip_step;
      uint8_t h = (uint8_t)(d.seed * 53u + t * 97u);
      if (h < skip_prob) continue;
    }
    o.drawPixel(d.x, ty);
  }
  d.y += d.speed >> 3;
  if (d.y - (d.tail << 1) >= s_tile_rows << 3) d.alive = false;
}

}  // namespace

void SSD1306Display::page_four_matrix() {
  Drop*& drops = s_matrix_drops;
  if (!drops) {
    drops = (Drop*)calloc(MATRIX_DROPS, sizeof(Drop));
    if (!drops) return;
  }
  if (_page4_ms == 0) {
    _page4_ms = corium::fastMillis();
    seed_drops(drops);
  }
  unsigned long blip_us = display.blip();
  if (blip_us != s_mtx_blip_us) spawn_drops(drops, blip_us);

  clearBuffer();
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) {
    if (drops[i].alive) draw_drop(*this, drops[i]);
  }
}

void SSD1306Display::page_four_static() {
  unsigned long now = corium::fastMillis();
  if (!corium::fresh(now, _page4_ms, 1000)) {
    _page4_ms = now;
    uint16_t num;
    GRNG::extract((uint8_t*)&num, sizeof(num));
    num %= 1001;
    snprintf_P(_page4_num, sizeof(_page4_num), W_FMT_U, num);
  }
  uint8_t*& noise = s_static_noise;
  if (!noise) noise = (uint8_t*)malloc(1024);
  clearBuffer();
  if (noise) {
    GRNG::extract_fast(noise, 1024);
    drawXBMP(0, 0, 128, 64, noise);
  }
  int16_t y = 19;
  if (isShort()) y = 4;
  setDrawColor(0);
  drawBox(41, y, 44, 24);
  setDrawColor(1);
  drawTPString(*this, 63 - (int)strlen(_page4_num) * 5, y + 2,
               DialogInput_Digits_17, _page4_num);
}

void SSD1306Display::showOTABanner() {
  if (!take_screen()) return;
  setFont(U8G2_FONT_ARIAL_16);
  // drawStr isn't PROGMEM safe, copy to ram
  char l1[12], l2[20];
  flashToRam(W_UPDATE, l1, sizeof(l1));
  snprintf_P(l2, sizeof(l2), PSTR("in progress..."));
  int16_t y1 = (OLED_HEIGHT / 2) - 20, y2 = OLED_HEIGHT / 2;
  if (isShort()) { y1 = 0; y2 = 16; }
  drawStr((OLED_WIDTH - getStrWidth(l1)) / 2, y1, l1);
  drawStr((OLED_WIDTH - getStrWidth(l2)) / 2, y2, l2);
  setPowerSave(0);
  oled_on = true;
  sendBuffer();
  _ota_lock = true;
}

// The update failed, go back to the normal screen
void SSD1306Display::endOTABanner() {
  if (!_ota_lock) return;
  _ota_lock = false;
  oled_last_update = 0;
  _next_render_due = corium::fastMillis();
  corium::Registry::every(this, loop_ms());
}

bool SSD1306Display::screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) {
  buf = getBufferPtr();
  if (!buf) return false;   // no driver yet either
  w = u8g2_GetBufferTileWidth(&_u8g2) * 8;
  h = u8g2_GetBufferTileHeight(&_u8g2) * 8;
  return true;
}
#endif
