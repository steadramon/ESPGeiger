/*
  NTP.h - NTP class
  
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
#ifndef _NTP_h
#define _NTP_h
#include <Arduino.h>
#include <ESPNtpClient.h>
#include "timezones.h"
#include "../Status.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef NTP_TZ
#  define NTP_TZ "Etc/UTC"
#endif
/*
https://remotemonitoringsystems.ca/time-zone-abbreviations.php
https://sites.google.com/a/usapiens.com/opnode/time-zones
https://github.com/pgurenko/tzinfo
https://github.com/GuruSR/Olson2POSIX
https://github.com/HASwitchPlate/openHASP/
https://github.com/charles-haynes/Watchy-Screen
https://github.com/cmdc0de/corncon2022
https://github.com/CargoBikoMeter/WZePaperDisplay
https://github.com/search?q=MST7MDT%2CM3.2.0%2CM11.1.0+language%3AC%2B%2B+&type=code&p=1
https://www.openhasp.com/0.7.0/firmware/configuration/time/
https://remotemonitoringsystems.ca/time-zone-abbreviations.php
https://github.com/yugabyte/yugabyte-db/blob/2461909fc9c34441c352c97e1b059336ac0cfc46/src/yb/util/date_time.h
*/
extern Status status;

class NTP_Client {
  public:
  static NTP_Client &getInstance()
  {
    static NTP_Client instance;
    return instance;
  }
    NTP_Client();
    void loop();
    void setup();
    void saveconfig();
    void loadconfig();
    void set_server(const char* ntpServer) {
      memcpy(_ntp_server, ntpServer, 64);
    };
    void set_tz(const char* ntpTZ) {
      memcpy(_ntp_tz, ntpTZ, 64);
    };
    const char* get_server() {
      return _ntp_server;
    };
    const char* get_tz() {
      return _ntp_tz;
    };
    char _ntp_server[64] = NTP_SERVER;
    char _ntp_tz[64]      = NTP_TZ;
  private:
};

#endif