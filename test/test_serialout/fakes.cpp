/*
  fakes.cpp - what SerialOut.cpp calls outside its own file.

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

#include "sout_fakes.h"

#include <EGHttpServer.h>
#include "GeigerInput/SerialFormat.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"

FakeSerial Serial = {};
Counter gcounter = {};

const char EG_K_INTERVAL[] = "interval";
const char EG_K_TPL[]      = "tpl";

size_t SerialFormat::fmt_mightyohm(char*, size_t) { return 0; }
size_t SerialFormat::fmt_user_template(char*, size_t) { return 0; }

void Log::setSerialLogLevel(bool) {}

bool EGModuleRegistry::add(EGModule*) { return true; }
bool EGModuleRegistry::set_loop_interval(EGModule*, int32_t) { return true; }
bool EGModuleRegistry::sleep_until(EGModule*, unsigned long, unsigned long) { return true; }

uint32_t EGPrefs::getUInt(const char*, const char*) { return 0; }
bool EGPrefs::putUInt(const char*, const char*, uint32_t) { return true; }
bool EGPrefs::commit(bool) { return true; }

// link only
void EGHttpResponse::send(uint16_t, const __FlashStringHelper*, const __FlashStringHelper*) {}
bool EGHttpServer::on(const __FlashStringHelper*, EGHttpRequest::Method, Handler, void*) { return true; }
