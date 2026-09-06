#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <time.h>

class TimeManager {
public:
    TimeManager();

    void begin();
    void update();                  // read RTC / refresh current time
    bool syncFromNTP();            // set system time from NTP and update RTC
    bool setFromRTC();             // if RTC exists, prefer RTC as source
    bool isRTCValid() const;

    String getTimeString();
    String getDateString();
    String getDisplayTimeString();
    String getDisplayDateString();

    uint8_t hour() const;
    uint8_t minute() const;
    uint8_t second() const;
    uint8_t day() const;
    uint8_t month() const;
    uint16_t year() const;

private:
    RTC_DS3231 rtc_;
    bool rtcReady_;
    bool useRTC_;

    void updateInternalClockFromRTC();
};

#endif