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
#include "Corium/System/AppHooks.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/Module/Module.h"
#include "Corium/WebPortal/WebPortal.h"
#include "Counter/Counter.h"
#include "Counter/Tube.h"
#include "GRNG/GRNG.h"
#include "WebAPI/Features.h"
#include "App/Board.h"
#include "App/Health.h"
#include "Corium/Output/Buzzer.h"

namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }
namespace AppWeb { void routes(EGHttpServer& http); }

static const corium::MenuEntry APP_MENU[] PROGMEM = {
  {"/status",  "Status"},
  {"/hist",    "History"},
  {"", ""}
};

const corium::MenuEntry* corium::AppHooks::menu() { return APP_MENU; }

void corium::AppHooks::routes(EGHttpServer& http) {
  GRNGRoutes::registerRoutes(http);
  CounterRoutes::registerRoutes(http);
  AppWeb::routes(http);
}

static const char HEALTH[] PROGMEM = "Health";

void corium::AppHooks::infoRows(EGHttpResponse& res) {
  char model[6 * CORIUM_NAME_MAX + 1];
  egesc_html(model, sizeof(model), Tube::model());
  corium::WebPortal::infoRow(res, PSTR("Geiger"), W_FMT_S, model);
  if (const char* b = corium::DeviceInfo::boardName()) corium::WebPortal::infoRow(res, PSTR("Board"), W_FMT_S, b);
  char now[40], ever[40];
  health_text(now, sizeof(now), health().live());
  if (health().ever() == health().live()) {
    corium::WebPortal::infoRow(res, HEALTH, W_FMT_S, now);
  } else {
    health_text(ever, sizeof(ever), health().ever());
    corium::WebPortal::infoRow(res, HEALTH, PSTR("%s (since boot: %s)"), now, ever);
  }
#if BUZZER_BUILT
  if (corium::Buzzer::faulted()) corium::WebPortal::infoRow(res, corium::Buzzer::NAME, W_FMT_S, PSTR("pin held high, off until restart"));
#endif
}

static const char* geiger_type_name() {
  if (GEIGER_IS_PULSE(GEIGER_TYPE))  return PSTR("pulse");
  if (GEIGER_IS_SERIAL(GEIGER_TYPE)) return PSTR("serial");
  return PSTR("none");
}

void corium::AppHooks::aboutJson(EGHttpResponse& res) {
  char model[6 * CORIUM_NAME_MAX + 1];  // escaped worst case
  egesc_json(model, sizeof(model), Tube::model());
  corium::WebPortal::sendf(res,
    PSTR("\"g_mod\":\"%s\",\"g_type\":\"%s\",\"g_test\":%s,\"g_pcnt\":%s,\"fl\":%u,"),
    model,
    geiger_type_name(),
    GEIGER_IS_TEST(GEIGER_TYPE) ? W_TRUE : W_FALSE,
    gcounter.has_pcnt()         ? W_TRUE : W_FALSE,
    (unsigned)Features::flags());
  if (const char* b = corium::DeviceInfo::boardName()) corium::WebPortal::sendf(res, PSTR("\"board\":\"%s\","), b);
  char now[40], ever[40];
  health_text(now, sizeof(now), health().live());
  health_text(ever, sizeof(ever), health().ever());
  corium::WebPortal::sendf(res, PSTR("\"health\":\"%s\",\"health_boot\":\"%s\","), now, ever);
}

void corium::AppHooks::metrics(EGHttpResponse& res, const char* cid) {
  res.sendChunk(F("# HELP geiger_pulse_interval_seconds Inter-pulse intervals in seconds, log2 buckets\n"
                  "# TYPE geiger_pulse_interval_seconds histogram\n"));
  const uint8_t nb = Counter::pulse_histogram_buckets();
  uint32_t hist[nb];
  gcounter.pulse_histogram(hist);
  // cumulative, bucket b tops out at 64 us << b, last is +Inf
  uint32_t cumul = 0;
  for (uint8_t b = 0; b < nb; b++) {
    cumul += hist[b];
    if (b < nb - 1) {
      uint32_t ub_us = 64UL << b;
      corium::WebPortal::sendf(res,
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"%lu.%06lu\"} %lu\n"),
        cid,
        (unsigned long)(ub_us / 1000000UL),
        (unsigned long)(ub_us % 1000000UL),
        (unsigned long)cumul);
    } else {
      corium::WebPortal::sendf(res,
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"+Inf\"} %lu\n"),
        cid, (unsigned long)cumul);
    }
  }
  corium::WebPortal::sendf(res,
    PSTR("geiger_pulse_interval_seconds_count{chipid=\"%s\"} %lu\n"),
    cid, (unsigned long)cumul);
}
