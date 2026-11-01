/*
  AppHooks.cpp - Corium's empty hooks, an app's own replace them.

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
#include "Corium/System/AppHooks.h"

#define APP_HOOK __attribute__((weak))

APP_HOOK const corium::MenuEntry* AppHooks::menu() { return nullptr; }
APP_HOOK void AppHooks::routes(EGHttpServer&) {}
APP_HOOK void AppHooks::info_rows(EGHttpResponse&) {}
APP_HOOK void AppHooks::about_json(EGHttpResponse&) {}
APP_HOOK void AppHooks::metrics(EGHttpResponse&, const char*) {}

APP_HOOK extern const char APP_CSS[] PROGMEM = "";
APP_HOOK extern const char APP_THEME_JS[] PROGMEM = "";
