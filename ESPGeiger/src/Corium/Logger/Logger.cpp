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

#include <stdarg.h>
#include "Logger.h"
#include "Corium/System/Clock.h"
#include "Corium/System/Uptime.h"

char corium::Log::_idx = 1;
corium::Log::LoggingLevels corium::Log::_level = (corium::Log::LoggingLevels)LOG_LEVEL;
char corium::Log::_log[MAX_LOG_SIZE] = "";
bool corium::Log::_serial = true;

// esp32: other tasks log too
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

// one line, longer is cut; used under LOG_LOCK
static char s_line[256];

void corium::Log::vlog(LoggingLevels level, bool timed, const char* fmt, va_list arg)
{
  if (level > _level) return;
  LOG_LOCK();
  vsnprintf_P(s_line, sizeof(s_line), fmt, arg);
  // peer bytes (an SSID) can't split the ring or fake a line
  for (char* c = s_line; *c; c++) {
    if ((uint8_t)*c < ' ' || *c == 0x7f) *c = '?';  // DEL
  }
  AddLog(level, s_line, timed);
  LOG_UNLOCK();
}

void corium::Log::console(const char* fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  vlog(LOG_LEVEL_CONSOLE, true, fmt, arg);
  va_end(arg);
}

void corium::Log::error(const char* fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  vlog(LOG_LEVEL_ERROR, true, fmt, arg);
  va_end(arg);
}

#if LOG_LEVEL >= 2
void corium::Log::info(const char* fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  vlog(LOG_LEVEL_INFO, true, fmt, arg);
  va_end(arg);
}
#endif

#if LOG_LEVEL >= 3
void corium::Log::debug(const char* fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  vlog(LOG_LEVEL_DEBUG, true, fmt, arg);
  va_end(arg);
}
#endif

void corium::Log::banner(const char* fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  vlog(LOG_LEVEL_CONSOLE, false, fmt, arg);
  va_end(arg);
}

static uint8_t next_idx(uint8_t i) { return i == 255 ? 1 : i + 1; }

// Based on arendst/Tasmota addLog (support.ino)
void corium::Log::AddLog(corium::Log::LoggingLevels level, const char* line, bool timed)
{
  char stamp[13] = {};
#ifndef DISABLE_LOG_TS
  if (timed) {
    struct tm t;
    if (corium::Clock::localTm(&t)) {
      snprintf_P (stamp, sizeof (stamp), PSTR("%02d:%02d:%02d "), t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      // * = uptime, not clock
      corium::Uptime::Split u = corium::Uptime::split((uint32_t)corium::Uptime::seconds());
      snprintf_P (stamp, sizeof (stamp), PSTR("%02u:%02u:%02u*"), u.h, u.m, u.s);
    }
  }
#endif
  if (_serial) {
    if (stamp[0]) Serial.print(stamp);
    Serial.println(line);
  }

  // Delimited, zero-terminated buffer of log lines.
  // Each entry has this format: [index][log data]['\1']
  while (_idx == _log[0] ||  // If _log already holds the next index, remove it
          strlen(_log) + strlen(line) + 13 > MAX_LOG_SIZE)  // 13 = idx + time + '\1' + '\0'
  {
    char* it = _log;
    it++;                                // Skip web_log_index
    it += strchrspn(it, '\1');           // Skip log line
    it++;                                // Skip delimiting "\1"
    memmove(_log, it, MAX_LOG_SIZE -(it-_log));  // Move buffer forward to remove oldest log line
  }

  size_t len = strlen(_log);
  snprintf_P(_log + len, sizeof(_log) - len, PSTR("%c%s%s\1"), _idx, stamp, line);
  _idx = (char)next_idx((uint8_t)_idx);
}

// the next "[idx]line\1", span takes the \1; false = none left
static bool next_line(const char*& it, const char* stop, uint8_t& idx, const char*& line, size_t& span)
{
  if (it >= stop || *it == '\0') return false;
  idx = (uint8_t)*it++;
  const char* delim = (const char*)memchr(it, '\1', (size_t)(stop - it));
  if (!delim) return false;
  line = it;
  span = (size_t)(delim - it) + 1;
  it = delim + 1;
  return true;
}

size_t corium::Log::copyLog(uint32_t idx, char* out, size_t cap)
{
  if (!idx || cap < 2) return 0;
  size_t n = 0;
  LOG_LOCK();
  const char* it = _log;
  const char* line;
  uint8_t cur_idx;
  size_t span;
  while (next_line(it, _log + MAX_LOG_SIZE, cur_idx, line, span)) {
    if (cur_idx == idx) {
      n = span < cap - 1 ? span : cap - 1;
      memcpy(out, line, n);
      break;
    }
  }
  LOG_UNLOCK();
  return n;
}

// steps from a to b, indices 1..255
static int idx_dist(uint8_t a, uint8_t b) {
  int d = b - a;
  return d < 0 ? d + 255 : d;
}

size_t corium::Log::copySince(uint32_t* idx, uint8_t end, char* out, size_t cap)
{
  if (*idx > 255 || !end || cap < 2) return 0;
  uint8_t from = *idx ? (uint8_t)*idx : next_idx(end);
  int want = idx_dist(from, end);
  size_t n = 0;
  LOG_LOCK();
  const char* it = _log;
  const char* line;
  uint8_t cur_idx;
  size_t span;
  while (next_line(it, _log + MAX_LOG_SIZE, cur_idx, line, span)) {
    if (idx_dist(from, cur_idx) >= want) continue;
    if (n + span > cap - 1) {
      if (n) break;
      span = cap - 1;   // one long line, cut as copyLog
    }
    memcpy(out + n, line, span);
    n += span;
    out[n - 1] = '\n';
    *idx = next_idx(cur_idx);
  }
  LOG_UNLOCK();
  return n;
}

// Get span until single character in string
size_t corium::Log::strchrspn(const char *str1, int character)
{
  const char *end = strchr(str1, character);
  return end ? (size_t)(end - str1) : 0;
}

char corium::Log::getLogIdx()
{
  return _idx;
}

void corium::Log::setSerialOn(bool on)
{
  _serial = on;
}
