/*
  TickProfile.cpp - Reactor::second timing + loop iteration counter.

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
#include "TickProfile.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Util/MathUtil.h"
#ifdef TICK_PROFILE
#include "DeviceInfo.h"
#endif

namespace corium {
namespace TickProfile {
  uint32_t tickUs = 0;
  uint32_t tickMaxUs = 0;
  uint32_t lps = 0;
  uint32_t lpsRaw = 0;
  uint32_t _lps_count = 0;
#ifdef LOOP_PROFILE
  uint32_t loopBodyUs = 0;
#endif
}
}  // namespace corium

static unsigned long s_start_us = 0;
static uint8_t s_max_age = 0;
#ifdef TICK_PROFILE
static unsigned long s_app_done_us = 0;
static unsigned long s_wifi_done_us = 0;
static uint32_t s_max_app_us = 0;
static uint32_t s_max_wifi_us = 0;
static uint32_t s_min_heap = 0xFFFFFFFFu;
#endif

void corium::TickProfile::beginTick() {
  s_start_us = micros();
}

#ifdef TICK_PROFILE
void corium::TickProfile::markApp()  { s_app_done_us = micros(); }
void corium::TickProfile::markWifi()    { s_wifi_done_us = micros(); }
#endif

bool corium::TickProfile::endTick() {
  // never reset, the loop owns it
  static uint32_t s_lps_last = 0;
  uint32_t n = _lps_count;
  uint32_t count = n - s_lps_last;
  s_lps_last = n;
  corium::TickProfile::lpsRaw = count;
  corium::TickProfile::lps = ema8(corium::TickProfile::lps, count);
  uint32_t us = (uint32_t)(micros() - s_start_us);
  corium::TickProfile::tickUs = ema8(corium::TickProfile::tickUs, us);
  if (us > corium::TickProfile::tickMaxUs) corium::TickProfile::tickMaxUs = us;
#ifdef TICK_PROFILE
  uint32_t app_us  = (uint32_t)(s_app_done_us - s_start_us);
  uint32_t wifi_us = (uint32_t)(s_wifi_done_us - s_app_done_us);
  if (app_us  > s_max_app_us)  s_max_app_us  = app_us;
  if (wifi_us > s_max_wifi_us) s_max_wifi_us = wifi_us;
  uint32_t cur_free = corium::DeviceInfo::freeHeap();
  if (cur_free < s_min_heap) s_min_heap = cur_free;
#endif
  if (++s_max_age >= 60) {
    s_max_age = 0;
#ifdef TICK_PROFILE
    corium::Log::console(PSTR("Tick profile: total=%u ctr=%u wifi=%u lps=%u free=%u min_free=%u frag=%u%%"),
      corium::TickProfile::tickMaxUs, s_max_app_us, s_max_wifi_us, corium::TickProfile::lps,
      (unsigned)cur_free, (unsigned)s_min_heap, (unsigned)corium::DeviceInfo::heapFrag());
    s_max_app_us = s_max_wifi_us = 0;
    s_min_heap = 0xFFFFFFFFu;
#endif
#ifdef LOOP_PROFILE
    corium::Registry::logLoopProfileAndReset();
    uint32_t body = loopBodyUs;
    loopBodyUs = 0;
    // of the 60 s, what loop() didn't get
    uint32_t stolen = (body > 60000000UL) ? 0 : (60000000UL - body);
    corium::Log::console(PSTR("Loop wall: body=%lums stolen=%lums (%lu%% async)"),
      (unsigned long)(body / 1000UL),
      (unsigned long)(stolen / 1000UL),
      (unsigned long)(stolen / 600000UL));
#endif
    corium::TickProfile::tickMaxUs = us;
    return true;
  }
  return false;
}
