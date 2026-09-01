/*
  Logger.cpp - Log class

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

#include "Logger.h"
#include "../Net/Wifi.h"
#include "../System/Clock.h"
#include "../System/Uptime.h"

char Log::logIdx = 1;
Log::LoggingLevels Log::logLevel = LOG_LEVEL;
char Log::log[MAX_LOG_SIZE] = "";
bool Log::serialLog = true;

// esp32: mqtt, audio and the loop all log
#ifdef ESP32
#include <freertos/semphr.h>
static SemaphoreHandle_t s_lock = nullptr;   // first log is in setup
#define LOG_LOCK()   do { if (!s_lock) s_lock = xSemaphoreCreateMutex(); \
                          xSemaphoreTake(s_lock, portMAX_DELAY); } while (0)
#define LOG_UNLOCK() xSemaphoreGive(s_lock)
#else
#define LOG_LOCK()
#define LOG_UNLOCK()
#endif

static char s_log_buf[256];

void Log::vlog(LoggingLevels level, bool withTimestamp, const char* formatP, va_list arg)
{
  if (level > logLevel) return;
  LOG_LOCK();
  vsnprintf_P(s_log_buf, sizeof(s_log_buf), formatP, arg);
  AddLog(level, s_log_buf, withTimestamp);
  LOG_UNLOCK();
}

void Log::console(const char* formatP, ...)
{
  va_list arg;
  va_start(arg, formatP);
  vlog(LOG_LEVEL_CONSOLE, true, formatP, arg);
  va_end(arg);
}

void Log::error(const char* formatP, ...)
{
  va_list arg;
  va_start(arg, formatP);
  vlog(LOG_LEVEL_ERROR, true, formatP, arg);
  va_end(arg);
}

void Log::info(const char* formatP, ...)
{
  va_list arg;
  va_start(arg, formatP);
  vlog(LOG_LEVEL_INFO, true, formatP, arg);
  va_end(arg);
}

void Log::debug(const char* formatP, ...)
{
  va_list arg;
  va_start(arg, formatP);
  vlog(LOG_LEVEL_DEBUG, true, formatP, arg);
  va_end(arg);
}

void Log::banner(const char* formatP, ...)
{
  va_list arg;
  va_start(arg, formatP);
  vlog(LOG_LEVEL_CONSOLE, false, formatP, arg);
  va_end(arg);
}

// Based on arendst/Tasmota addLog (support.ino)
void Log::AddLog(Log::LoggingLevels level, const char* logData, bool withTimestamp)
{
  char timeStr[10] = "";
#ifndef DISABLE_LOG_TS
  if (withTimestamp) {
    struct tm t;
    if (Clock::localTm(&t)) {
      snprintf_P (timeStr, sizeof (timeStr), PSTR("%02d:%02d:%02d "), t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      Uptime::Split u = Uptime::split((uint32_t)Uptime::seconds());
      snprintf_P (timeStr, sizeof (timeStr), PSTR("%02u:%02u:%02u "), u.h, u.m, u.s);
    }
  }
#endif
  if (serialLog) {
    if (timeStr[0]) Serial.print(timeStr);
    Serial.println(logData);
  }

  if (Wifi::disabled) return;

#ifndef DISABLE_LOG_TS
  // * = uptime, not clock
  if (timeStr[0] && !Clock::synced) {
    size_t tsLen = strlen(timeStr);
    if (tsLen > 0) timeStr[tsLen-1] = '*';
  }
#endif


  // Delimited, zero-terminated buffer of log lines.
  // Each entry has this format: [index][log data]['\1']
  while (logIdx == log[0] ||  // If log already holds the next index, remove it
          strlen(log) + strlen(logData) + 13 > MAX_LOG_SIZE)  // 13 = idx + time + '\1' + '\0'
  {
    char* it = log;
    it++;                                // Skip web_log_index
    it += strchrspn(it, '\1');           // Skip log line
    it++;                                // Skip delimiting "\1"
    memmove(log, it, MAX_LOG_SIZE -(it-log));  // Move buffer forward to remove oldest log line
  }

  size_t logLen = strlen(log);
  snprintf_P(log + logLen, sizeof(log) - logLen, PSTR("%c%s%s\1"), logIdx++, timeStr, logData);

  logIdx &= 0xFF;
  if (!logIdx)
    logIdx++;       // Index 0 is not allowed as it is the end of char string*/
}

size_t Log::copyLog(uint32_t idx, char* out, size_t cap)
{
  if (!idx || cap < 2) return 0;
  size_t n = 0;
  LOG_LOCK();
  const char* it  = log;
  const char* end = log + MAX_LOG_SIZE;
  while (it < end && *it != '\0') {
    uint32_t cur_idx = (uint8_t)*it;
    it++;
    const char* delim = (const char*)memchr(it, '\1', (size_t)(end - it));
    if (!delim) break;
    size_t span = (size_t)(delim - it) + 1;
    if (cur_idx == idx) {
      n = span < cap - 1 ? span : cap - 1;
      memcpy(out, it, n);
      break;
    }
    it += span;
  }
  LOG_UNLOCK();
  return n;
}

// Get span until single character in string
size_t Log::strchrspn(const char *str1, int character)
{
  const char *end = strchr(str1, character);
  return end ? (size_t)(end - str1) : 0;
}

char Log::getLogIdx()
{
  return logIdx;
}

void Log::setSerialLogLevel(bool level)
{
  serialLog = level;
}
