/*
  AppHooks.h - app hooks, empty by default

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
#ifndef CORIUM_APP_HOOKS_H
#define CORIUM_APP_HOOKS_H

class EGHttpServer;
class EGHttpResponse;
namespace corium { struct MenuEntry; }

namespace corium {
namespace AppHooks {
  const MenuEntry* menu();   // Shown first on the home page, PROGMEM, not in safe mode
  void routes(EGHttpServer& http);   // not in safe mode
  void statusRows(EGHttpResponse& res);   // top of the System table on /info
  void infoRows(EGHttpResponse& res);   // About table on /info
  void rowTail(const char* mid, const char* id, EGHttpResponse& res);   // after a /param row
  void aboutJson(EGHttpResponse& res);   // /about fields, each ends with a comma
  void metrics(EGHttpResponse& res, const char* cid);   // After the measure rows
  void onCrash();   // 8266 crash callback, runs before the stack prints. No heap, Serial or libc in here
}
}  // namespace corium

// In PROGMEM, served after the core's own
extern const char APP_CSS[];        // style.css tail
extern const char APP_THEME_JS[];   // theme.js tail

#endif
