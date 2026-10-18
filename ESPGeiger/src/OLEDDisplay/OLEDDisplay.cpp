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
#include <EGFormat.h>
#include "fonts.h"
#include "logo.h"
#include "Display/Display.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/System/I2CBus.h"
#include "Corium/System/PinSafety.h"
#include "Corium/System/Uptime.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/Words.h"
#include "GRNG/GRNG.h"
#include "GeigerInput/Type/UdpRx.h"
#ifdef ESPG_HV_ADC
#include "HV/HV.h"
#endif

EG_PSTR(K_RST,  "rst");
EG_PSTR(K_FLIP, "flip");

EG_PSTR(M_CPM,   "cpm");
EG_PSTR(M_CPM5,  "cpm5");
EG_PSTR(M_USV,   "usv");
EG_PSTR(M_CPS,   "cps");
EG_PSTR(M_RATIO, "ratio");
EG_PSTR(M_WARM,  "warm");
EG_PSTR(M_NONE,  "no_counts");
EG_PSTR(M_BLIP,  "blip");

static const uint8_t HIST = 45;   // seconds

static void format_cpm(char* buf, size_t bufsz, float cpm) {
  if (cpm < 10.0f) format_f(buf, bufsz, cpm, 1);
  else             snprintf_P(buf, bufsz, W_FMT_U, (unsigned)(cpm + 0.5f));
}

static float disp_cpm() {
  float c = egm_f(M_CPM);
  return c < 10.0f ? display_cpm(c, egm_f(M_CPM5)) : c;
}

// Page 4 only, freed on exit
struct Drop { int16_t y; uint8_t x; uint8_t speed; uint8_t tail; uint8_t seed; bool alive; };
static const uint8_t MATRIX_DROPS = 32;
static Drop*    s_matrix_drops = nullptr;
static uint8_t* s_static_noise = nullptr;

// loop rates, ms. no panel: /screen polls every 500 ms
static const uint16_t TICK_MS           = 20;
static const uint16_t SAVER_TICK_MS     = 10;
static const uint16_t NOPANEL_TICK_MS   = 250;
static const uint16_t NOPANEL_SAVER_MS  = 80;
static const uint16_t ASLEEP_TICK_MS    = 250;
static const uint16_t DARK_TICK_MS      = 30000;
static const uint16_t LOCKED_TICK_MS    = 1000;
// redraws, ms
static const unsigned long FRAME_MS         = 500;
static const unsigned long NOPANEL_FRAME_MS = 250;
static const unsigned long SAVER_FRAME_MS   = 80;
static const unsigned long MAIN_FULL_MS     = 10000;
static const unsigned long NET_MS           = 1000;
static const unsigned long ABOUT_MS         = 10000;
static const uint8_t TILE_COLS = OLED_WIDTH >> 3;
static const uint8_t TILE_ROWS = OLED_HEIGHT >> 3;

SSD1306Display oled;
EG_REGISTER_MODULE(oled)

// stand-ins for ThingPulse ArialMT_Plain
#define U8G2_FONT_ARIAL_10  u8g2_font_helvR08_tr
#define U8G2_FONT_ARIAL_16  u8g2_font_helvR12_tr
#define U8G2_FONT_ARIAL_24  U8G2_FONT_ARIAL_16

static const char PIN_OWNER[] PROGMEM = "OLED";

// NOLINTNEXTLINE(complexity) hot path
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
  const uint8_t W = OLED_WIDTH;
  const uint8_t PAGES = OLED_HEIGHT >> 3;

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
    const uint8_t* glyph = glyph_table + goff;

    uint8_t col = 0;
    uint8_t bc = 0;
    int8_t  page0 = y_top >> 3;
    uint8_t y_off0 = y_top & 7;
    int8_t  page  = page0;
    uint8_t y_off = y_off0;
    for (uint16_t i = 0; i < byte_count; i++) {
      uint8_t pixels = pgm_read_byte(glyph + i);
      if (pixels) {
        int16_t tx = cx + col;
        if ((uint16_t)tx < W) {
          if ((uint8_t)page < PAGES) {
            fb[(uint16_t)page * W + tx] |= (uint8_t)(pixels << y_off);
          }
          if (y_off != 0 && (uint8_t)(page + 1) < PAGES) {
            fb[(uint16_t)(page + 1) * W + tx] |= (uint8_t)(pixels >> (8 - y_off));
          }
        }
      }
      if (++bc >= bytes_per_col) {
        bc = 0; col++;
        page = page0; y_off = y_off0;
      } else {
        page++;
      }
    }
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

