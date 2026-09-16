/*
  AppHooks.cpp - ESPGeiger's side of the Corium hooks.

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
#include <Arduino.h>
#include <EGHttpServer.h>
#include <EGEscape.h>
#include "src/Corium/System/AppHooks.h"
#include "src/Corium/Prefs/EGPrefs.h"
#include "src/Corium/Util/StringUtil.h"
#include "src/Corium/Util/Words.h"
#include "src/Corium/Module/EGModule.h"
#include "src/Corium/WebPortal/WebPortal.h"
#include "src/Counter/Counter.h"
#include "src/Counter/Tube.h"
#include "src/GRNG/GRNG.h"
#include "src/SerialCommand/SerialCommand.h"
#include "src/WebAPI/Features.h"

namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }

static const EGMenuEntry APP_MENU[] PROGMEM = {
  {"/status",  "Status"},
  {"/hist",    "History"},
  {"", ""}
};

const EGMenuEntry* AppHooks::menu() { return APP_MENU; }

void AppHooks::console(const char* text) {
  char line[SERIALCOMMAND_BUFFER + 2];   // one over, so too long is refused
  size_t n = 0;
  while (text[n] && n < sizeof(line) - 1 && text[n] != '\r' && text[n] != '\n') {
    line[n] = text[n];
    n++;
  }
  line[n] = '\0';
  serialcmd.dispatch(line);
}

void AppHooks::routes(EGHttpServer& http) {
  GRNGRoutes::registerRoutes(http);
  CounterRoutes::registerRoutes(http);
}

void AppHooks::info_rows(EGHttpResponse& res) {
  char model[6 * EG_NAME_MAX + 1];
  egesc_html(model, sizeof(model), Tube::model());
  WebPortal::infoRow(res, PSTR("Geiger"), W_FMT_S, model);
  WebPortal::infoRow(res, PSTR("Feature flags"), PSTR("0x%04x"), (unsigned)Features::flags());
}

static const char* geiger_type_name() {
  if (GEIGER_IS_PULSE(GEIGER_TYPE))  return PSTR("pulse");
  if (GEIGER_IS_SERIAL(GEIGER_TYPE)) return PSTR("serial");
  return PSTR("none");
}

void AppHooks::about_json(EGHttpResponse& res) {
  char model[6 * EG_NAME_MAX + 1];  // escaped worst case
  egesc_json(model, sizeof(model), Tube::model());
  char buf[6 * EG_NAME_MAX + 80];
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("\"g_mod\":\"%s\",\"g_type\":\"%s\",\"g_test\":%s,\"g_pcnt\":%s,"),
    model,
    geiger_type_name(),
    GEIGER_IS_TEST(GEIGER_TYPE) ? W_TRUE : W_FALSE,
    gcounter.has_pcnt()         ? W_TRUE : W_FALSE);
  size_t len = 0;
  advance_pos(len, n, sizeof(buf));
  if (len) res.sendChunk(buf, len);
}

void AppHooks::metrics(EGHttpResponse& res, const char* cid) {
  res.sendChunk(F("# HELP geiger_pulse_interval_seconds Inter-pulse intervals in seconds, log2 buckets\n"
                  "# TYPE geiger_pulse_interval_seconds histogram\n"));
  const uint32_t* hist = gcounter.pulse_histogram();
  const uint8_t   nb   = Counter::pulse_histogram_buckets();
  uint32_t cumul = 0;
  for (uint8_t b = 0; b < nb; b++) {
    cumul += hist[b];
    if (b < nb - 1) {
      uint32_t ub_us = 64UL << b;
      WebPortal::metricsLine(res,
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"%lu.%06lu\"} %lu\n"),
        cid,
        (unsigned long)(ub_us / 1000000UL),
        (unsigned long)(ub_us % 1000000UL),
        (unsigned long)cumul);
    } else {
      WebPortal::metricsLine(res,
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"+Inf\"} %lu\n"),
        cid, (unsigned long)cumul);
    }
  }
  WebPortal::metricsLine(res,
    PSTR("geiger_pulse_interval_seconds_count{chipid=\"%s\"} %lu\n"),
    cid, (unsigned long)cumul);
}
