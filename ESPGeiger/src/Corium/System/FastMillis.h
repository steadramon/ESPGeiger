#ifndef UTIL_FASTMILLIS_H
#define UTIL_FASTMILLIS_H

#include <stdint.h>

// Wall-clock-accurate ms counter advanced from msTickerCB. Avoids the
// ~150-cycle millis() divide for hot-path callers. Not for sub-ms work
// (still use micros()).

extern volatile uint32_t _fast_ms_counter;

static inline uint32_t fast_millis() { return _fast_ms_counter; }

#endif