void SSD1306Display::load_pins() {
#ifndef OLED_PINS_BLOCKED
  {
    uint8_t sda = (uint8_t)EGPrefs::getUInt("i2c", EG_K_SDA);
    if (const char* why = PinSafety::claim_output((int)sda, PinSafety::I2C)) {
      Log::console(PSTR("OLED: sda=%u unsafe (%s) - keeping default %u"), sda, why, _pin_sda);
      PinSafety::claim((int)_pin_sda, PinSafety::I2C);
    } else {
      _pin_sda = sda;
    }
  }
  {
    uint8_t scl = (uint8_t)EGPrefs::getUInt("i2c", EG_K_SCL);
    if (const char* why = PinSafety::claim_output((int)scl, PinSafety::I2C)) {
      Log::console(PSTR("OLED: scl=%u unsafe (%s) - keeping default %u"), scl, why, _pin_scl);
      PinSafety::claim((int)_pin_scl, PinSafety::I2C);
    } else {
      _pin_scl = scl;
    }
  }
  {
    uint8_t rst = (uint8_t)EGPrefs::getUInt("display", K_RST);
    if (rst != 255) {
      if (const char* why = PinSafety::claim_output((int)rst, PIN_OWNER)) {
        Log::console(PSTR("OLED: rst=%u unsafe (%s) - keeping default %u"), rst, why, _pin_rst);
        if (_pin_rst != 255) PinSafety::claim((int)_pin_rst, PIN_OWNER);
      } else {
        _pin_rst = rst;
      }
    } else {
      _pin_rst = 255;
    }
  }
  _pref_display_type = (uint8_t)EGPrefs::getUInt("display", EG_K_TYPE);
#else
  _pref_display_type = OLED_TYPE;
#endif
  I2CBus::pins(_pin_sda, _pin_scl);
}

void SSD1306Display::start() {
  load_pins();
  setup();
  if (!_frame) return;
  _started = true;
  setBrightness((uint8_t)EGPrefs::getUInt("display", EG_K_BRIGHTNESS));
  EGModuleRegistry::set_loop_interval(this, TICK_MS);
}

void SSD1306Display::reload() {
  setBrightness((uint8_t)EGPrefs::getUInt("display", EG_K_BRIGHTNESS));
}

SSD1306Display::SSD1306Display() {
}

void SSD1306Display::setBrightness(uint8_t brightness) {
  if (!_present) return;
  brightness = (uint8_t)(((uint16_t)brightness * 255 + 50) / 100);
  setContrast(brightness);
}

uint16_t SSD1306Display::tick_ms() const {
  bool saver = oled_page == PAGE_SAVER;
  if (_present) return saver ? SAVER_TICK_MS : TICK_MS;
  return saver ? NOPANEL_SAVER_MS : NOPANEL_TICK_MS;
}

unsigned long SSD1306Display::frame_ms() const {
  if (oled_page == PAGE_SAVER) return SAVER_FRAME_MS;
  return _present ? FRAME_MS : NOPANEL_FRAME_MS;
}

void SSD1306Display::page_changed() {
  if (_last_page == PAGE_SAVER) {
    if (s_matrix_drops) { free(s_matrix_drops); s_matrix_drops = nullptr; }
    if (s_static_noise) { free(s_static_noise); s_static_noise = nullptr; }
  }
  _last_page = oled_page;
  _next_render_due = 0;
  EGModuleRegistry::set_loop_interval(this, tick_ms());
}

// false = asleep, nothing to draw
bool SSD1306Display::awake(unsigned long now) {
  if (!_present) return true;
  Display::Sleep sleep = display.sleep(now);
  if (sleep != Display::AWAKE) {
    if (oled_on) { clearBuffer(); setPowerSave(1); oled_on = false; _send_rows_left = 0; }
    EGModuleRegistry::set_loop_interval(this, sleep == Display::TIMED_OUT ? ASLEEP_TICK_MS : DARK_TICK_MS);
    return false;
  }
  if (!oled_on) {
    setPowerSave(0);
    oled_page = PAGE_MAIN;
    oled_on = true;
    oled_last_update = now - MAIN_FULL_MS;
    EGModuleRegistry::set_loop_interval(this, TICK_MS);
  }
  return true;
}

// 0 = never drawn
bool SSD1306Display::due(unsigned long now, unsigned long every) {
  if (oled_last_update != 0 && now - oled_last_update < every) return false;
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
  // values and graph take turns
  _p1_alt = !_p1_alt;
  if ((_p1_alt || full) && page_one_values(now)) dirty |= DR_TOP;
  if ((!_p1_alt || full) && page_one_graph()) dirty |= DR_BOT;
  return dirty;
}

void SSD1306Display::render_saver(unsigned long now) {
  if (oled_last_update == 0) {
    _page4_num_last = 0;
    _page4_variant = random(2);
  }
  oled_last_update = now;
  if (_page4_variant == 0) page_four_static();
  else page_four_matrix();
}

