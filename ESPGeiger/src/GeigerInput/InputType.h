/*
  GeigerInput/InputType.h - the count source class for this GEIGER_TYPE

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
#ifndef GEIGERINPUTTYPE_H
#define GEIGERINPUTTYPE_H

#include "GeigerInput.h"

// The classes are final, so the tick calls them directly
#if GEIGER_TYPE == GEIGER_TYPE_PULSE
#include "GeigerInput/Type/Pulse.h"
using InputType = GeigerPulse;
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
#include "GeigerInput/Type/Serial.h"
using InputType = GeigerSerial;
#elif GEIGER_TYPE == GEIGER_TYPE_UDPRX
#include "GeigerInput/Type/UdpRx.h"
using InputType = GeigerUdpRx;
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
#include "GeigerInput/Type/Test.h"
using InputType = GeigerTest;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
#include "GeigerInput/Type/TestPulse.h"
using InputType = GeigerTestPulse;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
#include "GeigerInput/Type/TestSerial.h"
using InputType = GeigerTestSerial;
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSEINT
#include "GeigerInput/Type/TestPulseInt.h"
using InputType = GeigerTestPulseInt;
#else
#error "GEIGER_TYPE: pulse, serial, udprx, test, testpulse, testserial or testpulseint"
#endif

// flags that only mean something on some types
#if defined(GEIGER_COUNT_TXPULSE) && !GEIGER_IS_TEST(GEIGER_TYPE)
#error "GEIGER_COUNT_TXPULSE needs a test build"
#endif
#if (defined(NO_PCNT_ALIAS) || defined(NO_PCNT_ENV)) && !defined(PCNT_SELECTABLE)
#error "NO_PCNT_ALIAS / NO_PCNT_ENV need an esp32 pulse build with PCNT"
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && (GEIGER_SERIALTYPE < GEIGER_STYPE_GC10 || GEIGER_SERIALTYPE > GEIGER_STYPE_TEMPLATE)
#error "GEIGER_SERIALTYPE: gc10, gc10nx, mightyohm, espgeiger or template"
#endif

#endif
