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
#include "../Status.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef NTP_TZ
#  define NTP_TZ "GMT0"
#endif
/*
https://remotemonitoringsystems.ca/time-zone-abbreviations.php
*/
extern Status status;

class NTP_Client {
  public:
    NTP_Client();
    void loop();
    void setup();
    void set_server(char* ntpServer);
    void set_tz(char* ntpTZ);
  private:
    const char* _ntp_server = NTP_SERVER;
    const char* _ntp_tz   = NTP_TZ;
};

#endif