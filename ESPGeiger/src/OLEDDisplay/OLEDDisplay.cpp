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
#include "OLEDDisplay.h"
#include "fonts.h"
#include "logo.h"
#include "screenJS.gz.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../NTP/NTP.h"
#include "../Util/DeviceInfo.h"
#include "../Util/PinSafety.h"
#include "../Util/StringUtil.h"
#include "../Util/Wifi.h"
#include "../GRNG/GRNG.h"
#include "../WebPortal/WebPortal.h"
#include <EGHttpServer.h>
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

extern uint8_t send_indicator;

#define _OLED_STR(x) #x
#define OLED_STR(x) _OLED_STR(x)

SSD1306Display display;
EG_REGISTER_MODULE(display)

// Stand-ins for ThingPulse's ArialMT_Plain_{10,16,24}.
#define U8G2_FONT_ARIAL_10  u8g2_font_helvR08_tr
#define U8G2_FONT_ARIAL_16  u8g2_font_helvR12_tr
#define U8G2_FONT_ARIAL_24  U8G2_FONT_ARIAL_16

EG_PSTR(OL_L_BRT, "Brightness");
#ifdef GEIGER_PUSHBUTTON
EG_PSTR(OL_L_TMO, "Timeout");
EG_PSTR(OL_H_TMO, "Idle seconds before display blanks (0 = use schedule below)");
#endif
EG_PSTR(OL_L_ONT, "On Time");
EG_PSTR(OL_L_OFT, "Off Time");
#ifdef GEIGER_PUSHBUTTON
EG_PSTR(OL_H_ONT, "Display on (used when Timeout = 0)");
EG_PSTR(OL_H_OFT, "Display off (used when Timeout = 0)");
#else
EG_PSTR(OL_H_ONT, "Display on");
EG_PSTR(OL_H_OFT, "Display off");
#endif
#ifndef OLED_PINS_BLOCKED
EG_PSTR(OL_L_SDA, "I2C SDA Pin");
EG_PSTR(OL_L_SCL, "I2C SCL Pin");
EG_PSTR(OL_H_RBA, "Reboot to apply");
EG_PSTR(OL_L_FLP, "Flip 180\xC2\xB0");
EG_PSTR(OL_L_TYP, "Display Type");
EG_PSTR(OL_H_TYP, "0=SSD1306, 1=SH1106 (1.3in), 2=SSD1309. Reboot to apply.");
#if OLED_RST != 255
EG_PSTR(OL_L_RST, "Reset Pin");
EG_PSTR(OL_H_RST, "GPIO toggled at boot to reset OLED (255 = none). Reboot to apply.");
#endif
#endif

