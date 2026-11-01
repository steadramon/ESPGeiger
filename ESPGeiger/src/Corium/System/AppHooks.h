/*
  AppHooks.h - What Corium calls in the app. Empty by default, the app's own win.

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
#ifndef APP_HOOKS_H
#define APP_HOOKS_H

class EGHttpServer;
class EGHttpResponse;
namespace corium { struct MenuEntry; }

namespace AppHooks {
  const corium::MenuEntry* menu();   // first on the home page, PROGMEM, not in safe mode
  void routes(EGHttpServer& http);   // not in safe mode
  void info_rows(EGHttpResponse& res);   // About table on /info
  void about_json(EGHttpResponse& res);   // /about fields, each ends with a comma
  void metrics(EGHttpResponse& res, const char* cid);   // after the measure rows
}

// PROGMEM, served after the core's own
extern const char APP_CSS[];        // style.css tail
extern const char APP_THEME_JS[];   // theme.js tail

#endif