#ifdef ESP32
void SSD1306Display::render_tiny() {
  if (oled_page < PAGE_MAIN || oled_page > PAGE_SAVER) oled_page = PAGE_MAIN;
  switch (oled_page) {
    case 2:  page_tiny_dose();  break;
    case 3:  page_tiny_net();   break;
    case 4:  page_tiny_meter(); break;
    default: page_tiny();       break;
  }
}
#endif

// DR_ flags, what to send
uint8_t SSD1306Display::render(unsigned long now) {
#ifdef ESP32
  if (isTiny()) {
    render_tiny();
    return DR_FULL;
  }
#endif
  switch (oled_page) {
    case PAGE_MAIN:
      return render_main(now);
    case 2:
      if (!due(now, NET_MS)) return DR_NONE;
      page_two_full();
      return DR_FULL;
    case 3:
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
#ifdef ESP32
  // tiny: one go, the row maths runs off the end
  if (isTiny()) {
    sendBuffer();
    return;
  }
#endif
  bool half = (dirty == DR_TOP || dirty == DR_BOT);
  _send_cur = 0;
  _send_ty = (dirty == DR_BOT) ? TILE_ROWS / 2 : 0;
  _send_rows_left = half ? TILE_ROWS / 2 : TILE_ROWS;
}

void SSD1306Display::loop(unsigned long now) {
  if (!_started) {
    EGModuleRegistry::set_loop_interval(this, -1);
    return;
  }
  if (_locked_page || _ota_lock) {
    EGModuleRegistry::set_loop_interval(this, LOCKED_TICK_MS);
    return;
  }
  // one row a tick
  if (_send_rows_left > 0) {
    if (_present) updateDisplayArea(0, _send_ty + _send_cur, TILE_COLS, 1);
    _send_cur++;
    _send_rows_left--;
    return;
  }
  if (oled_page > OLED_PAGES) oled_page = PAGE_MAIN;
  if (oled_page != _last_page) page_changed();
  if (oled_page == PAGE_SAVER) display.wake(now);
  if ((long)(now - _next_render_due) < 0) return;
  _next_render_due = now + frame_ms();
  if (!awake(now)) return;
  uint8_t dirty = render(now);
  if (dirty != DR_NONE && _present) queue_rows(dirty);
}

// tenths, floor 90% of the low, 1 span min, whole ends; false = no counts
static bool hist_range(int* h, uint16_t n, int32_t& lo, int32_t& hi) {
  const int32_t SPAN = 10;
  display_hist(h, n, egm_f(M_CPM5), 10);
  lo = hi = h[0];
  for (uint16_t i = 1; i < n; i++) {
    if (h[i] < lo) lo = h[i];
    if (h[i] > hi) hi = h[i];
  }
  bool counts = hi > 0;
  if (lo > 10) lo = lo * 9 / 10;
  if (hi - lo < SPAN) { lo = hi > SPAN ? hi - SPAN : 0; hi = lo + SPAN; }
  lo = lo / 10 * 10;
  hi = (hi + 9) / 10 * 10;
  return counts;
}

#ifdef ESP32
void SSD1306Display::page_tiny() {
  clearBuffer();
  char buf[12];
  format_cpm(buf, sizeof(buf), disp_cpm());
  setFont(U8G2_FONT_ARIAL_16);
  drawStr(0, 0, buf);
  if (Wifi::connected) drawPixel(71, 0);

  const int CHART_TOP = 18;
  const int CHART_BOT = 39;
  const int CHART_H   = CHART_BOT - CHART_TOP;
  const int CHART_W   = 72;
  int hist[HIST];
  uint16_t n = (uint16_t)egm_history(hist, HIST);
  if (n < 2) return;
  int32_t mn, mx;
  hist_range(hist, n, mn, mx);
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

static const char* fitStr(SSD1306Display& d, const char* src, int max_w) {
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

static const char* tinyUptime() {
  static char buf[12];
  Uptime::Split u = Uptime::split((uint32_t)Uptime::seconds());
  if (u.d > 0) snprintf_P(buf, sizeof(buf), PSTR("%ud %02u:%02u"), u.d, u.h, u.m);
  else         snprintf_P(buf, sizeof(buf), PSTR("%02u:%02u:%02u"),      u.h, u.m, u.s);
  return buf;
}

void SSD1306Display::page_tiny_dose() {
  clearBuffer();
  char buf[12];
  format_f(buf, sizeof(buf), egm_f(M_USV), 2);
  setFont(U8G2_FONT_ARIAL_16);
  drawStr(0, 0, buf);
  if (Wifi::connected) drawPixel(71, 0);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 16, PSTR("uSv/h"));
  char cpmbuf[8];
  format_cpm(cpmbuf, sizeof(cpmbuf), disp_cpm());
  snprintf_P(buf, sizeof(buf), PSTR("%s cpm"), cpmbuf);
  drawStr(0, 28, buf);
}

void SSD1306Display::page_tiny_net() {
  clearBuffer();
  if (Wifi::connected) drawPixel(71, 0);
  setFont(U8G2_FONT_ARIAL_10);
  if (Wifi::connected) {
    char buf[20];
    Wifi::formatIP(buf, sizeof(buf));
    drawStr(0, 2,  fitStr(*this, buf, 71));
    drawStr(0, 15, fitStr(*this, Wifi::ssid, 71));
    snprintf_P(buf, sizeof(buf), PSTR("%d dBm"), (int)Wifi::rssi);
    drawStr(0, 28, buf);
  } else {
    drawStrP(0, 2,  PSTR("WiFi off"));
    drawStr (0, 15, fitStr(*this, DeviceInfo::hostname(), 71));
    drawStr (0, 28, tinyUptime());
  }
}

void SSD1306Display::page_tiny_meter() {
  clearBuffer();
  if (Wifi::connected) drawPixel(71, 0);
  // log10 cpm over 180 deg
  const int px = 36;
  const int py = 36;
  const int r  = 28;
  char buf[8];
  format_cpm(buf, sizeof(buf), disp_cpm());
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
  float cpm = disp_cpm();
  if (cpm < 1.0f) cpm = 1.0f;
  float t = log10f(cpm) / 3.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float ang = (float)M_PI * (1.0f - t);
  float c = cosf(ang), s = sinf(ang);
  drawLine(px, py, px + (int)((r - 3) * c), py - (int)((r - 3) * s));
  drawDisc(px, py, 2);
}

#endif  // ESP32 (page_tiny)

void SSD1306Display::page_two_full() {
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 2, DeviceInfo::hostname());
  drawStrP(0, 17, PSTR("IP:"));
  char ipStr[EG_IPV4_MAX + 1];
  Wifi::formatIP(ipStr, sizeof(ipStr));
  drawStrP(16, 17, ipStr);
  int uptime_y = 32;
#ifdef ESPG_HV_ADC
  drawStrP(0, uptime_y, PSTR("HV:"));
  char hvBuf[12];
  format_f(hvBuf, sizeof(hvBuf), hv.hvReading.get());
  drawStrP(20, uptime_y, hvBuf);
  uptime_y = 47;
#endif
  drawStrP(0, uptime_y, DeviceInfo::uptimeString());
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  GeigerUdpRx* rx = gcounter.udp_rx();
  if (rx) {
    char rxBuf[24];
    const char* chip = rx->locked_chipid();
    if (rx->mode() == 1) {
      snprintf_P(rxBuf, sizeof(rxBuf), PSTR("Sum %u src"), (unsigned)rx->producer_count());
    } else if (chip) {
      snprintf_P(rxBuf, sizeof(rxBuf), PSTR("RX %s"), chip);
    } else {
      snprintf_P(rxBuf, sizeof(rxBuf), PSTR("RX waiting"));
    }
    drawStrP(0, uptime_y + 18, rxBuf);

    if (rx->packets_accepted() > 0) {
      uint16_t loss = rx->loss_pct_x10();
      snprintf_P(rxBuf, sizeof(rxBuf), PSTR("%u.%u%%"),
                 (unsigned)(loss / 10), (unsigned)(loss % 10));
      int16_t w = (int16_t)getStrWidth(rxBuf);
      drawStrP((int16_t)OLED_WIDTH - w, uptime_y + 18, rxBuf);
    }
  }
#endif
}

void SSD1306Display::page_one_clear() {
  clearBuffer();
  drawTPString(*this, 0,  5, DialogInput_plain_12, PSTR("CPM:"));
  drawXBMP(0, 20, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_mu);
  drawTPString(*this, EG_GLYPH_W, 20, DialogInput_plain_12, PSTR("Sv/h:"));
  if (!Wifi::disabled) {
    drawXBMP(120, 0, 8, 16,
             Wifi::connected ? _iconimage_connected : _iconimage_disconnected);
  }
  _p1v_last_cpm = 0xFFFFFFFFu;   // force a redraw
}

void SSD1306Display::setup_driver() {
  switch (_pref_display_type) {
    case DISP_SH1106:
      u8g2_Setup_sh1106_i2c_128x64_noname_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
    case DISP_SSD1309:
      u8g2_Setup_ssd1309_i2c_128x64_noname0_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
#ifdef ESP32
    case DISP_SSD1306_72X40:
      u8g2_Setup_ssd1306_i2c_72x40_er_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
    case DISP_SH1106_72X40:
      u8g2_Setup_sh1106_i2c_72x40_wise_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
#endif
    case DISP_SSD1306:
    default:
      u8g2_Setup_ssd1306_i2c_128x64_noname_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
  }
}

void SSD1306Display::reset_panel() {
  if (_pin_rst != 255) {
    pinMode(_pin_rst, OUTPUT);
    digitalWrite(_pin_rst, HIGH); delay(50);
    digitalWrite(_pin_rst, LOW);  delay(10);
    digitalWrite(_pin_rst, HIGH); delay(50);
  }

#ifdef ESP32
  // tiny has no RST and can wedge, 9 clocks + STOP
  // no START, other i2c devices stay ok
  if (_pref_display_type == DISP_SSD1306_72X40
      || _pref_display_type == DISP_SH1106_72X40) {
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

void SSD1306Display::draw_splash() {
  clearBuffer();
#ifdef ESP32
  if (isTiny()) {
    drawXBMP(0, 7, 25, 25, ESPLogoTiny);
    setFont(U8G2_FONT_ARIAL_10);
    drawStr(28, 14, RELEASE_VERSION);
  } else
#endif
  {
    drawXBMP(0, 0, 51, 51, ESPLogo);
    setFont(U8G2_FONT_ARIAL_10);
    drawStrP(55, 10, W_APP_NAME);
    drawStrP(55, 24, RELEASE_VERSION);
    drawStrP(55, 42, PSTR("Connecting .."));
  }
  sendBuffer();
}

static void oled_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::OTA_START) oled.showOTABanner();
  if (e == Lifecycle::OTA_FAILED) oled.endOTABanner();
#ifdef ESP32
  // tiny keeps its image over a reset
  if (e == Lifecycle::SHUTDOWN && oled.is_present()) {
    oled.clearBuffer();
    oled.sendBuffer();
  }
#endif
}

void SSD1306Display::setup() {
  Lifecycle::on(oled_lifecycle);
  setup_driver();
  // only when chosen, a MAX7219 build keeps the 1 KB
  if (!_frame) _frame = (uint8_t*)malloc(u8g2_GetBufferSize(&_u8g2));
  if (!_frame) {
    Log::console(PSTR("OLED: no memory for the frame"));
    return;
  }
  u8g2_SetBufferPtr(&_u8g2, _frame);
  reset_panel();

  _oled_addr = I2CBus::begin() ? oled_probe_addr() : 0;
  if (!_oled_addr) {
    _present = false;
    Log::console(PSTR("OLED: no display detected at 0x3C/0x3D (sda=%d scl=%d)"),
                 _pin_sda, _pin_scl);
    EGModuleRegistry::set_loop_interval(this, 250);
    setFontPosTop();
    return;
  }
  _present = true;
  Log::console(PSTR("OLED: detected at 0x%02X (type=%u)"), _oled_addr, _pref_display_type);
  setI2CAddress(_oled_addr << 1);
  setBusClock(700000);
  u8g2_InitDisplay(&_u8g2);
  u8g2_SetPowerSave(&_u8g2, 0);
  // not ClearDisplay, tiny SSD1306 ignores it after a soft reset
  u8g2_ClearBuffer(&_u8g2);
  u8g2_SendBuffer(&_u8g2);
  setFontPosTop();
#ifdef OLED_PINS_BLOCKED
  bool flipped = OLED_FLIP;
#else
  bool flipped = EGPrefs::getBool("display", K_FLIP);
#endif
  // inverted vs ThingPulse, u8g2 init is already flipped
  setFlipMode(flipped ? 0 : 1);
  setContrast(64);
  draw_splash();
}

// first tap only wakes
void SSD1306Display::onButtonTap(unsigned long now, int8_t direction) {
  static unsigned long s_last_tap = 0;
  static uint8_t s_tap_count = 0;
  if (direction > 0) {
    s_tap_count = (now - s_last_tap < 500) ? (s_tap_count + 1) : 1;
    s_last_tap = now;
  }

  if (!oled_on) {
    oled_page = PAGE_MAIN;
    if (_present) setPowerSave(0);
    oled_on = true;
    EGModuleRegistry::set_loop_interval(this, tick_ms());
  } else if (direction > 0 && s_tap_count >= 5) {
    oled_page = PAGE_SAVER;
    s_tap_count = 0;
  } else if (direction > 0) {
    oled_page++;
    if (oled_page > PAGE_LAST) oled_page = PAGE_MAIN;
  } else {
    if (oled_page <= PAGE_MAIN) oled_page = PAGE_LAST;
    else oled_page--;
  }
  oled_last_update = 0;
  _send_rows_left = 0;
  _next_render_due = 0;
  loop(now);
}

void SSD1306Display::setupWifi(const char* s) {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
#ifdef ESP32
  if (isTiny()) {
    setFont(U8G2_FONT_ARIAL_10);
    drawStrP(0, 2,  PSTR("Setup WiFi"));
    const char* host = s ? s : "";
    const char* dash = strrchr(host, '-');
    char line[16];
    snprintf_P(line, sizeof(line), PSTR("AP: %s"), dash ? dash + 1 : host);
    drawStr(0, 15, line);
    sendBuffer();
    return;
  }
#endif
  drawTPString(*this, 0, 10, DialogInput_plain_12, PSTR("Setup - Connect to"));
  drawTPString(*this, 0, 24, DialogInput_plain_12, PSTR("WiFi -"));
  drawTPString(*this, 0, 38, DialogInput_plain_12, s);
  sendBuffer();
}

void SSD1306Display::apOnlyMode(const char* host, const char* ip) {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  drawTPString(*this, 0, 6,  DialogInput_plain_12, PSTR("Setup - Join AP"));
  drawTPString(*this, 0, 20, DialogInput_plain_12, host ? host : "");
  drawTPString(*this, 0, 34, DialogInput_plain_12, ip ? ip : "");
  drawTPString(*this, 0, 48, DialogInput_plain_12, PSTR("Set region, reboot"));
  sendBuffer();
  display.keep_on();
  _locked_page = true;
}

void SSD1306Display::safe_mode() {
  start();
  if (!_present) return;
  clearBuffer();
  drawTPString(*this, 0, 6, DialogInput_plain_12, PSTR("Safe mode"));
  sendBuffer();
}

void SSD1306Display::wifiDisabled() {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  drawXBMP(0, 0, 51, 51, ESPLogo);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(55, 10, W_APP_NAME);
  drawStrP(55, 24, RELEASE_VERSION);
  drawStrP(55, 42, PSTR("Offline mode"));
  sendBuffer();
}

void SSD1306Display::wipeCountdown(int seconds_left) {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 0,  PSTR("Offline mode armed"));
  drawStrP(0, 16, PSTR("Hold to factory reset"));
  char line[24];
  snprintf_P(line, sizeof(line), PSTR("%ds"), seconds_left);
  setFont(U8G2_FONT_ARIAL_24);
  drawStrP(0, 32, line);
  sendBuffer();
}

