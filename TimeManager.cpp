#include "TimeManager.h"
#include "config.h"
#include <WiFi.h>

TimeManager::TimeManager()
    : rtcReady_(false), useRTC_(false) {
}

void TimeManager::begin() {
    // Os valores gravados no DS3231 sao tratados como horario local.
    setenv("TZ", "BRT3", 1);
    tzset();

    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);

    rtcReady_ = rtc_.begin();
    if (!rtcReady_) {
        Serial.println("[TimeManager] Erro: DS3231 nao encontrado");
        return;
    }

    useRTC_ = true; // preferir RTC como fonte de tempo

    if (rtc_.lostPower()) {
        Serial.println("[TimeManager] Aviso: DS3231 perdeu energia, utilizando tempo padrão NTP ou manual");
    }
}

void TimeManager::updateInternalClockFromRTC() {
    if (!rtcReady_) return;

    DateTime now = rtc_.now();
    struct tm timeinfo;
    timeinfo.tm_year = now.year() - 1900;
    timeinfo.tm_mon = now.month() - 1;
    timeinfo.tm_mday = now.day();
    timeinfo.tm_hour = now.hour();
    timeinfo.tm_min = now.minute();
    timeinfo.tm_sec = now.second();
    
    time_t epoch = mktime(&timeinfo);
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
}

bool TimeManager::syncFromNTP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TimeManager] Erro: Sem conexao com o Wi-Fi");
        return false;
    }

    configTzTime("BRT3", "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[TimeManager] Erro: Falha ao obter hora local");
        return false;
    }

    if (rtcReady_) {
        DateTime dt(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        );
        rtc_.adjust(dt);
        Serial.println("[TimeManager] DS3231 atualizado com sucesso a partir do NTP");
    }

    return true;
}

bool TimeManager::setFromRTC() {
    if (!rtcReady_) return false;

    updateInternalClockFromRTC();
    return true;
}

bool TimeManager::isRTCValid() const {
    return rtcReady_;
}

void TimeManager::update() {
    // O relogio interno do ESP32 avanca sozinho. Reescrever o horario a cada
    // segundo a partir do RTC desfaz uma sincronizacao NTP recente.
}

String TimeManager::getTimeString() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
}

String TimeManager::getDateString() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[20];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return String(buf);
}

String TimeManager::getDisplayTimeString() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

String TimeManager::getDisplayDateString() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    const char* days[] = {
        "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"
    };

    const char* months[] = {
        "Jan", "Fev", "Mar", "Abr", "Mai", "Jun",
        "Jul", "Ago", "Set", "Out", "Nov", "Dez"
    };

    char buf[32];
    snprintf(buf, sizeof(buf), "%s, %d de %s", days[t.tm_wday], t.tm_mday, months[t.tm_mon]);

    return String(buf);
}

uint8_t TimeManager::hour() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour;
}

uint8_t TimeManager::minute() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_min;
}

uint8_t TimeManager::second() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_sec;
}

uint8_t TimeManager::day() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_mday;
}

uint8_t TimeManager::month() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_mon + 1;
}

uint16_t TimeManager::year() const {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year + 1900;
}