static const EGPref OLED_PREF_ITEMS[] = {
  {"brightness", OL_L_BRT, nullptr,  "25", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#ifdef GEIGER_PUSHBUTTON
  {"timeout",    OL_L_TMO, OL_H_TMO, "120", nullptr, 0, 99999, 0, EGP_UINT, 0},
#endif
  {"on_time",    OL_L_ONT, OL_H_ONT, "", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
  {"off_time",   OL_L_OFT, OL_H_OFT, "", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
#ifndef OLED_PINS_BLOCKED
  {"sda",        OL_L_SDA, OL_H_RBA, OLED_STR(OLED_SDA), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"scl",        OL_L_SCL, OL_H_RBA, OLED_STR(OLED_SCL), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"flip",       OL_L_FLP, OL_H_RBA, OLED_FLIP ? "1" : "0", nullptr, 0, 1, 0, EGP_BOOL, 0},
  {"type",       OL_L_TYP, OL_H_TYP, OLED_STR(OLED_TYPE), nullptr, 0, 2, 0, EGP_UINT, 0},
#if OLED_RST != 255
  {"rst",        OL_L_RST, OL_H_RST, OLED_STR(OLED_RST), nullptr, 0, 255, 0, EGP_UINT, 0},
#endif
#endif
};

static const EGPrefGroup OLED_PREF_GROUP = {
  "display", "Display", 1,
  OLED_PREF_ITEMS,
  sizeof(OLED_PREF_ITEMS) / sizeof(OLED_PREF_ITEMS[0]),
};

const EGPrefGroup* SSD1306Display::prefs_group() { return &OLED_PREF_GROUP; }

static const char PIN_OWNER[] PROGMEM = "OLED";
static int16_t s_sched_on_mins = -1;
static int16_t s_sched_off_mins = -1;
static unsigned long s_sched_recompute_ms = 0;
static bool s_sched_cached = true;
static bool s_oled_flipped = false;

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

    // col/page tracked incrementally to avoid div/mod per byte. Partial last
    // column is handled implicitly by iterating until byte_count is exhausted.
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

static uint16_t tpStringWidth(const uint8_t* font, const char* str) {
  if (!font || !str) return 0;
  uint8_t first_char = pgm_read_byte(font + 2);
  uint8_t num_chars  = pgm_read_byte(font + 3);
  const uint8_t* jump_table = font + 4;
  uint16_t w = 0;
  for (const char* p = str; ; p++) {
    uint8_t c = pgm_read_byte(p);
    if (c == 0) break;
    if (c < first_char || (uint16_t)(c - first_char) >= num_chars) continue;
    const uint8_t* je = jump_table + ((uint16_t)(c - first_char) << 2);
    w += pgm_read_byte(je + 3);
  }
  return w;
}

void SSD1306Display::drawStrP(int16_t x, int16_t y, const char* s) {
  if (!s) return;
  // Per-char DrawGlyph + pgm_read_byte: no temp buffer, no length cap.
  // Bypasses u8g2's UTF-8 decoder so multibyte sequences render as raw
  // Latin-1 bytes; all current callers pass ASCII only.
  int16_t cx = x;
  for (const char* p = s; ; p++) {
    uint8_t c = pgm_read_byte(p);
    if (c == 0) break;
    cx += u8g2_DrawGlyph(&_u8g2, cx, y, c);
  }
}

void SSD1306Display::on_prefs_loaded() {
#ifndef OLED_PINS_BLOCKED
  {
    uint8_t sda = (uint8_t)EGPrefs::getUInt("display", "sda");
    if (const char* why = PinSafety::claim_output((int)sda, PIN_OWNER)) {
      Log::console(PSTR("OLED: sda=%u unsafe (%s) - keeping default %u"), sda, why, _pin_sda);
      PinSafety::claim((int)_pin_sda, PIN_OWNER);
    } else {
      _pin_sda = sda;
    }
  }
  {
    uint8_t scl = (uint8_t)EGPrefs::getUInt("display", "scl");
    if (const char* why = PinSafety::claim_output((int)scl, PIN_OWNER)) {
      Log::console(PSTR("OLED: scl=%u unsafe (%s) - keeping default %u"), scl, why, _pin_scl);
      PinSafety::claim((int)_pin_scl, PIN_OWNER);
    } else {
      _pin_scl = scl;
    }
  }
#if OLED_RST != 255
  {
    uint8_t rst = (uint8_t)EGPrefs::getUInt("display", "rst");
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
#endif
  _pref_display_type = (uint8_t)EGPrefs::getUInt("display", "type");
#else
  _pref_display_type = OLED_TYPE;
#endif
  static bool setup_done = false;
  if (!setup_done) {
    setup_done = true;
    setup();
  }
  setBrightness((uint8_t)EGPrefs::getUInt("display", "brightness"));
#ifdef GEIGER_PUSHBUTTON
  setTimeout((int)EGPrefs::getUInt("display", "timeout"));
#endif
  s_sched_on_mins = -1;
  EGModuleRegistry::set_loop_interval(this, 20);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias OLED_LEGACY[] = {
  {"dispBrightness", "brightness"},
#ifdef GEIGER_PUSHBUTTON
  {"dispTimeout", "timeout"},
#endif
  {"oledOn",  "on_time"},
  {"oledOff", "off_time"},
  {nullptr, nullptr},
};
const EGLegacyAlias* SSD1306Display::legacy_aliases() { return OLED_LEGACY; }
// === END LEGACY IMPORT ===

SSD1306Display::SSD1306Display() {
  // Driver-specific u8g2_Setup_*_f runs in setup() once we know the type pref.
}

void SSD1306Display::setBrightness(uint8_t brightness) {
  if (!_present) return;
  brightness = (uint8_t)(((uint16_t)brightness * 255 + 50) / 100);
  setContrast(brightness);
}

#ifndef OLED_PINS_BLOCKED
void SSD1306Display::on_prefs_saved() {
  uint8_t sda = (uint8_t)EGPrefs::getUInt("display", "sda");
  uint8_t scl = (uint8_t)EGPrefs::getUInt("display", "scl");
  bool flip = EGPrefs::getBool("display", "flip");
  uint8_t type = (uint8_t)EGPrefs::getUInt("display", "type");
  bool need_reboot = (sda != _pin_sda) || (scl != _pin_scl)
                  || (flip != s_oled_flipped) || (type != _pref_display_type);
#if OLED_RST != 255
  uint8_t rst = (uint8_t)EGPrefs::getUInt("display", "rst");
  need_reboot = need_reboot || (rst != _pin_rst);
#endif
  on_prefs_loaded();
  if (need_reboot) EGPrefs::request_restart();
}
#endif

void SSD1306Display::loop(unsigned long now) {
    // Progressive send: if a previous render queued rows, drain one per tick.
    // Keeps blocking under ~1.6 ms per tick instead of 6.6-13 ms in one go.
    // Render is gated below so we only re-render once the queue is empty.
    if (_send_rows_left > 0) {
      if (_present) updateDisplayArea(0, _send_ty + _send_cur, 16, 1);
      _send_cur++;
      _send_rows_left--;
      return;
    }
    if (oled_page > OLED_PAGES) {
      oled_page = 1;
    }
    if (oled_page != _last_page) {
      _last_page = oled_page;
      _next_render_due = 0;   // force immediate render on page change
      // Page 4's render cadence is 80 ms; loop must tick fast enough so the
      // 8-row drain finishes in 80 ms (8 * 10 = 80). Other pages happily
      // drain in 80-160 ms at 20 ms.
      EGModuleRegistry::set_loop_interval(this, oled_page == 4 ? 10 : 20);
    }
    if (oled_page == 4) {
      oled_timeout = now;
    }
    // Throttle render. Page 4 is animated (~12.5 Hz), others 2 Hz.
    if ((long)(now - _next_render_due) < 0) return;
    _next_render_due = now + (oled_page == 4 ? 80UL : 500UL);
    // Timeout/schedule only relevant when a real display is attached. Without
    // one we keep rendering into the FB so /screen still shows the live UI.
    if (_present) {
      if (enable_oled_timeout && _lcd_timeout_ms > 0 &&
          (now - oled_timeout > _lcd_timeout_ms)) {
        if (oled_on) { clearBuffer(); setPowerSave(1); oled_on = false; _send_rows_left = 0; }
        return;
      }
      if (_lcd_timeout_ms == 0
          && (now - oled_timeout) > 30000
          && !isScreenOnTime(now)) {
        if (oled_on) { clearBuffer(); setPowerSave(1); oled_on = false; _send_rows_left = 0; }
        EGModuleRegistry::set_loop_interval(this, 30000);
        return;
      }
      if (!oled_on) {
        setPowerSave(0);
        oled_page = 1;
        oled_on = true;
        oled_last_update = now - 20000;
        EGModuleRegistry::set_loop_interval(this, 20);
      }
    }
    enum : uint8_t { DR_NONE = 0, DR_TOP = 1, DR_BOT = 2, DR_FULL = 0xFF };
    uint8_t dirty = DR_NONE;
    if (oled_page == 1) {
      bool full_redraw = (now - oled_last_update >= 10000) || (oled_last_update == 0);
      if (full_redraw) {
        oled_last_update = now;
        page_one_clear();
        dirty = DR_FULL;
      }
      _p1_alt = !_p1_alt;
      if (_p1_alt || full_redraw) {
        if (page_one_values(now)) dirty |= DR_TOP;
      }
      if (!_p1_alt || full_redraw) {
        if (page_one_graph()) dirty |= DR_BOT;
      }
    } else if (oled_page == 2) {
      if ((now - oled_last_update >= 1000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_two_full();
        dirty = DR_FULL;
      }
    } else if (oled_page == 3) {
      if ((now - oled_last_update >= 10000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_three_full();
        dirty = DR_FULL;
      }
    } else if (oled_page == 4) {
      if (oled_last_update == 0) {
        _page4_num_last = 0;
        _page4_variant = random(2);
      }
      oled_last_update = now;
      if (_page4_variant == 0) {
        page_four_static();
      } else {
        page_four_matrix();
      }
      dirty = DR_FULL;
    }
    // Queue progressive send: one tile-row drained per loop tick above.
    // Top half = ty 0..3, bottom half = ty 4..7, full = ty 0..7.
    if (dirty != DR_NONE) {
      _send_cur = 0;
      if (dirty == DR_TOP)      { _send_ty = 0; _send_rows_left = 4; }
      else if (dirty == DR_BOT) { _send_ty = 4; _send_rows_left = 4; }
      else                      { _send_ty = 0; _send_rows_left = 8; }
    }
  }

bool SSD1306Display::isScreenOnTime(unsigned long now) {
  if (s_sched_on_mins == -1) {
    ParsedTime on_time  = parseTime(EGPrefs::getString("display", "on_time"));
    ParsedTime off_time = parseTime(EGPrefs::getString("display", "off_time"));
    if (!on_time.isValid || !off_time.isValid) { s_sched_on_mins = -2; return true; }
    s_sched_on_mins = on_time.hour * 60 + on_time.minute;
    s_sched_off_mins = off_time.hour * 60 + off_time.minute;
    s_sched_recompute_ms = 0;
  }
  if (s_sched_on_mins == -2) return true;
  if (!ntpclient.synced) return true;
  if ((long)(now - s_sched_recompute_ms) < 0) return s_sched_cached;
  struct tm t;
  if (!ntpclient.localTm(&t)) return true;
  int now_mins = t.tm_hour * 60 + t.tm_min;
  s_sched_cached = (s_sched_on_mins < s_sched_off_mins)
    ? (now_mins >= s_sched_on_mins && now_mins < s_sched_off_mins)
    : (now_mins >= s_sched_on_mins || now_mins < s_sched_off_mins);
  s_sched_recompute_ms = now + (60UL - t.tm_sec) * 1000UL;
  return s_sched_cached;
}

void SSD1306Display::page_two_full() {
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(0, 2, DeviceInfo::hostname());
  drawStrP(0, 17, PSTR("IP:"));
  char ipStr[16];
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
  // UDP source line: identity left, loss % right-aligned.
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
  // Font is now ASCII-only; mu lives in logo.h as a 7x15 XBM specials glyph.
  drawXBMP(0, 20, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_mu);
  drawTPString(*this, EG_GLYPH_W, 20, DialogInput_plain_12, PSTR("Sv/h:"));
  if (!Wifi::disabled) {
    drawXBMP(120, 0, 8, 16,
             Wifi::connected ? _iconimage_connected : _iconimage_disconnected);
  }
  // FB just wiped; force the next values render to skip the cache.
  _p1v_last_cpm = 0xFFFFFFFFu;
}

void SSD1306Display::setup() {
  // Both 128x64 drivers share u8g2_m_16_8_f's buffer allocator, so the second
  // driver only costs its init-sequence tables in flash, no extra RAM.
  switch (_pref_display_type) {
    case DISP_SH1106:
      u8g2_Setup_sh1106_i2c_128x64_noname_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
    case DISP_SSD1309:
      u8g2_Setup_ssd1309_i2c_128x64_noname0_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
    case DISP_SSD1306:
    default:
      u8g2_Setup_ssd1306_i2c_128x64_noname_f(&_u8g2, U8G2_R0,
          u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
      break;
  }

#if OLED_RST != 255
  if (_pin_rst != 255) {
    pinMode(_pin_rst, OUTPUT);
    digitalWrite(_pin_rst, HIGH); delay(50);
    digitalWrite(_pin_rst, LOW);  delay(10);
    digitalWrite(_pin_rst, HIGH); delay(50);
  }
#endif

  Wire.begin(_pin_sda, _pin_scl);

  // SA0 pin low = 0x3C (common), high = 0x3D.
  static const uint8_t probe_addrs[] = { 0x3c, 0x3d };
  _oled_addr = 0;
  for (uint8_t i = 0; i < sizeof(probe_addrs); i++) {
    Wire.beginTransmission(probe_addrs[i]);
    if (Wire.endTransmission() == 0) { _oled_addr = probe_addrs[i]; break; }
  }
  if (!_oled_addr) {
    _present = false;
    Log::console(PSTR("OLED: no display detected at 0x3C/0x3D (sda=%d scl=%d)"),
                 _pin_sda, _pin_scl);
    // Keep rendering into the FB so /screen still shows the UI. I2C sends
    // are gated on _present in loop().
    setFontPosTop();
    return;
  }
  _present = true;
  Log::console(PSTR("OLED: detected at 0x%02X (type=%u)"), _oled_addr, _pref_display_type);
  setI2CAddress(_oled_addr << 1);
  setBusClock(700000);
  u8g2_InitDisplay(&_u8g2);
  u8g2_SetPowerSave(&_u8g2, 0);
  u8g2_ClearDisplay(&_u8g2);
  setFontPosTop();
#ifdef OLED_PINS_BLOCKED
  s_oled_flipped = OLED_FLIP;
#else
  s_oled_flipped = EGPrefs::getBool("display", "flip");
#endif
  // u8g2's NONAME init already applies what ThingPulse called "flipped"
  // (SEGREMAP 0xA1 + COMSCAN 0xC8), so setFlipMode is inverted vs ThingPulse.
  setFlipMode(s_oled_flipped ? 0 : 1);
  setContrast(64);
  clearBuffer();
  drawXBMP(0, 0, 51, 51, ESPLogo);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(55, 10, PSTR("ESPGeiger"));
  drawStrP(55, 24, RELEASE_VERSION);
  drawStrP(55, 42, PSTR("Connecting .."));
  sendBuffer();
}

void SSD1306Display::onButtonTap(unsigned long now, int8_t direction) {
  if (!_present) return;
  // 5-rapid-tap gesture only counts on the forward button so users on
  // back/forward setups don't get the hidden page 4 by accident.
  static unsigned long s_last_tap = 0;
  static uint8_t s_tap_count = 0;
  if (direction > 0) {
    s_tap_count = (now - s_last_tap < 400) ? (s_tap_count + 1) : 1;
    s_last_tap = now;
  }

  oled_timeout = now;
  if (!oled_on) {
    oled_page = 1;
    setPowerSave(0);
    oled_on = true;
    EGModuleRegistry::set_loop_interval(this, 20);
  } else if (direction > 0 && s_tap_count >= 5) {
    oled_page = 4;
    s_tap_count = 0;
  } else if (direction > 0) {
    oled_page++;
    if (oled_page > 3) oled_page = 1;
  } else {
    if (oled_page <= 1) oled_page = 3;
    else oled_page--;
  }
  oled_last_update = 0;
  _send_rows_left = 0;
  _next_render_due = 0;
  loop(now);
}

void SSD1306Display::setupWifi(const char* s) {
  if (!_present) return;
  _send_rows_left = 0;       // abandon in-flight progressive send; we'll do a full sync below
  clearBuffer();
  drawTPString(*this, 0, 10, DialogInput_plain_12, PSTR("Setup - Connect to"));
  drawTPString(*this, 0, 24, DialogInput_plain_12, PSTR("WiFi -"));
  drawTPString(*this, 0, 38, DialogInput_plain_12, s);
  sendBuffer();
}

void SSD1306Display::wifiDisabled() {
  if (!_present) return;
  _send_rows_left = 0;
  clearBuffer();
  drawXBMP(0, 0, 51, 51, ESPLogo);
  setFont(U8G2_FONT_ARIAL_10);
  drawStrP(55, 10, PSTR("ESPGeiger"));
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

bool SSD1306Display::page_one_graph() {
  // No cache: at 1 Hz refresh against a 1 Hz data source, hash would almost
  // always miss.
  uint16_t histSize = (uint16_t)gcounter.cpm_history.size();
  int32_t minValue = 0;
  int32_t maxValue = 0;
  if (histSize > 0) {
    minValue = maxValue = gcounter.cpm_history[0];
    for (decltype(gcounter.cpm_history)::index_t i = 1; i < histSize; i++) {
      int v = gcounter.cpm_history[i];
      if (v > maxValue) maxValue = v;
      if (v < minValue) minValue = v;
    }
    if (minValue > 1) minValue = minValue * 9 / 10;
  }

  setDrawColor(0);
  drawBox(0, 35, OLED_WIDTH, 29);
  setDrawColor(1);
  drawLine(0, 63, 90, 63);
  drawLine(90, 35, 90, 63);
  if (histSize > 0) {
    int x_start = 0;
    if (gcounter.cpm_history.capacity != histSize) {
      x_start = (2 * (gcounter.cpm_history.capacity - histSize));
    }
    char graphBuf[8];
    if (maxValue == 0) {
      snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), (int)minValue);
      drawTPString(*this, 93, 55, Open_Sans_Digits_10, graphBuf);
      return true;
    }
    for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
      int location = ((map((long)gcounter.cpm_history[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;
      drawFrame(x_start + i * 2, location, 2, (63 - location));
    }
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), (int)minValue);
    drawTPString(*this, 93, 55, Open_Sans_Digits_10, graphBuf);
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), (int)maxValue);
    drawTPString(*this, 93, 35, Open_Sans_Digits_10, graphBuf);
  }
  return true;
}

bool SSD1306Display::page_one_values(unsigned long now) {
  // Cache-skip when nothing visible changed. WiFi + trend state included so
  // those refresh promptly, not only on the 10s page_one_clear cycle.
  uint32_t cpm = (uint32_t)gcounter.get_cpm();
  int32_t  usv_x100 = (int32_t)(gcounter.get_usv() * 100.0f + 0.5f);
  bool warming = !gcounter.is_warm();
  uint8_t snd = send_indicator;
  uint8_t wifi = (Wifi::disabled ? 1 : 0) | (Wifi::connected ? 2 : 0);
  // Trend: compare to history N samples back, with sqrt(N) Poisson threshold.
  // Samples are pushed once per second, so 5 back = ~5 seconds of context.
  int8_t trend = 0;
  uint16_t hsize = (uint16_t)gcounter.cpm_history.size();
  if (hsize >= 5) {
    int32_t past = gcounter.cpm_history[hsize - 5];
    int32_t diff = (int32_t)cpm - past;
    int32_t thresh = (int32_t)(sqrtf((float)past) * 1.5f + 0.5f);
    if (thresh < 2) thresh = 2;
    if (diff >  thresh) trend =  1;
    else if (diff < -thresh) trend = -1;
  }
  if (cpm == _p1v_last_cpm && usv_x100 == _p1v_last_usv_x100
      && warming == _p1v_last_warming && snd == _p1v_last_send_ind
      && wifi == _p1v_last_wifi && trend == _p1v_last_trend) {
    return false;
  }
  _p1v_last_cpm = cpm;
  _p1v_last_usv_x100 = usv_x100;
  _p1v_last_warming = warming;
  _p1v_last_send_ind = snd;
  _p1v_last_wifi = wifi;
  _p1v_last_trend = trend;

  // Clear from x=36 to wipe the trend-arrow region too. WiFi icon column at
  // x=120-127 also reset here so reconnects show on the next tick.
  setDrawColor(0);
  drawBox(36, 0, 92, 32);
  setDrawColor(1);
  if (trend > 0)      drawXBMP(37, 2, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_arrow_up);
  else if (trend < 0) drawXBMP(37, 2, EG_GLYPH_W, EG_GLYPH_H, EGGlyph_arrow_down);
  char oledBuf[16];
  snprintf_P(oledBuf, sizeof(oledBuf), PSTR("%d"), (int)cpm);
  drawTPString(*this, 45, 0, DialogInput_Digits_17, oledBuf);
  format_f(oledBuf, sizeof(oledBuf), gcounter.get_usv());
  drawTPString(*this, 45, 20, DialogInput_plain_12, oledBuf);
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
  return true;
}

void SSD1306Display::page_three_full() {
  clearBuffer();
  setFont(U8G2_FONT_ARIAL_10);
  static char versionString[32] = "";
  if (versionString[0] == '\0') {
    snprintf_P(versionString, sizeof(versionString), PSTR("%s / %s"), RELEASE_VERSION, GIT_VERSION);
  }
  drawStrP(0, 2, GEIGER_MODEL);
  drawStrP(0, 17, versionString);
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
  // Multiply-and-shift uniform: 11-bit slice * range >> 11 gives [0, range).
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

}  // namespace

void SSD1306Display::page_four_matrix() {
  static const uint8_t MATRIX_DROPS = 32;
  struct Drop { int16_t y; uint8_t x; uint8_t speed; uint8_t tail; uint8_t seed; bool alive; };
  static Drop* drops = nullptr;
  static unsigned long last_blip_seen = 0;
  static uint8_t last_col = 255;

  if (!drops) {
    drops = (Drop*)calloc(MATRIX_DROPS, sizeof(Drop));
    if (!drops) return;
  }

  if (_page4_num_last == 0) {
    _page4_num_last = millis();
    last_blip_seen = gcounter.last_blip();
    for (uint8_t i = 0; i < MATRIX_DROPS; i++) drops[i].alive = false;
    size_t hist_n = gcounter.cpm_history.size();
    float cps_e = gcounter.get_cps();
    unsigned long typical_e = (cps_e > 0.05f) ? (unsigned long)(1000.0f / cps_e) : 1500;
    MatrixCtx ctx_e = compute_matrix_ctx(typical_e);
    for (uint8_t i = 0; i < 6; i++) {
      uint32_t r = GRNG::fast_uint32();
      unsigned long sample_delta_ms = 5000;
      if (hist_n > 0) {
        size_t idx = (r >> 12) % hist_n;
        int cpm_sample = gcounter.cpm_history[idx];
        if (cpm_sample > 0) sample_delta_ms = 60000UL / (unsigned long)cpm_sample;
      }
      uint8_t spd, tail;
      compute_drop_speed_tail(sample_delta_ms, ctx_e, 8, r, spd, tail);
      drops[i].alive = true;
      drops[i].x = (uint8_t)((r >> 4) & 0x7F);
      drops[i].y = (int16_t)((r >> 26) & 0x3F);
      drops[i].speed = spd;
      drops[i].tail = tail;
      drops[i].seed = (uint8_t)GRNG::fast_uint32();
    }
  }

  unsigned long now_blip = gcounter.last_blip();
  if (now_blip != last_blip_seen) {
    unsigned long delta_ms = (now_blip - last_blip_seen) / 1000;
    last_blip_seen = now_blip;
    size_t hist_n = gcounter.cpm_history.size();
    uint32_t recent_sum = 0;
    uint8_t  recent_n   = 0;
    if (hist_n > 0) {
      recent_n = (hist_n < 5) ? (uint8_t)hist_n : 5;
      for (uint8_t k = 0; k < recent_n; k++) {
        recent_sum += (uint32_t)gcounter.cpm_history[hist_n - 1 - k];
      }
    }
    unsigned long typical = 1500;
    if (recent_n > 0 && recent_sum > 0) {
      uint32_t avg_cpm = recent_sum / recent_n;
      if (avg_cpm > 0) typical = 60000UL / avg_cpm;
    }
    MatrixCtx ctx = compute_matrix_ctx(typical);
    uint8_t alive_count = 0;
    for (uint8_t i = 0; i < MATRIX_DROPS; i++) if (drops[i].alive) alive_count++;
    uint8_t alive_factor = (alive_count < 3) ? (4 + alive_count * 2) : 8;
    uint8_t to_spawn = 1;
    if      (delta_ms < MTX_T1) to_spawn = 3;
    else if (delta_ms < MTX_T2) to_spawn = 2;
    uint8_t prox_thresh = (ctx.cps_factor >> 1);
    uint8_t prox_shift  = prox_thresh + 3;
    for (uint8_t s = 0; s < to_spawn; s++) {
      for (uint8_t i = 0; i < MATRIX_DROPS; i++) {
        if (!drops[i].alive) {
          uint32_t r = GRNG::fast_uint32();
          uint8_t col = (uint8_t)(r & 0x7F);
          int8_t diff = (int8_t)col - (int8_t)last_col;
          if (diff < 0) diff = -diff;
          if (last_col < 128 && diff < prox_thresh) col = (col + prox_shift) & 0x7F;
          last_col = col;
          uint8_t spd, tail;
          compute_drop_speed_tail(delta_ms, ctx, alive_factor, r, spd, tail);
          drops[i].alive = true;
          drops[i].x = col;
          drops[i].y = (alive_factor < 8) ? 0 : -(int16_t)((r >> 16) & 0x1F);
          drops[i].speed = spd;
          drops[i].tail  = tail;
          drops[i].seed  = (uint8_t)(GRNG::fast_uint32() & 0xFF);
          alive_count++;
          if (alive_count >= 3) alive_factor = 8;
          break;
        }
      }
    }
  }

  clearBuffer();
  for (uint8_t i = 0; i < MATRIX_DROPS; i++) {
    Drop& d = drops[i];
    if (!d.alive) continue;
    const uint8_t BODY = 2;
    uint8_t fade_span = (d.tail > BODY) ? (d.tail - BODY) : 1;
    uint16_t skip_step = 256 / fade_span;
    for (uint8_t t = 0; t < d.tail; t++) {
      int16_t ty = d.y - (t << 1);
      if (ty < 0 || ty >= 64) continue;
      if (t >= BODY) {
        uint16_t skip_prob = (uint16_t)(t - BODY) * skip_step;
        uint8_t h = (uint8_t)(d.seed * 53u + t * 97u);
        if (h < skip_prob) continue;
      }
      drawPixel(d.x, ty);
    }
    d.y += d.speed >> 3;
    if (d.y - (d.tail << 1) >= 64) d.alive = false;
  }
}

void SSD1306Display::page_four_static() {
  unsigned long now = millis();
  if (_page4_num_last == 0 || now - _page4_num_last >= 1000) {
    _page4_num_last = now;
    uint16_t num;
    GRNG::extract((uint8_t*)&num, sizeof(num));
    num %= 1001;
    snprintf_P(_page4_num, sizeof(_page4_num), PSTR("%u"), num);
  }
  static uint8_t* noise = nullptr;
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
  // getStrWidth + drawStr are both PROGMEM-unsafe; copy to RAM first.
  char l1[12], l2[20];
  strcpy_P(l1, PSTR("Update"));
  strcpy_P(l2, PSTR("in progress..."));
  drawStr((OLED_WIDTH - getStrWidth(l1)) / 2, (OLED_HEIGHT / 2) - 20, l1);
  drawStr((OLED_WIDTH - getStrWidth(l2)) / 2, (OLED_HEIGHT / 2),  l2);
  setPowerSave(0);
  oled_on = true;
  sendBuffer();
}

// 1-bit monochrome BMP of the current framebuffer.
// 62 B header + 1024 B pixel data, row-major bottom-up, MSB-first per byte.
// Transposes from u8g2's page-major (8 vertical pixels per byte) layout.
static void hScreenBmp(EGHttpRequest&, EGHttpResponse& res, void* ctx) {
  SSD1306Display* self = static_cast<SSD1306Display*>(ctx);
  if (!self || !self->is_present()) {
    res.send(503, "text/plain", "no display");
    return;
  }
  uint8_t hdr[62] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  uint32_t file_size = 1086;
  hdr[2] = file_size & 0xff; hdr[3] = (file_size >> 8) & 0xff;
  hdr[4] = (file_size >> 16) & 0xff; hdr[5] = (file_size >> 24) & 0xff;
  hdr[10] = 62;   // pixel data offset
  hdr[14] = 40;   // DIB header size
  hdr[18] = 128;  // width
  hdr[22] = 64;   // height (+ve = bottom-up)
  hdr[26] = 1;    // planes
  hdr[28] = 1;    // bits per pixel
  hdr[46] = 2;    // palette entries
  // Palette: 0 = black, 1 = white (BGRA per entry).
  hdr[54] = 0;   hdr[55] = 0;   hdr[56] = 0;   hdr[57] = 0;
  hdr[58] = 255; hdr[59] = 255; hdr[60] = 255; hdr[61] = 0;

  if (!res.beginChunked(200, "image/bmp")) return;
  res.sendChunk((const char*)hdr, sizeof(hdr));

  const uint8_t* buf = self->getBufferPtr();
  uint8_t row[16];
  for (int y_bmp = 0; y_bmp < 64; y_bmp++) {
    int y_disp = 63 - y_bmp;
    int page = y_disp >> 3;
    int bit_in_page = y_disp & 7;
    for (int x_byte = 0; x_byte < 16; x_byte++) {
      uint8_t b = 0;
      const uint8_t* col_base = buf + page * 128 + x_byte * 8;
      for (int i = 0; i < 8; i++) {
        if ((col_base[i] >> bit_in_page) & 1) b |= (1 << (7 - i));
      }
      row[x_byte] = b;
    }
    res.sendChunk((const char*)row, sizeof(row));
  }
  res.endChunked();
}

// Raw 1024 B framebuffer in u8g2 page-major layout. Used by /screen's canvas.
static void hScreenBin(EGHttpRequest&, EGHttpResponse& res, void* ctx) {
  SSD1306Display* self = static_cast<SSD1306Display*>(ctx);
  if (!self || !self->is_present()) {
    res.send(503, "text/plain", "no display");
    return;
  }
  if (!res.beginChunked(200, "application/octet-stream")) return;
  res.sendChunk((const char*)self->getBufferPtr(), 1024);
  res.endChunked();
}

// Virtual tap: routes through the physical-button path so /screen cycles pages.
static void hScreenTap(EGHttpRequest&, EGHttpResponse& res, void* ctx) {
  SSD1306Display* self = static_cast<SSD1306Display*>(ctx);
  if (!self || !self->is_present()) {
    res.send(503, "text/plain", "no display");
    return;
  }
  self->onButtonTap(millis());
  res.send(200, "text/plain", "ok");
}

// /screen HTML viewer: polls /screen.bin and paints onto a 4x-zoomed canvas.
static const char SCREEN_PAGE_BODY[] PROGMEM = R"HTML(
<style>
  .oledwrap{display:flex;flex-direction:column;align-items:center;gap:1em;margin:1em 0}
  .oledframe{padding:8px;background:#222;border-radius:8px;box-shadow:0 0 10px #0006}
  canvas{image-rendering:pixelated;display:block;background:#000;width:512px;height:256px}
  .ctrls{display:flex;gap:.6em;align-items:center;flex-wrap:wrap;justify-content:center}
  button.tap{padding:.7em 1.5em;font-size:1em;cursor:pointer}
  .ms{display:flex;align-items:center;gap:.3em;font-size:.9em}
  .ms input[type=number]{width:6em;padding:.3em}
  .ms input[type=color]{width:2.3em;height:2em;padding:0;border:1px solid #888;cursor:pointer}
  .ms input[type=text]{width:5.5em;padding:.3em;font-family:monospace}
  .fps{font-size:.85em;opacity:.65;font-family:monospace;min-width:5em;text-align:right}
</style>
<div class="oledwrap">
  <div class="oledframe"><canvas id="oled" width="128" height="64"></canvas></div>
  <div class="ctrls">
    <button class="tap" id="tap" type="button">Tap (cycle page)</button>
    <label class="ms">Refresh <input id="ivl" type="number" min="50" max="10000" step="50" value="500">ms</label>
    <label class="ms">Tint <input id="col" type="color" value="#ffffff"><input id="colt" type="text" maxlength="7" value="#ffffff"></label>
    <span class="fps" id="fps">--</span>
  </div>
</div>
<script src=/screen.js)HTML" EG_CACHE_BUST R"HTML(></script>
)HTML";

extern const char screenJS[] PROGMEM = R"JS(
const $=byID,
  C=$('oled'),x=C.getContext('2d'),I=x.createImageData(128,64),P=new Uint32Array(I.data.buffer),
  F=$('fps'),V=$('ivl'),M=$('col'),H=$('colt'),
  N=()=>{let v=+V.value;return v<50?50:v>10000?10000:v},
  OFF=0xFF000000>>>0;
let T=Date.now(),f=0,t=0;
function tintABGR(){
  // little-endian: bytes go R,G,B,A so uint32 = (A<<24)|(B<<16)|(G<<8)|R
  const v=M.value,r=parseInt(v.slice(1,3),16),g=parseInt(v.slice(3,5),16),b=parseInt(v.slice(5,7),16);
  return ((0xFF<<24)|(b<<16)|(g<<8)|r)>>>0;
}
const TK='egoled_tint',sv=localStorage.getItem(TK);
if(sv&&/^#[0-9a-fA-F]{6}$/.test(sv)){M.value=sv;H.value=sv}
const sT=()=>localStorage.setItem(TK,M.value);
M.oninput=()=>{H.value=M.value;sT()};
H.oninput=()=>{let v=H.value.trim();if(v[0]!=='#')v='#'+v;if(/^#[0-9a-fA-F]{6}$/.test(v)){M.value=v.toLowerCase();sT()}};
async function r(){
  try{
    const R=await fetch('/screen.bin',{cache:'no-store'});
    if(R.ok){
      const b=new Uint8Array(await R.arrayBuffer());
      if(b.length>=1024){
        const ON=tintABGR();
        for(let p=0;p<8;p++)for(let c=0;c<128;c++){
          const B=b[(p<<7)+c],bs=(p<<10)+c;
          for(let i=0;i<8;i++)P[bs+(i<<7)]=B>>i&1?ON:OFF;
        }
        x.putImageData(I,0,0);f++;
      }
    }
  }catch(e){}
  const n=Date.now();
  if(n-T>=1000){F.textContent=(f*1000/(n-T)).toFixed(1)+' fps';T=n;f=0}
  t=setTimeout(r,document.hidden?1000:N());
}
$('tap').onclick=()=>fetch('/screen/tap',{cache:'no-store'});
V.onchange=()=>{clearTimeout(t);r()};
document.onvisibilitychange=()=>{if(!document.hidden){clearTimeout(t);r()}};
r();
)JS";

static void hScreen(EGHttpRequest&, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Screen"));
  res.sendChunk(FPSTR(SCREEN_PAGE_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hScreenJs(EGHttpRequest&, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", "public, max-age=31536000, immutable");
#if EG_GZ_screenJS
  res.sendGzipP(200, "application/javascript", screenJS_GZ, screenJS_GZ_LEN);
#else
  res.send(200, "application/javascript", FPSTR(screenJS));
#endif
}

void SSD1306Display::registerRoutes(EGHttpServer& http) {
  http.on("/screen",     EGHttpRequest::GET, hScreen);
  http.on("/screen.js",  EGHttpRequest::GET, hScreenJs);
  http.on("/screen.bmp", EGHttpRequest::GET, hScreenBmp,  this);
  http.on("/screen.bin", EGHttpRequest::GET, hScreenBin,  this);
  http.on("/screen/tap", EGHttpRequest::GET, hScreenTap,  this);
}
#endif