void SSD1306Display::wipeReady() {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_16);
  drawStrP(0, 0,  PSTR("RELEASE for"));
  drawStrP(0, 18, PSTR("FULL WIPE"));
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
#ifdef ESP32
  if (isTiny()) font = U8G2_FONT_ARIAL_10;
#endif
  setFont(font);
  drawStrP(0, 0,  PSTR("Screen"));
  drawStrP(0, 20, auto_off ? PSTR("auto off") : PSTR("always on"));
  if (_present) { setPowerSave(0); sendBuffer(); }
  oled_on = true;
  oled_last_update = 0;
  _next_render_due = now + 1500;   // message stays up
  EGModuleRegistry::set_loop_interval(this, TICK_MS);
}
#endif

bool SSD1306Display::page_one_graph() {
  int hist[HIST];
  uint16_t histSize = (uint16_t)egm_history(hist, HIST);
  int32_t minValue = 0;
  int32_t maxValue = 0;
  bool counts = histSize > 0 && hist_range(hist, histSize, minValue, maxValue);

  setDrawColor(0);
  drawBox(0, 35, OLED_WIDTH, 29);
  setDrawColor(1);
  drawLine(0, 63, 90, 63);
  drawLine(90, 35, 90, 63);
  if (histSize > 0) {
    int x_start = 2 * (HIST - histSize);
    char graphBuf[8];
    if (!counts) {
      snprintf_P(graphBuf, sizeof(graphBuf), W_FMT_D, 0);
      drawTPString(*this, 93, 55, Open_Sans_Digits_10, graphBuf);
      return true;
    }
    for (uint16_t i = 0; i < histSize; i++) {
      int location = ((map((long)hist[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;
      drawFrame(x_start + i * 2, location, 2, (63 - location));
    }
    snprintf_P(graphBuf, sizeof(graphBuf), W_FMT_D, (int)(minValue / 10));
    drawTPString(*this, 93, 55, Open_Sans_Digits_10, graphBuf);
    snprintf_P(graphBuf, sizeof(graphBuf), W_FMT_D, (int)(maxValue / 10));
    drawTPString(*this, 93, 35, Open_Sans_Digits_10, graphBuf);
  }
  return true;
}

// vs 5 s ago, 1.5 sigma
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

// false = nothing changed, no redraw
bool SSD1306Display::page_one_values(unsigned long now) {
  float dcpm = disp_cpm();
  float dratio = egm_f(M_RATIO);
  float dusv = (dratio > 0.0f) ? dcpm / dratio : 0.0f;
  uint32_t cpm_x10 = (uint32_t)(dcpm * 10.0f + 0.5f);
  int32_t  usv_x100 = (int32_t)(dusv * 100.0f + 0.5f);
  bool warming = !egm_b(M_WARM);
  uint8_t snd = send_indicator;
  uint8_t wifi = (Wifi::disabled ? 1 : 0) | (Wifi::connected ? 2 : 0)
               | (egm_b(M_NONE) ? 4 : 0);
  int8_t trend = cpm_trend(cpm_x10 / 10);
  if (cpm_x10 == _p1v_last_cpm && usv_x100 == _p1v_last_usv_x100
      && warming == _p1v_last_warming && snd == _p1v_last_send_ind
      && wifi == _p1v_last_wifi && trend == _p1v_last_trend) {
    return false;
  }
  _p1v_last_cpm = cpm_x10;
  _p1v_last_usv_x100 = usv_x100;
  _p1v_last_warming = warming;
  _p1v_last_send_ind = snd;
  _p1v_last_wifi = wifi;
  _p1v_last_trend = trend;
  draw_p1_values(cpm_x10, dusv, warming, snd, wifi, trend);
  return true;
}

void SSD1306Display::draw_p1_values(uint32_t cpm_x10, float dusv, bool warming,
                                    uint8_t snd, uint8_t wifi, int8_t trend) {
  setDrawColor(0);
  drawBox(36, 0, 92, 32);
  setDrawColor(1);
  if (trend > 0)      drawXBMP(37, 2, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_arrow_up);
  else if (trend < 0) drawXBMP(37, 2, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_arrow_down);
  char oledBuf[16];
  format_cpm(oledBuf, sizeof(oledBuf), cpm_x10 * 0.1f);
  drawTPString(*this, 45, 0, DialogInput_Digits_17, oledBuf);
  if (wifi & 4) {
    drawTPString(*this, 45, 20, DialogInput_plain_12, PSTR("check tube?"));
  } else {
    format_f(oledBuf, sizeof(oledBuf), dusv);
    drawTPString(*this, 45, 20, DialogInput_plain_12, oledBuf);
  }
  if (warming) {
    drawTPString(*this, 98, 2, DialogInput_plain_12, PSTR("W"));
  }
  if (snd) {
    drawXBMP(110, 0, 8, 16, _iconimage_remotext);
    send_indicator--;
  }
  if (!Wifi::disabled) {
    drawXBMP(120, 0, 8, 16, Wifi::connected ? _iconimage_connected : _iconimage_disconnected);
  }
}

void SSD1306Display::page_three_full() {
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 2, GEIGER_MODEL);
  drawStrP(0, 17, PSTR(RELEASE_VERSION " / " GIT_VERSION));
  drawStrP(0, 32, PSTR(__DATE__ " " __TIME__));
  drawStrP(0, 47, PSTR("@ steadramon"));
}

namespace {
static const unsigned long MTX_T1 = 100;
static const unsigned long MTX_T2 = 300;
static const unsigned long MTX_T3 = 800;
static const unsigned long MTX_T4 = 2000;

struct MatrixCtx {
  uint8_t cps_factor;
};

static MatrixCtx compute_matrix_ctx(unsigned long typical) {
  MatrixCtx c;
  if      (typical < 200)  c.cps_factor = 8;
  else if (typical < 500)  c.cps_factor = 7;
  else if (typical < 1000) c.cps_factor = 6;
  else if (typical < 2000) c.cps_factor = 5;
  else                     c.cps_factor = 4;
  return c;
}

static void compute_drop_speed_tail(unsigned long delta_ms, const MatrixCtx& ctx,
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
static const uint8_t MTX_FULL = 8;               // alive factor once 3 run
static unsigned long s_mtx_blip = 0;
static uint8_t s_mtx_col = 255;                  // 255 = none yet

// opening rain, speeds from the CPM history
static void seed_drops(Drop* drops) {
  s_mtx_blip = egm_u(M_BLIP);
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) drops[i].alive = false;
  int hist[HIST];
  size_t hist_n = egm_history(hist, HIST);
  float cps = egm_f(M_CPS);
  unsigned long typical = (cps > 0.05f) ? (unsigned long)(1000.0f / cps) : MTX_IDLE_MS;
  MatrixCtx ctx = compute_matrix_ctx(typical);
  for (uint8_t i = 0; i < MTX_SEED_DROPS; i++) {
    uint32_t r = GRNG::fast_uint32();
    unsigned long sample_delta_ms = 5000;
    if (hist_n > 0) {
      int cpm_sample = hist[(r >> 12) % hist_n];
      if (cpm_sample > 0) sample_delta_ms = 60000UL / (unsigned long)cpm_sample;
    }
    uint8_t spd, tail;
    compute_drop_speed_tail(sample_delta_ms, ctx, MTX_FULL, r, spd, tail);
    drops[i].alive = true;
    drops[i].x = (uint8_t)((r >> 4) & 0x7F);
    drops[i].y = (int16_t)((r >> 26) & 0x3F);
    drops[i].speed = spd;
    drops[i].tail = tail;
    drops[i].seed = (uint8_t)GRNG::fast_uint32();
  }
}

// ms between clicks over the last 5 s
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

// a click: quicker clicks, more and faster drops
static void spawn_drops(Drop* drops, unsigned long now_blip) {
  unsigned long delta_ms = (now_blip - s_mtx_blip) / 1000;
  s_mtx_blip = now_blip;
  MatrixCtx ctx = compute_matrix_ctx(recent_gap_ms());
  uint8_t alive_count = 0;
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) if (drops[i].alive) alive_count++;
  uint8_t alive_factor = (alive_count < 3) ? (4 + alive_count * 2) : MTX_FULL;
  uint8_t to_spawn = 1;
  if      (delta_ms < MTX_T1) to_spawn = 3;
  else if (delta_ms < MTX_T2) to_spawn = 2;
  // keep drops off the last column
  uint8_t prox_thresh = (ctx.cps_factor >> 1);
  uint8_t prox_shift  = prox_thresh + 3;
  for (uint8_t s = 0; s < to_spawn; s++) {
    Drop* d = free_drop(drops);
    if (!d) return;
    uint32_t r = GRNG::fast_uint32();
    uint8_t col = (uint8_t)(r & 0x7F);
    int8_t diff = (int8_t)col - (int8_t)s_mtx_col;
    if (diff < 0) diff = -diff;
    if (s_mtx_col < 128 && diff < prox_thresh) col = (col + prox_shift) & 0x7F;
    s_mtx_col = col;
    uint8_t spd, tail;
    compute_drop_speed_tail(delta_ms, ctx, alive_factor, r, spd, tail);
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

// head solid, the tail thins out, same pixels each frame
static void draw_drop(SSD1306Display& o, Drop& d) {
  const uint8_t BODY = 2;
  uint8_t fade_span = (d.tail > BODY) ? (d.tail - BODY) : 1;
  uint16_t skip_step = 256 / fade_span;
  for (uint8_t t = 0; t < d.tail; t++) {
    int16_t ty = d.y - (t << 1);
    if (ty < 0 || ty >= OLED_HEIGHT) continue;
    if (t >= BODY) {
      uint16_t skip_prob = (uint16_t)(t - BODY) * skip_step;
      uint8_t h = (uint8_t)(d.seed * 53u + t * 97u);
      if (h < skip_prob) continue;
    }
    o.drawPixel(d.x, ty);
  }
  d.y += d.speed >> 3;
  if (d.y - (d.tail << 1) >= OLED_HEIGHT) d.alive = false;
}

}  // namespace

void SSD1306Display::page_four_matrix() {
  Drop*& drops = s_matrix_drops;
  if (!drops) {
    drops = (Drop*)calloc(MATRIX_DROPS, sizeof(Drop));
    if (!drops) return;
  }
  if (_page4_num_last == 0) {
    _page4_num_last = fast_millis();
    seed_drops(drops);
  }
  unsigned long now_blip = egm_u(M_BLIP);
  if (now_blip != s_mtx_blip) spawn_drops(drops, now_blip);

  clearBuffer();
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) {
    if (drops[i].alive) draw_drop(*this, drops[i]);
  }
}

void SSD1306Display::page_four_static() {
  unsigned long now = fast_millis();
  if (_page4_num_last == 0 || now - _page4_num_last >= 1000) {
    _page4_num_last = now;
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
  setDrawColor(0);
  drawBox(41, 19, 44, 24);
  setDrawColor(1);
  drawTPString(*this, 63 - (int)strlen(_page4_num) * 5, 21,
               DialogInput_Digits_17, _page4_num);
}

void SSD1306Display::showOTABanner() {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_16);
  // not PROGMEM safe, copy to ram
  char l1[12], l2[20];
  snprintf_P(l1, sizeof(l1), PSTR("Update"));
  snprintf_P(l2, sizeof(l2), PSTR("in progress..."));
  drawStr((OLED_WIDTH - getStrWidth(l1)) / 2, (OLED_HEIGHT / 2) - 20, l1);
  drawStr((OLED_WIDTH - getStrWidth(l2)) / 2, (OLED_HEIGHT / 2),  l2);
  setPowerSave(0);
  oled_on = true;
  sendBuffer();
  _ota_lock = true;
}

// failed, back to the pages with a full redraw
void SSD1306Display::endOTABanner() {
  if (!_ota_lock) return;
  _ota_lock = false;
  oled_last_update = 0;
  _next_render_due = 0;
  EGModuleRegistry::set_loop_interval(this, tick_ms());
}

bool SSD1306Display::screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) {
  w = 128; h = 64;
#ifdef ESP32
  if (isTiny()) { w = 72; h = 40; }
#endif
  buf = getBufferPtr();
  return buf != nullptr;
}
#endif
