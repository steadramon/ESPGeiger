/*
  NTP.cpp - functions to handle NTP
  
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

#include "NTP.h"
#include "../Logger/Logger.h"

void ntp_cb (NTPEvent_t e)
{
  switch (e.event) {
    case timeSyncd:
      if (status.ntp_synced == false) {
        Log::console(PSTR("NTP: Synched"));
        status.ntp_synced = true;
      }
      break;
    default:
      break;
  }
}

void setupNTP()
{
  Log::console(PSTR("NTP: Starting ... %s"), NTP_SERVER);
  NTP.setInterval (300); // Sync each 5 minutes
  NTP.onNTPSyncEvent (ntp_cb);
  NTP.begin(NTP_SERVER); // Start NTP client
}