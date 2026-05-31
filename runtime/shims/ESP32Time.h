#pragma once
#include <time.h>
#include <stdint.h>
#include "WString.h"

// fbiego/ESP32Time API. Real implementation reads the RTC via
// `gettimeofday()` honouring an internal `offset_`. The sim mirrors that
// behaviour against the host clock, so `rtc.getDateTime()` etc. always
// return a sensible "now" without configuration — useful for getting
// receipts/log lines populated even without NTP.
class ESP32Time {
public:
    ESP32Time(unsigned long offset_seconds = 0) : offset_(offset_seconds) {}

    void setTime(unsigned long /*epoch*/, int /*ms*/ = 0) {}
    void setTime(int /*sc*/, int /*mn*/, int /*hr*/, int /*dy*/, int /*mt*/, int /*yr*/, int /*ms*/ = 0) {}
    void setTimeStruct(struct tm /*t*/) {}

    long      getLocalEpoch() { return (long)(time(nullptr) + offset_); }
    unsigned long getEpoch()  { return (unsigned long)(time(nullptr) + offset_); }

    String getDate(bool /*long_format*/ = false)        { return fmt_("%Y-%m-%d"); }
    String getTime(bool /*twentyFourHour*/ = true)      { return fmt_("%H:%M:%S"); }
    String getDateTime(bool /*long_format*/ = false)    { return fmt_("%Y-%m-%d %H:%M:%S"); }
    String getTimeDate(bool /*long_format*/ = false)    { return fmt_("%H:%M:%S %Y-%m-%d"); }

    int  getMillis()    { return 0; }
    int  getMicros()    { return 0; }
    int  getSecond()    { return now_tm_().tm_sec; }
    int  getMinute()    { return now_tm_().tm_min; }
    int  getHour(bool twentyFour = false) {
        int h = now_tm_().tm_hour;
        if (twentyFour) return h;
        h = h % 12; return h == 0 ? 12 : h;
    }
    int  getDay()       { return now_tm_().tm_mday; }
    int  getDayofWeek() { return now_tm_().tm_wday; }
    int  getDayofYear() { return now_tm_().tm_yday; }
    int  getMonth()     { return now_tm_().tm_mon; }     // 0-11 — matches ESP32Time
    int  getYear()      { return now_tm_().tm_year + 1900; }
    bool getAmPm()      { return now_tm_().tm_hour >= 12; }

    String getTimeZone() { return String("UTC"); }
    void   setTimeZone(const char* /*tz*/) {}

private:
    unsigned long offset_;
    struct tm now_tm_() {
        time_t t = time(nullptr) + (time_t)offset_;
        struct tm tmv; localtime_r(&t, &tmv); return tmv;
    }
    String fmt_(const char* spec) {
        struct tm tmv = now_tm_();
        char buf[40];
        strftime(buf, sizeof(buf), spec, &tmv);
        return String(buf);
    }
};
