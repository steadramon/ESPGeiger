/*
  Rtc.cpp - a battery clock on the i2c bus: the time at power-up, kept by NTP.

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
#include "App/AppConfig.h"
#ifdef CORIUM_RTC

#include "Rtc.h"
#include <Wire.h>
#include <time.h>
#include "Clock.h"
#include "I2CBus.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Util/Attr.h"
#include "Corium/Util/Words.h"

// DS3231 / DS1307 time from 0x00, PCF8563 / BM8563 from 0x02
static const uint8_t DS_ADDR    = 0x68;
static const uint8_t DS_STATUS  = 0x0F;   // bit 7 OSF, the clock stopped
static const uint8_t PCF_ADDR   = 0x51;
static const uint8_t PCF_TIME   = 0x02;   // seconds bit 7 VL, the clock stopped
static const uint8_t PCF_CLKOUT = 0x0D;
static const unsigned long WRITE_MS = 24UL * 3600UL * 1000UL;   // its drift, once a day

enum Chip : uint8_t { NONE, DS, PCF };
static Chip s_chip = NONE;

namespace corium {
namespace Rtc {
  PGM_P chip() {
    if (s_chip == DS)  return PSTR("DS3231");
    if (s_chip == PCF) return PSTR("PCF8563");
    return nullptr;
  }
}
}  // namespace corium

static bool rd(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool wr(uint8_t addr, uint8_t reg, const uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(buf, n);
  return Wire.endTransmission() == 0;
}

// not bcd = 0xFF, out of every range below. tens past 9 already are
static uint8_t bcd(uint8_t b) {
  if ((b & 0x0F) > 9) return 0xFF;
  return (uint8_t)((b >> 4) * 10 + (b & 0x0F));
}
static uint8_t to_bcd(uint8_t v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }

// 2000-2199; the chips count a 29 Feb 2100, this doesn't
static bool leap(uint16_t y) { return (y & 3) == 0 && y != 2100; }
static const uint16_t BEFORE[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

// years from 2000, the month's bit 7 the century; 0 = 20xx as RTClib and Linux write it
static uint8_t year_of(uint8_t yb, uint8_t mb) {
  uint8_t y = bcd(yb);
  if (y > 99) return 0xFF;
  return (uint8_t)((mb & 0x80) ? y + 100 : y);
}

// 0 = not a time
static time_t epoch_of(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t m, uint8_t s) {
  if (y > 199 || mo < 1 || mo > 12 || h > 23 || m > 59 || s > 59) return 0;
  uint8_t month_days = (uint8_t)(BEFORE[mo] - BEFORE[mo - 1] + (mo == 2 && leap(2000 + y)));
  if (d < 1 || d > month_days) return 0;
  uint32_t days = 10957;   // 1970 to 2000
  for (uint16_t i = 2000; i < 2000 + y; i++) days += leap(i) ? 366 : 365;
  days += BEFORE[mo - 1] + d - 1;
  if (mo > 2 && leap(2000 + y)) days++;
  uint64_t e = (uint64_t)days * 86400 + h * 3600UL + m * 60UL + s;   // 2106 on is past 32 bits
  return e > corium::Clock::MAX_EPOCH ? 0 : (time_t)e;
}

// 12 h mode too, writes are always 24
static uint8_t ds_hour(uint8_t b) {
  if (!(b & 0x40)) return bcd(b & 0x3F);
  uint8_t h = bcd(b & 0x1F);
  if (h < 1 || h > 12) return 0xFF;   // 12 h runs 1-12
  return (uint8_t)(h % 12 + ((b & 0x20) ? 12 : 0));
}

// 0 = none, or it stopped since it was set
static time_t read_time() {
  uint8_t t[7];
  if (s_chip == PCF) {
    if (!rd(PCF_ADDR, PCF_TIME, t, 7) || (t[0] & 0x80)) return 0;
    return epoch_of(year_of(t[6], t[5]), bcd(t[5] & 0x1F), bcd(t[3] & 0x3F), bcd(t[2] & 0x3F),
                    bcd(t[1] & 0x7F), bcd(t[0] & 0x7F));
  }
  uint8_t st = 0;
  // seconds bit 7 halts a DS1307
  if (!rd(DS_ADDR, 0, t, 7) || !rd(DS_ADDR, DS_STATUS, &st, 1) || (t[0] & 0x80) || (st & 0x80)) return 0;
  return epoch_of(year_of(t[6], t[5]), bcd(t[5] & 0x1F), bcd(t[4] & 0x3F), ds_hour(t[2]),
                  bcd(t[1] & 0x7F), bcd(t[0] & 0x7F));
}

// a DS1307 has no century bit, to 2099 only
CORIUM_NODISCARD static bool write_time(time_t now) {
  struct tm tm;
  gmtime_r(&now, &tm);
  if (tm.tm_year < 100 || tm.tm_year > 299) return false;
  uint8_t yy = (uint8_t)(tm.tm_year - 100);
  uint8_t s = to_bcd((uint8_t)tm.tm_sec), m = to_bcd((uint8_t)tm.tm_min), h = to_bcd((uint8_t)tm.tm_hour);
  uint8_t d = to_bcd((uint8_t)tm.tm_mday);
  uint8_t mo = (uint8_t)(to_bcd((uint8_t)(tm.tm_mon + 1)) | (yy >= 100 ? 0x80 : 0));
  uint8_t y = to_bcd((uint8_t)(yy % 100));
  if (s_chip == PCF) {
    const uint8_t t[7] = {s, m, h, d, (uint8_t)tm.tm_wday, mo, y};   // VL cleared
    return wr(PCF_ADDR, PCF_TIME, t, 7);
  }
  const uint8_t t[7] = {s, m, h, (uint8_t)(tm.tm_wday + 1), d, mo, y};
  uint8_t st = 0;
  if (!wr(DS_ADDR, 0, t, 7) || !rd(DS_ADDR, DS_STATUS, &st, 1)) return false;
  st &= 0x7F;   // OSF, a DS1307's ram byte there
  return wr(DS_ADDR, DS_STATUS, &st, 1);
}

class RtcModule : public corium::Module {
public:
  const char* name() override { return PSTR("rtc"); }
  void begin() override;
  void loop(unsigned long now) override;
private:
  corium::Clock::Source _from = corium::Clock::NONE;   // what it was last set from
  unsigned long _written_at = 0;
};

static RtcModule rtcmodule;
CORIUM_REGISTER_MODULE(rtcmodule)

static bool here(uint8_t addr, uint8_t reg) {
  uint8_t b;
  return rd(addr, reg, &b, 1);
}

void RtcModule::begin() {
  corium::Registry::every(this, 0);
  if (!corium::I2CBus::begin()) return;
  if (here(PCF_ADDR, PCF_TIME)) s_chip = PCF;
  else if (here(DS_ADDR, 0)) s_chip = DS;
  else return;
  if (s_chip == PCF) {
    const uint8_t off = 0;
    wr(PCF_ADDR, PCF_CLKOUT, &off, 1);   // on at power-up
  }
  corium::Log::console(PSTR("RTC: %s found"), corium::Rtc::chip());
  if (!corium::Clock::synced) {
    time_t e = read_time();
    if (e >= corium::Clock::BUILT_EPOCH) {
      corium::Clock::set(e, corium::Clock::RTC);
      corium::Log::console(PSTR("RTC: Clock set from RTC"));
    } else {
      corium::Log::console(PSTR("RTC: Not set, waiting for NTP"));
    }
  }
  corium::Registry::every(this, 60000);
}

// NTP's or the browser's time into it, then daily; offline the browser is all it gets
void RtcModule::loop(unsigned long now) {
  bool ntp = corium::Clock::source == corium::Clock::NTP;
  if (!ntp && corium::Clock::source != corium::Clock::WEB) return;
  // NTP after the browser goes in at once
  if (corium::Clock::source == _from && now - _written_at < WRITE_MS) return;
  if (!write_time(time(nullptr))) return;
  if (corium::Clock::source != _from) corium::Log::console(PSTR("RTC: Set from %s"), ntp ? W_NTP : W_BROWSER);
  _from = corium::Clock::source;
  _written_at = now;
}

#endif
