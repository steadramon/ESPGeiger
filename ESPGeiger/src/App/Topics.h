/*
  Topics.h - The app's topics, one list so none share a number.

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
#ifndef APP_TOPICS_H
#define APP_TOPICS_H

#include "Corium/Module/Registry.h"

// a module posts its own, anyone listens
namespace Topic {
  constexpr uint8_t ALARM    = CORIUM_TOPIC_APP;
  constexpr uint8_t COUNTS   = CORIUM_TOPIC_APP + 1;
  constexpr uint8_t POWER    = CORIUM_TOPIC_APP + 2;
  constexpr uint8_t HV_FAULT = CORIUM_TOPIC_APP + 3;
  constexpr uint8_t SD_ERROR = CORIUM_TOPIC_APP + 4;
  constexpr uint8_t PRESS    = CORIUM_TOPIC_APP + 5;
  constexpr uint8_t PING     = CORIUM_TOPIC_APP + 6;
}
static_assert(Topic::PING < CORIUM_MAX_TOPICS, "app topics past EG_MAX_TOPICS");

#endif
