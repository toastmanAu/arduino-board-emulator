#pragma once
// Paul Stoffregen's Time library API. The sketch includes this for symbol
// availability; on host we rely on <time.h>. Only the subset commonly
// touched is exposed — extend as needed.

#include <time.h>
#include <stdint.h>

using time_t_ = time_t;  // disambiguate when sketches define their own time_t

// tmElements_t — fields match the canonical TimeLib layout. Sketches that
// read individual fields get standard struct-tm equivalents.
typedef struct {
    uint8_t  Second;
    uint8_t  Minute;
    uint8_t  Hour;
    uint8_t  Wday;   // day of week (1 = Sunday)
    uint8_t  Day;
    uint8_t  Month;
    uint8_t  Year;   // offset from 1970
} tmElements_t;

inline int hour()    { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_hour; }
inline int minute()  { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_min;  }
inline int second()  { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_sec;  }
inline int day()     { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_mday; }
inline int month()   { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_mon + 1; }
inline int year()    { time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv); return tmv.tm_year + 1900; }
inline time_t now()  { return time(nullptr); }
inline void   setTime(time_t /*t*/) {}  // sim clock is host-driven; no-op.
