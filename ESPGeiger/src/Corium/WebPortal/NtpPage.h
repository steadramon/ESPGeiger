/*
  NtpPage.h - the Time / NTP block on /network, its JS and save.

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

#ifndef NTPPAGE_H
#define NTPPAGE_H

class EGHttpResponse;
class EGHttpServer;

namespace NtpPage {

void form(EGHttpResponse& res);
void routes(EGHttpServer& http);   // /ntpjs, /ntpset

}

#endif
