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

// "0x%08x " per word, longer than infoRow's buffer
static constexpr size_t FRAMES_MAX = corium::CrashDump::STACK_WORDS * 11 + 1;

#ifdef ESP8266
static void info_render_frames(EGHttpResponse& res, const corium::CrashDump::Snapshot* cd) {
  // code addresses only, iram and flash
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
  INFO_ROW("Restart reason", W_FMT_S, corium::DeviceInfo::resetText());
  uint32_t epc1, excvaddr;
  uint8_t exccause;
  if (corium::DeviceInfo::resetExc(&epc1, &excvaddr, &exccause)) {
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)excvaddr);
  }
  if (const corium::CrashDump::Snapshot* cd = corium::CrashDump::lastCrash()) {
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
  INFO_ROW("Restart reason", W_FMT_S, corium::DeviceInfo::resetText());
  if (const char* in = corium::Registry::crashedIn()) INFO_ROW("Crashed in", W_FMT_S, in);
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

// who set the clock
static void info_time(EGHttpResponse& res) {
  PGM_P src = PSTR("not set");
  if (corium::Clock::source == corium::Clock::NTP)       src = PSTR("NTP");
  else if (corium::Clock::source == corium::Clock::KEPT) src = PSTR("kept over a restart");
  else if (corium::Clock::source == corium::Clock::WEB)  src = PSTR("browser");
#ifdef CORIUM_RTC
  if (corium::Rtc::chip()) {
    if (corium::Clock::source == corium::Clock::RTC) INFO_ROW("Time", PSTR("RTC (%s)"), corium::Rtc::chip());
    else INFO_ROW("Time", PSTR("%s, RTC %s"), src, corium::Rtc::chip());
    return;
  }
#endif
  INFO_ROW("Time", W_FMT_S, src);
}

static void info_render_system(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>System</summary><table>"));
  INFO_ROW("Uptime", W_FMT_S, corium::DeviceInfo::uptimeString());
  info_time(res);
  INFO_ROW("Chip ID",       W_FMT_S, corium::DeviceInfo::chipId());   // udp pairing asks for it
  uint32_t heap = corium::DeviceInfo::freeHeap();
  // KB with one decimal, no float
  INFO_ROW("Free memory",   PSTR("%u.%u KB"), (unsigned)(heap / 1024), (unsigned)(heap % 1024 * 10 / 1024));
  INFO_ROW("Firmware",      PSTR("%u KB of %u KB"), (unsigned)(corium::DeviceInfo::sketchSize() / 1024),
                            (unsigned)((corium::DeviceInfo::sketchSize() + corium::DeviceInfo::freeSketch()) / 1024));
#ifdef ESP8266
  if (corium::DeviceInfo::flashSize() != corium::DeviceInfo::realFlash()) {
    INFO_ROW("Flash", PSTR("%u KB set, the chip has %u KB"), (unsigned)(corium::DeviceInfo::flashSize() / 1024),
             (unsigned)(corium::DeviceInfo::realFlash() / 1024));
  }
#endif
  info_render_reset(res);
  if (corium::BootGuard::safeMode()) INFO_ROW("Safe mode", PSTR("yes"));
  if (corium::BootGuard::crashStreak()) INFO_ROW("Crash boots in a row", W_FMT_U, (unsigned)corium::BootGuard::crashStreak());
  if (corium::SystemPrefs::passRestored) INFO_ROW("Web password", PSTR("restored from backup (settings lost)"));
  res.sendChunk(FPSTR(IR_END));
}

// -80 and weaker drops out
static PGM_P rssi_word(int r) {
  if (r >= -60) return PSTR("excellent");
  if (r >= -70) return PSTR("good");
  if (r >= -80) return PSTR("fair");
  return PSTR("weak");
}

static void info_render_wifi(EGHttpResponse& res, const InfoNet& net) {
  res.sendChunk(F("<details open><summary>WiFi</summary><table>"));
  if (net.ssid[0]) INFO_ROW("Network", PSTR("%s, channel %d"), net.ssid, (int)corium::Wifi::channel());
  if (corium::Wifi::isConnected()) INFO_ROW("Signal", PSTR("%d dBm (%s)"), (int)corium::Wifi::rssi, rssi_word(corium::Wifi::rssi));
  INFO_ROW("IP",             W_FMT_S,     net.ip);
  INFO_ROW("Hostname",       W_FMT_S,     corium::DeviceInfo::hostname());
  INFO_ROW("MAC",            W_FMT_S,     net.mac);
  INFO_ROW("DNS",            W_FMT_S,     net.dns);
  if (strcmp_P(net.ap_ip, PSTR("0.0.0.0"))) {   // ssid is kept with the AP off
    INFO_ROW("AP SSID",      W_FMT_S,     net.ap_ssid);
    INFO_ROW("AP IP",        W_FMT_S,     net.ap_ip);
    INFO_ROW("AP MAC",       W_FMT_S,     net.ap_mac);
  }
  res.sendChunk(FPSTR(IR_END));
}

static void info_render_about(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>About</summary><table>"));
  INFO_ROW("Build", PSTR(RELEASE_VERSION " " GIT_VERSION " " BUILD_ENV " %s"),
           corium::DeviceInfo::buildDate());
  corium::AppHooks::infoRows(res);
  res.sendChunk(FPSTR(IR_END));
}

#undef INFO_ROW

void corium::WebPortal::hInfo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Info"));

  InfoNet net;
  info_collect_net(net);

  sendf(res, W_CONNECTED, net.ssid[0] ? net.ssid : PSTR("(not connected)"));
  res.sendKV(F("</b> with IP <b>"), net.ip);
  res.sendKV(F("</b> &middot; "), corium::DeviceInfo::board(), F("</p>"));

  info_render_system(res);
  info_render_wifi(res, net);
  info_render_about(res);

  corium::WebPortal::endPage(res);
}

