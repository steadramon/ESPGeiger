#ifndef UTIL_FASTMILLIS_H
#define UTIL_FASTMILLIS_H

#include <stdint.h>

// 1 kHz Ticker-driven counter, incremented in msTickerCB (ESPGeiger.cpp).
// Avoids newlib millis()'s ~150-cycle system_get_time()/1000 cost for
// callers that don't need sub-ms precision. May drift by a few ms
// during sustained no-yield work; not safe for sub-ms budgets.

extern volatile uint32_t _fast_ms_counter;

static inline uint32_t fast_millis() { return _fast_ms_counter; }

#endif
