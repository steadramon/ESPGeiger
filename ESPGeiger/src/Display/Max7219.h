/*
  Max7219.h - MAX7219 display backend (7-seg, round and LED matrix), TM1637 7-seg.

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
#ifndef EG_MAX7219_H
#define EG_MAX7219_H
#ifdef MAX7219_DISPLAY
#ifndef SSD1306_DISPLAY
#error "MAX7219_DISPLAY needs SSD1306_DISPLAY: the Display layer is built with the OLED"
#endif
#include <Arduino.h>
#include <Max7219Driver.h>
#include <Tm1637Driver.h>
#include <SevenSeg.h>
#include <BarGraph.h>
#include "DisplayType.h"
#include "DisplayBackend.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"

class Max7219Display : public EGModule, public DisplayBackend {
  public:
    const char* name() override { return "max72"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    uint8_t display_order() override;
    bool hw_present() override { return true; }   // write only, no probe
    bool screenBuffer(const uint8_t*& buf, uint16_t& w, uint16_t& h);
    void onButtonTap(unsigned long now, int8_t dir);
    void begin() override;
    void start() override;
    void button(unsigned long now, int8_t dir) override { onButtonTap(now, dir); }
    bool screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) override {
      return screenBuffer(buf, w, h);
    }
    void reload() override;
    void safe_mode() override;
    const EGPrefGroup* prefs_group() override;
    const EGPrefShow* prefs_show() override;
    void on_prefs_loaded() override;
    bool has_tick() override { return true; }
    void s_tick(unsigned long now) override;   // 7-seg / round
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 60; }
    void loop(unsigned long now) override;      // matrix scroll

  private:
    void read_prefs();
    bool claim_pins();
    void apply();
    void applyTm();
    void update_power(unsigned long now);
    uint8_t adaptiveLevel(uint8_t maxLevel);
    uint8_t arcLevel();
    float   currentCPM();
    uint8_t buildPages(uint8_t* list);
    void    updatePage();
    void    pageInfo(uint8_t page, float& value, uint8_t& dec, const char*& unit);
    void    render7seg();
    void    renderRound();
    void    roundDigits(float v, uint8_t dec);
    void    matrixValueUnit(char* val, size_t vc, char* unit, size_t uc);
    void    pushNumber(uint8_t first_digit, uint8_t ndigits, float value,
                       uint8_t decimals, uint8_t dp_bar_level);
    void    pushArc(uint8_t level);   // 24 leds, regs 5-7
    void    pushArcMask(uint32_t mask);   // bit n = led n
    void    bootSweep();
    void    roundBoot();
    bool    tm() const { return _layout == DISP_TM1637; }
    void    pageSegs(uint8_t* segs);
    void    tmSend(const uint8_t* segs, uint8_t n);
    void    drawRep7seg(uint8_t* fb, uint16_t& w, uint16_t& h);
    void    drawRepRound(uint8_t* fb, uint16_t& w, uint16_t& h);

    Max7219Driver _drv;
    Tm1637Driver  _tm;            // DIN = DIO
    bool    _tm_acked = true;
    uint8_t _tm_wait = 0;
    bool    _tm_due = false;      // bit-banged in loop, not the ticker
    uint8_t _tm_fails = 0;
    bool    _tm_off = false;      // gave up, until reboot (as the OLED)
    static const uint8_t TM_RETRY_S = 10;
    static const uint8_t TM_GIVE_UP = 6;   // ~1 min of retries
    bool    _colon = false;       // no dots, a colon
    bool    _active = false;
    bool    _lit = true;          // false = shut down
    uint8_t _layout = 0;
    bool    _swept = false;
    uint8_t _set_layout = 0xFF;   // what the chip is set up for
    uint8_t _set_count = 0;

    int8_t  _din = -1, _clk = -1, _cs = -1;
    uint8_t _count = 8;           // 7-seg: digits, matrix: panels
    uint8_t _decimals = 2;
    bool    _bar = true;
    uint8_t _bar_gain = 2;        // full = gain * baseline
    uint8_t _bar_win = 1;         // 0 = 5 min, 1 = 15 min

    uint8_t  _mode = 1;           // 0 = cycle, 1 = fixed
    bool     vu() const;          // round, VU page
    bool     _reverse = false;    // 7-seg digit 0 on the left
    uint8_t  _page = 0;
    uint8_t  _cur_page = 0;
    unsigned long _page_since = 0;

    void    renderMatrixContent(unsigned long now);
    void    drawMatrixIcon(const uint8_t* rows8);
    int     mxText(uint8_t* fb, int W, int x, const char* s, bool draw, bool small);
    enum MxFont : uint8_t { MX_BIG, MX_SMALL };
    int mxWidth(const char* s, MxFont f) { return mxText(nullptr, 0, 0, s, false, f == MX_SMALL); }
    int mxDraw(uint8_t* fb, int W, int x, const char* s, MxFont f) {
      return mxText(fb, W, x, s, true, f == MX_SMALL);
    }
    uint8_t _panels = 1;          // 1-8
    uint8_t _orient = 0;          // FC-16, Generic, each upside down
    int16_t _scroll_x = 0;
    uint8_t _mfb[Max7219Driver::MAX_CHIPS * 8];   // page-major, 8 high
    uint16_t _mfb_w = 0;

    bool    fxTap(unsigned long now);
    void    fxFrame(int W);
    bool    fxScroll(int W);
    void    fxPlasma(int W);
    void    vuFrame();
    VuNeedle _vu = {0, 0};
    uint32_t _vu_blip = 0;
    unsigned long _vu_ms = 0;
    bool    _fx_on = false;
    uint8_t _fx_part = 0;
    uint16_t _fx = 0;
    uint8_t _taps = 0;
    unsigned long _tap_ms = 0;
};

extern Max7219Display max7219;

#endif
#endif
