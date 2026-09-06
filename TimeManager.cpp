#include "TimeManager.h"
#include "ConnManager.h"
#include "NetLog.h"
#define Serial NetSerial // espelha os logs deste arquivo também via telnet — ver NetLog.h

TimeManager RtcClock;

void TimeManager::seedFromRTC() {
    if (!_rtcPresent) return;

    DateTime now = _rtc.now();
    struct tm timeinfo = {};
    timeinfo.tm_year = now.year() - 1900;
    timeinfo.tm_mon  = now.month() - 1;
    timeinfo.tm_mday = now.day();
    timeinfo.tm_hour = now.hour();
    timeinfo.tm_min  = now.minute();
    timeinfo.tm_sec  = now.second();

    time_t epoch = mktime(&timeinfo);
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);

    _timeValid.store(true);
    Serial.printf("[Clock] hora inicial vinda do DS3231: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
}

void TimeManager::begin() {
    setenv("TZ", TimeCfg::TIMEZONE, 1);
    tzset();

    Wire.begin(Pins::RTC::SDA, Pins::RTC::SCL);
    _rtcPresent = _rtc.begin();

    if (!_rtcPresent) {
        Serial.println("[Clock] DS3231 nao encontrado no barramento I2C — relogio so fica valido apos o primeiro NTP");
    } else {
        _batteryLow = _rtc.lostPower();
        if (_batteryLow) {
            Serial.println("[Clock] aviso: DS3231 perdeu energia (bateria fraca/ausente?) — hora pode estar errada ate o proximo NTP");
        }
        seedFromRTC();
    }

    xTaskCreatePinnedToCore(ntpSyncTask, "ntp_sync", 4096, this, 1, &_ntpTaskHandle, 0);
}

bool TimeManager::syncFromNTP() {
    configTzTime(TimeCfg::TIMEZONE, TimeCfg::NTP_SERVER1, TimeCfg::NTP_SERVER2);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, TimeCfg::NTP_TIMEOUT_MS)) {
        Serial.println("[Clock] NTP: sem resposta");
        return false;
    }

    _timeValid.store(true);
    Serial.printf("[Clock] NTP OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    if (_rtcPresent) {
        DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        _rtc.adjust(dt);
        Serial.println("[Clock] DS3231 atualizado a partir do NTP");
    }

    return true;
}

void TimeManager::ntpSyncTask(void* param) {
    auto* self = static_cast<TimeManager*>(param);

    for (;;) {
        if (Conn.isConnected()) {
            bool ok = self->syncFromNTP();
            vTaskDelay(pdMS_TO_TICKS(ok ? TimeCfg::NTP_SYNC_INTERVAL_MS : TimeCfg::NTP_RETRY_INTERVAL_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(TimeCfg::NTP_RETRY_INTERVAL_MS));
        }
    }
}

bool TimeManager::isTimeValid() const {
    return _timeValid.load();
}

bool TimeManager::isRTCPresent() const {
    return _rtcPresent;
}

bool TimeManager::isBatteryLow() const {
    return _batteryLow;
}

void TimeManager::getDisplayTimeString(char* buf, size_t bufSize) const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, bufSize, "%02d:%02d", t.tm_hour, t.tm_min);
}

uint8_t TimeManager::second() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return (uint8_t)t.tm_sec;
}

void TimeManager::getDisplayDateString(char* buf, size_t bufSize) const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    static const char* days[]   = { "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB" };
    static const char* months[] = { "JAN", "FEV", "MAR", "ABR", "MAI", "JUN",
                                     "JUL", "AGO", "SET", "OUT", "NOV", "DEZ" };

    snprintf(buf, bufSize, "%s, %02d %s", days[t.tm_wday], t.tm_mday, months[t.tm_mon]);
}
