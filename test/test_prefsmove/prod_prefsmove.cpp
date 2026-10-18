/*
  prod_prefsmove.cpp - EGPrefs and its storage for the moved-keys suite.

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
#include "Corium/Prefs/EGPrefs.cpp"
#include "Corium/Prefs/EGPrefsStorage.cpp"
#include "Corium/System/Fs.cpp"
#include "Corium/System/PinSafety.cpp"
#include "Corium/Util/StringUtil.cpp"
#include "../../lib/EGText/src/EGEscape.cpp"
