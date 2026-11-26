/*
  EGTcpLock.h - short lock between the stack's thread and the loop.

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
#ifndef EGTCPLOCK_H
#define EGTCPLOCK_H

// Never held across a call out. EGTcpHold takes it for a scope

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
// A mutex, a critical section would mask irqs
class EGTcpLock {
  public:
    EGTcpLock() { _m = xSemaphoreCreateMutex(); }
    ~EGTcpLock() { if (_m) vSemaphoreDelete(_m); }
    void lock()   { xSemaphoreTake(_m, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(_m); }
  private:
    SemaphoreHandle_t _m;
};
#elif !defined(ESP8266)
#include <mutex>
class EGTcpLock {
  public:
    void lock()   { _m.lock(); }
    void unlock() { _m.unlock(); }
  private:
    std::mutex _m;
};
#endif
// esp8266: one context, lwIP's callbacks and loop() take turns, no lock

#endif
