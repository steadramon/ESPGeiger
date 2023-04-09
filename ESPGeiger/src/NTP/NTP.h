/*
  Counter.h - Geiger Counter class
  
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

#include <ESPNtpClient.h>

const char* ntpServer = "pool.ntp.org";

void setupNTP()
{
  NTP.setInterval (120); // Sync each 2 minutes
  NTP.setMinSyncAccuracy (2000); // Sync accuracy target is 2 ms
  NTP.settimeSyncThreshold (1000); // Sync only if calculated offset absolute value is greater than 1 ms
  NTP.setMaxNumSyncRetry (2); // 2 resync trials if accuracy not reached
  NTP.begin (ntpServer); // Start NTP client
}
