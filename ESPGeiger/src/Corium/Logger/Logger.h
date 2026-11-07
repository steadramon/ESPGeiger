/*
  Logger.h - Log class

  Copyright (C) 2022 @steadramon

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

#ifndef CORIUM_LOGGER_H
#define CORIUM_LOGGER_H

#include "Arduino.h"
#include "App/AppConfig.h"

// web log buffer in bytes, oldest lines drop out
#ifndef MAX_LOG_SIZE
#define MAX_LOG_SIZE 4000
#endif
// a number: 0 console, 1 error, 2 info, 3 debug. below it info/debug compile out
#ifndef LOG_LEVEL
#define LOG_LEVEL    1
#endif

namespace corium {
class Log {
public:
  enum LoggingLevels {LOG_LEVEL_CONSOLE, LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};
  // fmt in flash or SRAM
  static void console(const char* fmt, ...);
  static void error(const char* fmt, ...);
#if LOG_LEVEL >= 2
  static void info(const char* fmt, ...);
#else
  template <class... A> static void info(const char*, A...) {}
#endif
#if LOG_LEVEL >= 3
  static void debug(const char* fmt, ...);
#else
  template <class... A> static void debug(const char*, A...) {}
#endif
  static void banner(const char* fmt, ...);  // raw serial, no timestamp
  // line idx ending '\1', at most cap-1 bytes; 0 if gone
  static size_t copyLog(uint32_t idx, char* out, size_t cap);
  // lines idx..end-1 ending '\n', whole lines in one pass, idx moves on; idx 0 = all, over 255 = none
  static size_t copySince(uint32_t* idx, uint8_t end, char* out, size_t cap);
  static char getLogIdx();   // idx the next line gets, 1..255
  static void setSerialOn(bool on);
  static bool serialOn() { return _serial; }

private:
  static void vlog(LoggingLevels level, bool timed, const char* fmt, va_list arg);
  static void AddLog(LoggingLevels level, const char* line, bool timed = true);
  static size_t strchrspn(const char *str1, int character);
  static char _log[MAX_LOG_SIZE];
  static char _idx;
  static LoggingLevels _level;
  static bool _serial;
};
}  // namespace corium

#endif
