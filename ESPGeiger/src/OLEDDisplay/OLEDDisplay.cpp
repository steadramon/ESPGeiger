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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/DeviceInfo.h"
#include "../Util/StringUtil.h"
#include "../Util/Wifi.h"
#include "../GRNG/GRNG.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

extern uint8_t send_indicator;

#define _OLED_STR(x) #x
#define OLED_STR(x) _OLED_STR(x)

// -1 defers Wire.begin to setup() once prefs have loaded.
SSD1306Display display = SSD1306Display(OLED_ADDR, -1, -1);
EG_REGISTER_MODULE(display)

EG_PSTR(OL_L_BRT, "Brightness");
#ifdef GEIGER_PUSHBUTTON
EG_PSTR(OL_L_TMO, "Timeout");
EG_PSTR(OL_H_TMO, "sec, 0=off");
#else
EG_PSTR(OL_L_ONT, "On Time");
EG_PSTR(OL_H_ONT, "Display on");
EG_PSTR(OL_L_OFT, "Off Time");
EG_PSTR(OL_H_OFT, "Display off");
#endif
#ifndef OLED_PINS_BLOCKED
EG_PSTR(OL_L_SDA, "I2C SDA Pin");
EG_PSTR(OL_L_SCL, "I2C SCL Pin");
EG_PSTR(OL_H_RBA, "Reboot to apply");
#endif