static void about_modules(EGHttpResponse& res) {
  uint8_t count = corium::Registry::count();
  for (uint8_t i = 0; i < count; i++) {
    corium::Module* m = corium::Registry::get(i);
    corium::WebPortal::sendf(res, PSTR("%s\"%s\""), (i > 0 ? PSTR(",") : PSTR("")), m->name());
  }
}

// what /info leaves out
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

  char gw[CORIUM_IPV4_MAX + 1], sn[CORIUM_IPV4_MAX + 1], bssid[18] = {}, tx[8];
  corium::Wifi::formatIP(gw, sizeof(gw), corium::Wifi::gatewayIP());
  corium::Wifi::formatIP(sn, sizeof(sn), corium::Wifi::subnetMask());
  if (const uint8_t* b = corium::Wifi::BSSID()) info_fmt_mac(bssid, sizeof(bssid), b);
  int q = corium::Wifi::txPowerQdbm();   // 0.25 dBm steps, -1 = unknown
  if (q >= 0) snprintf_P(tx, sizeof(tx), PSTR("%d.%02d"), q / 4, (q % 4) * 25);
  else flashToRam(PSTR("null"), tx, sizeof(tx));
  corium::WebPortal::sendf(res,
    PSTR("\"mhz\":%u,\"gw\":\"%s\",\"sn\":\"%s\",\"bssid\":\"%s\",\"tx\":%s,"
         "\"phy\":\"%s\",\"sleep\":\"%s\",\"auto\":%s,"),
    (unsigned)corium::DeviceInfo::cpuMHz(), gw, sn, bssid, tx, corium::Wifi::phyModeStr(), corium::Wifi::sleepModeStr(),
    corium::Wifi::getAutoReconnect() ? W_TRUE : W_FALSE);
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
  corium::AppHooks::aboutJson(res);
  res.sendChunk(F("\"modules\":["));
  about_modules(res);
  res.sendChunk(F("]}"));
  res.endChunked();
}
