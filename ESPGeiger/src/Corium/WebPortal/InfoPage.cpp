/*
  InfoPage.cpp - /info and /about.

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
#include "WebPortal.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Prefs/SystemPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/Fs.h"
#include "Corium/System/TickProfile.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Net/WifiInfo.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include <EGEscape.h>
#include <EGPortal.h>
#include "Corium/System/Clock.h"
#include "Corium/System/Rtc.h"
#include "Corium/System/AppHooks.h"

static const char IR_BYTES[] PROGMEM = "%u bytes";
static const char IR_HEX[]   PROGMEM = "0x%08x";
static const char IR_CODE_END[] PROGMEM = "</code></td></tr>";
static const char IR_END[]   PROGMEM = "</table></details>";

static const uint8_t BAR_WARN = 85;   // %, same as the ota check
static constexpr size_t BAR_MAX = 56;

// num of den as a bar, amber from BAR_WARN up when warn is set
static const char* info_bar(char* b, uint32_t num, uint32_t den, bool warn) {
  uint32_t pct = num < den ? num * 100 / den : 100;
  snprintf_P(b, BAR_MAX, PSTR("<div class=bar><i%s style=width:%u%%></i></div>"),
             warn && pct >= BAR_WARN ? PSTR(" class=w") : PSTR(""), (unsigned)pct);
  return b;
}

void corium::WebPortal::infoRow(EGHttpResponse& res, PGM_P key, PGM_P fmt, ...) {
  char row[320];
  size_t pos = 0;
  advancePos(pos, snprintf_P(row, sizeof(row), PSTR("<tr><th>%s</th><td>"), key), sizeof(row));
  va_list ap;
  va_start(ap, fmt);
  advancePos(pos, vsnprintf_P(row + pos, sizeof(row) - pos, fmt, ap), sizeof(row));
  va_end(ap);
  advancePos(pos, snprintf_P(row + pos, sizeof(row) - pos, PSTR("</td></tr>")), sizeof(row));
  res.sendChunk(row, pos);
}

#define INFO_ROW(k, fmt, ...) corium::WebPortal::infoRow(res, PSTR(k), fmt, ##__VA_ARGS__)

struct InfoNet {
  char ip[CORIUM_IPV4_MAX + 1], dns[CORIUM_IPV4_MAX + 1];
  char mac[18];
  char ssid[6 * EGPORTAL_SSID_MAX + 1];   // escaped worst case
  char ap_ssid[EGPORTAL_SSID_MAX + 1], ap_ip[CORIUM_IPV4_MAX + 1], ap_mac[18];
};

static void info_fmt_mac(char* dst, size_t cap, const uint8_t* m) {
  snprintf_P(dst, cap, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void info_collect_net(InfoNet& net) {
  corium::Wifi::formatIP(net.ip,    sizeof(net.ip),    corium::Wifi::localIP());
  corium::Wifi::formatIP(net.dns,   sizeof(net.dns),   corium::Wifi::dnsIP());
  corium::Wifi::formatIP(net.ap_ip, sizeof(net.ap_ip), corium::Wifi::softAPIP());
  strlcpy(net.mac, corium::DeviceInfo::mac(), sizeof(net.mac));
  uint8_t mac[6];
  corium::Wifi::softAPmacAddress(mac);
  info_fmt_mac(net.ap_mac, sizeof(net.ap_mac), mac);
  egesc_html(net.ssid, sizeof(net.ssid), corium::Wifi::isConnected() ? corium::Wifi::SSID().c_str() : "");
  strlcpy(net.ap_ssid, corium::Wifi::softAPSSID().c_str(), sizeof(net.ap_ssid));
}

// "0x%08x " per word, which is longer than infoRow's buffer
static constexpr size_t FRAMES_MAX = corium::CrashDump::STACK_WORDS * 11 + 1;

static void info_reset_head(EGHttpResponse& res) {
  INFO_ROW("Restart reason", W_FMT_S, corium::DeviceInfo::resetText());
  if (const char* in = corium::Registry::crashedIn()) INFO_ROW("Crashed in", W_FMT_S, in);
  if (corium::CrashDump::reset().damaged) INFO_ROW("Crash dump", PSTR("damaged"));
}

#ifdef ESP8266
static void info_render_frames(EGHttpResponse& res, const corium::CrashDump::Snapshot* cd) {
  // Code addresses only, iram and flash
  char frames[FRAMES_MAX];
  size_t pos = 0;
  bool any = false;
  for (uint32_t i = 0; i < cd->wordCount && pos < sizeof(frames) - 12; i++) {
    uint32_t w = cd->stack[i];
    if (w < 0x40100000u || w >= 0x40300000u) continue;
    int wn = snprintf_P(frames + pos, sizeof(frames) - pos,
                        PSTR("%s0x%08x"), any ? PSTR(" ") : PSTR(""), (unsigned)w);
    if (wn > 0 && (size_t)wn < sizeof(frames) - pos) pos += (size_t)wn;
    any = true;
  }
  if (any) {
    frames[pos] = '\0';
    res.sendKV(F("<tr><th>Stack frames</th><td><code>"), frames, FPSTR(IR_CODE_END));
  }
}

static void info_render_reset(EGHttpResponse& res) {
  info_reset_head(res);
  const corium::CrashDump::Reset& r = corium::CrashDump::reset();
  const corium::CrashDump::Snapshot* cd = corium::CrashDump::lastCrash();
  uint32_t epc1, excvaddr;
  uint8_t exccause;
  bool exc = corium::DeviceInfo::resetExc(&epc1, &excvaddr, &exccause);
  // A soft wdt's pc is only in the dump
  if (cd && r.kind == corium::CrashDump::K_SW_WDT) {
    epc1 = cd->epc1;
    excvaddr = cd->excvaddr;
    exccause = (uint8_t)cd->exccause;
    exc = true;
  }
  if (exc) {
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)excvaddr);
  }
  if (r.kind == corium::CrashDump::K_OOM) INFO_ROW("Failed alloc", IR_HEX, (unsigned)r.pc);
  if (cd) {
    INFO_ROW("EPC2",      IR_HEX, (unsigned)cd->epc2);
    INFO_ROW("EPC3",      IR_HEX, (unsigned)cd->epc3);
    INFO_ROW("DEPC",      IR_HEX, (unsigned)cd->depc);
    INFO_ROW("SP",        IR_HEX, (unsigned)cd->sp);
    info_render_frames(res, cd);
  }
}
#elif defined(ESP32)
static void info_render_backtrace(EGHttpResponse& res, const corium::CrashDump::Snapshot* cd) {
  char frames[FRAMES_MAX];
  size_t pos = 0;
  for (uint32_t i = 0; i < cd->wordCount && pos < sizeof(frames) - 12; i++) {
    int wn = snprintf_P(frames + pos, sizeof(frames) - pos,
                        PSTR("%s0x%08x"), i ? " " : "", (unsigned)cd->stack[i]);
    if (wn > 0 && (size_t)wn < sizeof(frames) - pos) pos += (size_t)wn;
  }
  frames[pos] = '\0';
#ifdef __riscv
  res.sendKV(F("<tr><th>Stack</th><td><code>"), frames, FPSTR(IR_CODE_END));   // raw words, not PCs
#else
  res.sendKV(F("<tr><th>Backtrace</th><td><code>"), frames, FPSTR(IR_CODE_END));
#endif
}

static void info_render_reset(EGHttpResponse& res) {
  info_reset_head(res);
  if (const corium::CrashDump::Snapshot* cd = corium::CrashDump::lastCrash()) {
#ifdef __riscv
    INFO_ROW("mcause",    W_FMT_U,   (unsigned)cd->exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)cd->epc1);
    INFO_ROW("mtval",     IR_HEX, (unsigned)cd->excvaddr);
#else
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)cd->exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)cd->epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)cd->excvaddr);
#endif
    if (cd->epc2) INFO_ROW("EPC2", IR_HEX, (unsigned)cd->epc2);
    if (cd->epc3) INFO_ROW("EPC3", IR_HEX, (unsigned)cd->epc3);
    if (cd->sp)   INFO_ROW("SP",   IR_HEX, (unsigned)cd->sp);
    if (cd->wordCount) info_render_backtrace(res, cd);
  }
}
#endif

// Who set the clock, NTP isn't news
static void info_time(EGHttpResponse& res) {
  PGM_P src = PSTR("not set");
  if (corium::Clock::source == corium::Clock::NTP)       src = W_NTP;
  else if (corium::Clock::source == corium::Clock::KEPT) src = PSTR("kept over a restart");
  else if (corium::Clock::source == corium::Clock::WEB)  src = W_BROWSER;
#ifdef CORIUM_RTC
  if (corium::Rtc::chip()) {
    if (corium::Clock::source == corium::Clock::RTC) INFO_ROW("Time", PSTR("RTC (%s)"), corium::Rtc::chip());
    else INFO_ROW("Time", PSTR("%s, RTC %s"), src, corium::Rtc::chip());
    return;
  }
#endif
  if (corium::Clock::source != corium::Clock::NTP) INFO_ROW("Time", W_FMT_S, src);
}

static void info_render_system(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>System</summary><table>"));
  corium::AppHooks::statusRows(res);
  INFO_ROW("Uptime", W_FMT_S, corium::DeviceInfo::uptimeString());
  info_time(res);
  INFO_ROW("Chip ID",       W_FMT_S, corium::DeviceInfo::chipId());   // udp pairing asks for it
  uint32_t heap = corium::DeviceInfo::freeHeap();
  uint32_t total = corium::DeviceInfo::heapSize();
  uint32_t sketch = corium::DeviceInfo::sketchSize();
  uint32_t slot = corium::DeviceInfo::freeSketch();   // what the next update gets
  if (slot > corium::DeviceInfo::appMax()) slot = corium::DeviceInfo::appMax();
  char barhtml[BAR_MAX];
  // KB with one decimal, no float
  INFO_ROW("Free memory",   PSTR("%u.%u KB of %u KB%s"), (unsigned)(heap / 1024), (unsigned)(heap % 1024 * 10 / 1024),
                            (unsigned)(total / 1024), info_bar(barhtml, total > heap ? total - heap : 0, total, false));
  INFO_ROW("Firmware",      PSTR("%u KB of %u KB%s"), (unsigned)(sketch / 1024), (unsigned)(slot / 1024),
                            info_bar(barhtml, sketch, slot, true));
  uint32_t fs_used, fs_total;
  if (corium::Fs::usage(&fs_used, &fs_total)) {
    INFO_ROW("Files", PSTR("%u KB of %u KB (%u%%)%s"), (unsigned)(fs_used / 1024), (unsigned)(fs_total / 1024),
             (unsigned)((uint64_t)fs_used * 100 / fs_total), info_bar(barhtml, fs_used, fs_total, true));
  }
  uint32_t wear;
  if (corium::Fs::wearPct100(&wear)) INFO_ROW("Flash wear", PSTR("%u.%02u%%"), (unsigned)(wear / 100), (unsigned)(wear % 100));
#if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32)
  // the classic esp32 reads a fixed figure
  float t = corium::DeviceInfo::chipTempC();
  if (!isnan(t)) INFO_ROW("Chip temperature", PSTR("%.1f &deg;C"), t);
#endif
#ifdef ESP8266
  if (corium::DeviceInfo::flashSize() != corium::DeviceInfo::realFlash()) {
    INFO_ROW("Flash", PSTR("%u KB set, the chip has %u KB"), (unsigned)(corium::DeviceInfo::flashSize() / 1024),
             (unsigned)(corium::DeviceInfo::realFlash() / 1024));
  }
#endif
  info_render_reset(res);
  if (corium::BootGuard::safeMode()) INFO_ROW("Safe mode", PSTR("yes"));
  if (corium::BootGuard::crashStreak()) INFO_ROW("Crash boots in a row", W_FMT_U, (unsigned)corium::BootGuard::crashStreak());
  if (corium::SystemPrefs::passRestored) corium::WebPortal::infoRow(res, corium::L_WEB_PASS, PSTR("restored from backup (settings lost)"));
  res.sendChunk(FPSTR(IR_END));
}

// same sum as the status page
static int rssi_pct(int r) {
  if (r <= -100) return 0;
  if (r >= -50) return 100;
  return 2 * (r + 100);
}

static void info_render_wifi(EGHttpResponse& res, const InfoNet& net) {
  res.sendChunk(F("<details open><summary>WiFi</summary><table>"));
  if (net.ssid[0]) INFO_ROW("Network", PSTR("%s, channel %d"), net.ssid, (int)corium::Wifi::channel());
  char barhtml[BAR_MAX];
  if (corium::Wifi::isConnected()) {
    int pct = rssi_pct(corium::Wifi::rssi);
    INFO_ROW("Signal", PSTR("%d dBm (%d%%)%s"), (int)corium::Wifi::rssi, pct, info_bar(barhtml, pct, 100, false));
  }
  INFO_ROW("IP",             W_FMT_S,     net.ip);
  INFO_ROW("Hostname",       W_FMT_S,     corium::DeviceInfo::hostname());
  INFO_ROW("MAC",            W_FMT_S,     net.mac);
  INFO_ROW("DNS",            W_FMT_S,     net.dns);
  if (corium::Wifi::ap) {   // the esp32 keeps the AP ip with it off
    INFO_ROW("Hotspot SSID", W_FMT_S,     net.ap_ssid);
    INFO_ROW("Hotspot IP",   W_FMT_S,     net.ap_ip);
    INFO_ROW("Hotspot MAC",  W_FMT_S,     net.ap_mac);
  }
  res.sendChunk(FPSTR(IR_END));
}

static void info_render_about(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>About</summary><table>"));
  INFO_ROW("Version", PSTR(RELEASE_VERSION " (" GIT_VERSION ", %s)"), corium::DeviceInfo::buildDate());
  INFO_ROW("Build",   PSTR(BUILD_ENV));
  corium::AppHooks::infoRows(res);
  res.sendChunk(FPSTR(IR_END));
}

#undef INFO_ROW

void corium::WebPortal::hInfo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Info"));

  InfoNet net;
  info_collect_net(net);

  sendf(res, W_CONNECTED, net.ssid[0] ? net.ssid : W_NONE);
  res.sendKV(F("</b> with IP <b>"), net.ip);
  res.sendKV(F("</b> &middot; "), corium::DeviceInfo::board(), F("</p>"));

  info_render_system(res);
  info_render_wifi(res, net);
  info_render_about(res);
  res.sendChunk(F("<p><a href=/about>More</a></p>"));

  corium::WebPortal::endPage(res);
}

// Each module as name: peak us, plus stalls when there are any
static void ps_modules(EGHttpResponse& res) {
  uint8_t count = corium::Registry::count();
  for (uint8_t i = 0; i < count; i++) {
    corium::WebPortal::sendf(res, PSTR("%s\"%s\":{"), (i > 0 ? PSTR(",") : PSTR("")), corium::Registry::get(i)->name());
    PGM_P sep = PSTR("");   // {} = nothing to say
    if (uint32_t p = corium::Registry::peakUs(i)) { corium::WebPortal::sendf(res, PSTR("\"peak\":%u"), (unsigned)p); sep = PSTR(","); }
    if (uint8_t n = corium::Registry::stalls(i)) { corium::WebPortal::sendf(res, PSTR("%s\"stalls\":%u"), sep, (unsigned)n); sep = PSTR(","); }
    if (const char* st = corium::Registry::state(i)) corium::WebPortal::sendf(res, PSTR("%s\"state\":\"%s\""), sep, st);
    res.sendChunk(F("}"));
  }
}

#ifdef CORIUM_TRACE
// On crash boots, the last modules that fired and how many ms before the last one
static void about_trace(EGHttpResponse& res) {
  const uint32_t* t = corium::Registry::trace();
  if (!t) return;
  uint8_t count = corium::Registry::count();
  uint32_t last = t[corium::Registry::TRACE_N - 1] >> 8;
  const char* sep = PSTR("");
  res.sendChunk(F(",\"trace\":["));
  for (uint8_t i = 0; i < corium::Registry::TRACE_N; i++) {
    uint8_t slot = t[i] & 0xFF;
    if (slot >= count) continue;   // empty
    long ago = (long)((last - (t[i] >> 8)) & 0xFFFFFF);   // 24 bit ms
    corium::WebPortal::sendf(res, PSTR("%s[\"%s\",%ld]"), sep, corium::Registry::get(slot)->name(), -ago);
    sep = PSTR(",");
  }
  res.sendChunk(F("]"));
}
#endif

// What /info leaves out
static void about_device(EGHttpResponse& res) {
#ifdef ESP8266
  corium::WebPortal::sendf(res,
    PSTR("\"core\":\"%s\",\"boot\":%u,\"fsize\":%u,\"freal\":%u,"),
    corium::DeviceInfo::coreVersion().c_str(), (unsigned)corium::DeviceInfo::bootVersion(),
    (unsigned)corium::DeviceInfo::flashSize(), (unsigned)corium::DeviceInfo::realFlash());
#elif defined(ESP32)
  char celsius[8] = "null";
  float t = corium::DeviceInfo::chipTempC();
  if (!isnan(t)) snprintf_P(celsius, sizeof(celsius), PSTR("%.1f"), t);
  corium::WebPortal::sendf(res,
    PSTR("\"model\":\"%s\",\"rev\":%u,\"sdk\":\"%s\",\"fsize\":%u,\"temp\":%s,"),
    corium::DeviceInfo::chipModel(), (unsigned)corium::DeviceInfo::chipRev(), corium::DeviceInfo::sdkVersion(),
    (unsigned)corium::DeviceInfo::flashSize(), celsius);
#endif

  // no gateway, mask or bssid: it gets pasted into bug reports
  char tx[8];
  int q = corium::Wifi::txPowerQdbm();   // 0.25 dBm steps, -1 = unknown
  if (q >= 0) snprintf_P(tx, sizeof(tx), PSTR("%d.%02d"), q / 4, (q % 4) * 25);
  else flashToRam(PSTR("null"), tx, sizeof(tx));
  corium::WebPortal::sendf(res,
    PSTR("\"mhz\":%u,\"tx\":%s,"
         "\"phy\":\"%s\",\"sleep\":\"%s\",\"auto\":%s,\"wifi\":\"%s\","),
    (unsigned)corium::DeviceInfo::cpuMHz(), tx, corium::Wifi::phyModeStr(), corium::Wifi::sleepModeStr(),
    corium::Wifi::getAutoReconnect() ? W_TRUE : W_FALSE, corium::Wifi::stateName(corium::Wifi::state()));
}

void corium::WebPortal::hAbout(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  uint8_t mac[6];
  corium::Wifi::macAddress(mac);
  char hex[13];
  snprintf_P(hex, sizeof(hex), PSTR("%02X%02X%02X%02X%02X%02X"),
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  sendf(res,
    PSTR("{\"ver\":\"" RELEASE_VERSION "\",\"git\":\"" GIT_VERSION "\",\"env\":\"" BUILD_ENV "\","
         "\"date\":\"%s\",\"chip\":\"%s\",\"flash\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\","),
    corium::DeviceInfo::buildDate(),
    corium::DeviceInfo::board(),   // "chip" carries the board
    corium::DeviceInfo::flashInfo(),
    hex,
    corium::DeviceInfo::hostname());

  about_device(res);
  uint32_t wear;
  if (corium::Fs::wearPct100(&wear)) sendf(res, PSTR("\"wear\":%u.%02u,"), (unsigned)(wear / 100), (unsigned)(wear % 100));
  if (uint32_t st = corium::DeviceInfo::stackFree()) sendf(res, PSTR("\"stack\":%u,"), (unsigned)st);
  corium::AppHooks::aboutJson(res);
  sendf(res, PSTR("\"erases\":%u"), (unsigned)corium::Fs::wearBoot());   // last, no comma
#ifdef CORIUM_TRACE
  about_trace(res);
#endif
  res.sendChunk(F("}"));
  res.endChunked();
}

// the loop, then each module's slowest recent loop, slow loops and what holds it
void corium::WebPortal::hPs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  sendf(res, PSTR("{\"lps\":%u,\"tick\":%u,\"t_max\":%u,\"modules\":{"), (unsigned)corium::TickProfile::lps,
        (unsigned)corium::TickProfile::tickUs, (unsigned)corium::TickProfile::tickMaxUs);
  ps_modules(res);
  res.sendChunk(F("}}"));
  res.endChunked();
}