static const EGPref OLED_PREF_ITEMS[] = {
  {"brightness", OL_L_BRT, nullptr,  "25", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#ifdef GEIGER_PUSHBUTTON
  {"timeout",    OL_L_TMO, OL_H_TMO, "120", nullptr, 0, 99999, 0, EGP_UINT, 0},
#else
  {"on_time",    OL_L_ONT, OL_H_ONT, "06:00", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
  {"off_time",   OL_L_OFT, OL_H_OFT, "22:00", nullptr, 0, 0, 5, EGP_STRING, EGP_TIME},
#endif
#ifndef OLED_PINS_BLOCKED
  {"sda",        OL_L_SDA, OL_H_RBA, OLED_STR(OLED_SDA), nullptr, 0, 39, 0, EGP_UINT, 0},
  {"scl",        OL_L_SCL, OL_H_RBA, OLED_STR(OLED_SCL), nullptr, 0, 39, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup OLED_PREF_GROUP = {
  "display", "Display", 1,
  OLED_PREF_ITEMS,
  sizeof(OLED_PREF_ITEMS) / sizeof(OLED_PREF_ITEMS[0]),
};

const EGPrefGroup* SSD1306Display::prefs_group() { return &OLED_PREF_GROUP; }

void SSD1306Display::on_prefs_loaded() {
  setBrightness((uint8_t)EGPrefs::getUInt("display", "brightness"));
#ifdef GEIGER_PUSHBUTTON
  setTimeout((int)EGPrefs::getUInt("display", "timeout"));
#endif
#ifndef OLED_PINS_BLOCKED
  _pin_sda = (uint8_t)EGPrefs::getUInt("display", "sda");
  _pin_scl = (uint8_t)EGPrefs::getUInt("display", "scl");
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias OLED_LEGACY[] = {
  {"dispBrightness", "brightness"},
#ifdef GEIGER_PUSHBUTTON
  {"dispTimeout", "timeout"},
#else
  {"oledOn",  "on_time"},
  {"oledOff", "off_time"},
#endif
  {nullptr, nullptr},
};
const EGLegacyAlias* SSD1306Display::legacy_aliases() { return OLED_LEGACY; }
// === END LEGACY IMPORT ===

SSD1306Display::SSD1306Display(uint8_t _addr, int _sda, int _scl)
 : SSD1306Wire(_addr, _sda, _scl) {
  cx = 0;
  cy = 0;
}

#ifndef OLED_PINS_BLOCKED
void SSD1306Display::on_prefs_saved() {
  uint8_t sda = (uint8_t)EGPrefs::getUInt("display", "sda");
  uint8_t scl = (uint8_t)EGPrefs::getUInt("display", "scl");
  bool need_reboot = (sda != _pin_sda) || (scl != _pin_scl);
  on_prefs_loaded();
  if (need_reboot) EGPrefs::request_restart();
}
#endif

void SSD1306Display::loop(unsigned long now) {
    if (oled_page > OLED_PAGES) {
      oled_page = 1;
    }
    if (oled_page != _last_page) {
      EGModuleRegistry::set_loop_interval(this, oled_page == 4 ? 80 : 250);
      _last_page = oled_page;
    }
    if (oled_page == 4) {
      oled_timeout = now;
    }
#ifdef GEIGER_PUSHBUTTON
    if ((enable_oled_timeout) && (_lcd_timeout > 0) && ((now - oled_timeout) / 1000 > _lcd_timeout)) {
      if (oled_on) {
        displayOff();
        oled_on = false;
      }
      return;
    } else {
      if (oled_on == false) {
        displayOn();
        oled_page=1;
        oled_on = true;
        oled_last_update = now-20000;
      }
    }
#else
    if (isScreenOnTime(now)) {
      if (oled_on == false) {
        displayOn();
        oled_page = 1;
        oled_on = true;
        oled_last_update = now;
      }
    } else {
      if (oled_on) {
        displayOff();
        oled_on = false;
      }
      return;
    }
#endif
    if (oled_page == 1) {
      if ((now - oled_last_update >= 10000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_one_clear();
      }
      bool half = (now >> 9) & 1;
      if (half || (oled_last_update == now)) {
        page_one_values(now);
      }
      if (!half || (oled_last_update == now)) {
        page_one_graph();
      }
    } else if (oled_page == 2) {
      if ((now - oled_last_update >= 1000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_two_full();
      }
    } else if (oled_page == 3) {
      if ((now - oled_last_update >= 10000) || (oled_last_update == 0)) {
        oled_last_update = now;
        page_three_full();
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
    }
    display();

  }

  bool SSD1306Display::isScreenOnTime(unsigned long now) {
    static int16_t cached_on_mins = -1;
    static int16_t cached_off_mins = -1;

    static uint8_t tz_cnt = 0;
    if (cached_on_mins == -1 || ++tz_cnt >= 120) {
      tz_cnt = 0;
      ParsedTime on_time  = parseTime(EGPrefs::getString("display", "on_time"));
      ParsedTime off_time = parseTime(EGPrefs::getString("display", "off_time"));
      if (!on_time.isValid || !off_time.isValid) { cached_on_mins = -2; return true; }
      cached_on_mins = on_time.hour * 60 + on_time.minute;
      cached_off_mins = off_time.hour * 60 + off_time.minute;
    }

    if (cached_on_mins == -2) return true;

    time_t currentTime = time(NULL);
    struct tm *timeinfo = localtime(&currentTime);
    if (!timeinfo) return true;

    int now_mins = timeinfo->tm_hour * 60 + timeinfo->tm_min;
    if (cached_on_mins < cached_off_mins)
      return (now_mins >= cached_on_mins && now_mins < cached_off_mins);
    else
      return (now_mins >= cached_on_mins || now_mins < cached_off_mins);
  }

void SSD1306Display::page_two_full() {
  clear();
  setFont(ArialMT_Plain_10);
  drawString(0, 2, DeviceInfo::hostname());
  drawString(0, 17, PSTR("IP:"));
  drawString(16, 17, Wifi::ip);
  int uptime_y = 32;
#ifdef ESPG_HV_ADC
  drawString(0, uptime_y, PSTR("HV:"));
  char hvBuf[12];
  format_f(hvBuf, sizeof(hvBuf), hv.hvReading.get());
  drawString(20, uptime_y, hvBuf);
  uptime_y = 47;
#endif
  drawString(0, uptime_y, DeviceInfo::uptimeString());
}

void SSD1306Display::page_one_clear() {
  clear();
  setFont(DialogInput_plain_12);
  drawString(0,5, PSTR("CPM:"));
  drawString(0,20, PSTR("µSv/h:"));
  if (Wifi::disabled) {
    return;
  }
  drawXbm(120, 0, fontWidth, fontHeight, Wifi::connected?_iconimage_connected:_iconimage_disconnected);
}

void SSD1306Display::setup() {
  Wire.begin(_pin_sda, _pin_scl);
  SSD1306Wire::init();
#if OLED_FLIP
  flipScreenVertically();
#endif
  setBrightness(64);
  setFont(ArialMT_Plain_10);
  fontWidth = 8;
  fontHeight = 16;
  clear();
  drawXbm(0, 0, 51, 51, ESPLogo);
  drawString(55, 10, PSTR("ESPGeiger"));
  drawString(55, 24, RELEASE_VERSION);
  drawString(55, 42, PSTR("Connecting .."));

  display();
  setFont(ArialMT_Plain_16);
}

void SSD1306Display::onButtonTap(unsigned long now) {
  static unsigned long s_last_tap = 0;
  static uint8_t s_tap_count = 0;
  s_tap_count = (now - s_last_tap < 400) ? (s_tap_count + 1) : 1;
  s_last_tap = now;

  oled_timeout = now;
  if (!oled_on) {
    oled_page = 1;
    displayOn();
    oled_on = true;
  } else if (s_tap_count >= 5) {
    oled_page = 4;            // hidden gesture: 5 rapid taps
    s_tap_count = 0;
  } else {
    oled_page++;
    if (oled_page > 3) oled_page = 1;   // normal rotation skips page 4
  }
  oled_last_update = 0;
  loop(now);
}

void SSD1306Display::setupWifi(const char* s) {
  clear();
  setFont(DialogInput_plain_12);
  drawString(0, 10, PSTR("Setup - Connect to"));
  drawString(0, 24, PSTR("WiFi -"));
  drawString(0, 38, s);
  display();
}

void SSD1306Display::wifiDisabled() {
  setFont(ArialMT_Plain_10);
  fontWidth = 8;
  fontHeight = 16;
  clear();
  drawXbm(0, 0, 51, 51, ESPLogo);
  drawString(55, 10, PSTR("ESPGeiger"));
  drawString(55, 24, RELEASE_VERSION);
  drawString(55, 42, PSTR("Offline mode"));

  display();
  setFont(ArialMT_Plain_16);
}

void SSD1306Display::page_one_graph() {
  setColor(BLACK);
  fillRect(0, 35, OLED_WIDTH, 29);
  setColor(WHITE);

  drawLine(0, 63, 90, 63);
  drawLine(90, 35, 90, 63);
  if (gcounter.cpm_history.size() > 0) {
    int histSize = gcounter.cpm_history.size();
    int maxValue = gcounter.cpm_history[0];
    int minValue = histSize > 1 ? gcounter.cpm_history[0]:0;
    for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
      maxValue = gcounter.cpm_history[i] > maxValue ? gcounter.cpm_history[i] : maxValue;
      minValue = gcounter.cpm_history[i] < minValue ? gcounter.cpm_history[i] : minValue;
    }

    if (minValue > 1) {
      minValue = (int)(minValue * 0.9);
    }
    int x_start = 0;
    if (gcounter.cpm_history.capacity != histSize) {
      x_start = (2 * (gcounter.cpm_history.capacity - histSize));
    }

    char graphBuf[8];
    if (maxValue == 0) {
      setFont(Open_Sans_Digits_10);
      snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), minValue);
      drawString(93,55, graphBuf);
      return;
    }

    for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
      int location = ((map((long)gcounter.cpm_history[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;
      drawRect(x_start + i * 2, location, 2, (63 - location));
    }
    setFont(Open_Sans_Digits_10);
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), minValue);
    drawString(93,55, graphBuf);
    snprintf_P(graphBuf, sizeof(graphBuf), PSTR("%d"), maxValue);
    drawString(93,35, graphBuf);
  }
}

void SSD1306Display::page_one_values(unsigned long now) {
  setFont(DialogInput_Digits_17);
  setColor(BLACK);
  fillRect(45, 0, 72, 32);
  setColor(WHITE);
  char oledBuf[16];
  snprintf_P(oledBuf, sizeof(oledBuf), PSTR("%d"), gcounter.get_cpm());
  drawString(45,0, oledBuf);
  setFont(DialogInput_plain_12);
  format_f(oledBuf, sizeof(oledBuf), gcounter.get_usv());
  drawString(45,20, oledBuf);
  if (gcounter.cpm_history.capacity != gcounter.cpm_history.size()) {
    drawString(98,2, PSTR("W") );
  }
  if (send_indicator) {
    drawXbm(110, 0, fontWidth, fontHeight, _iconimage_remotext);
    send_indicator--;
  }
}

void SSD1306Display::page_three_full() {
  clear();
  setFont(ArialMT_Plain_10);
  static char versionString[32] = "";
  if (versionString[0] == '\0') {
    snprintf_P(versionString, sizeof(versionString), PSTR("%S / %S"), RELEASE_VERSION, GIT_VERSION);
  }
  drawString(0, 2, GEIGER_MODEL);
  drawString(0, 17, versionString);
  drawString(0, 32, PSTR(__DATE__ " " __TIME__));
  drawString(0, 47, PSTR("@ steadramon"));
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
  static Drop drops[MATRIX_DROPS];
  static unsigned long last_blip_seen = 0;
  static uint8_t last_col = 255;

  if (_page4_num_last == 0) {
    _page4_num_last = millis();
    last_blip_seen = gcounter.last_blip();
    for (uint8_t i = 0; i < MATRIX_DROPS; i++) drops[i].alive = false;
    // Pre-populate so the screen isn't blank on entry. They'll fall off
    // naturally as real blips arrive.
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

  clear();
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
      setPixel(d.x, ty);
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
  uint8_t noise[1024];
  GRNG::extract_fast(noise, sizeof(noise));
  clear();
  drawXbm(0, 0, 128, 64, noise);
  setColor(BLACK);
  fillRect(41, 19, 44, 24);
  setColor(WHITE);
  setFont(DialogInput_Digits_17);
  drawString(63 - (int)strlen(_page4_num) * 5, 21, _page4_num);
}

void SSD1306Display::showOTABanner() {
  clear();
  setTextAlignment(TEXT_ALIGN_CENTER);
  setFont(ArialMT_Plain_16);
  drawString(OLED_WIDTH / 2, (OLED_HEIGHT / 2) - 20, "Update");
  drawString(OLED_WIDTH / 2, (OLED_HEIGHT / 2),  "in progress...");
  displayOn();
  oled_on = true;
  display();
  setTextAlignment(TEXT_ALIGN_LEFT);
}
#endif
