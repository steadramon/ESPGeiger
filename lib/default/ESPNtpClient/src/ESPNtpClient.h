/**
  * @file ESPNtpClient.h
  * @version 0.2.6
  * @date 29/12/2021
  * @author German Martin
  * @brief Library to get system sync from a NTP server with microseconds accuracy in ESP8266 and ESP32
  */

#ifndef _EspNtpClient_h
#define _EspNtpClient_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <functional>
//using namespace std;
//using namespace placeholders;

extern "C" {
#include "sys/time.h"
#include "time.h"
}

/* Useful Constants */
#ifndef SECS_PER_MIN
constexpr auto SECS_PER_MIN = ((time_t)(60UL));
#endif
#ifndef SECS_PER_HOUR
constexpr auto SECS_PER_HOUR = ((time_t)(3600UL));
#endif
#ifndef SECS_PER_DAY
constexpr auto SECS_PER_DAY = ((time_t)(SECS_PER_HOUR * 24UL));
#endif
#ifndef DAYS_PER_WEEK
constexpr auto DAYS_PER_WEEK = ((time_t)(7UL));
#endif
#ifndef SECS_PER_WEEK
constexpr auto SECS_PER_WEEK = ((time_t)(SECS_PER_DAY * DAYS_PER_WEEK));
#endif
#ifndef SECS_PER_YEAR
constexpr auto SECS_PER_YEAR = ((time_t)(SECS_PER_DAY * 365UL));
#endif
#ifndef SECS_YR_2000
constexpr auto SECS_YR_2000 = ((time_t)(946684800UL)); ///< @brief The time at the start of y2k
#endif

static char strBuffer[35]; ///< @brief Temporary buffer for time and date strings

/**
  * @brief NTPClient class
  */
class NTPClient {
protected:
    unsigned long uptime = 0;       ///< @brief Time since boot
public:

    /**
    * @brief Gets uptime in UNIX format, time since MCU was last rebooted
    * @return Uptime
    */
    time_t getUptime () {
        uptime = uptime + ((::millis ()/1000) - uptime);
        return uptime;
    }

    /**
     * @brief Gets milliseconds since 1-Jan-1970 00:00 UTC
     * @return Milliseconds since 1-Jan-1970 00:00 UTC
     */
    int64_t millis () {
        timeval currentTime;
        gettimeofday (&currentTime, NULL);
        int64_t milliseconds = (int64_t)currentTime.tv_sec * 1000L + (int64_t)currentTime.tv_usec / 1000L;
        //Serial.printf ("timeval: %ld.%ld millis %lld\n", currentTime.tv_sec, currentTime.tv_usec, milliseconds);
        return milliseconds;
    }
};

extern NTPClient NTP; ///< @brief Singleton NTPClient instance

#endif // _NtpClientLib_h