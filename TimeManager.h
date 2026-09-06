#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <atomic>
#include "config.h"

// =====================================================================
// TimeManager — Etapa 4: NTP + DS3231.
//
// Filosofia de "mínimo uso de rede": o DS3231 semeia o relógio de sistema
// do ESP32 uma vez no boot (settimeofday), e dali em diante o próprio
// ESP32 conta o tempo sozinho (time()/localtime_r() em memória, sem I2C
// nem rede). Uma task no core 0 sincroniza via NTP raramente (a cada
// TimeCfg::NTP_SYNC_INTERVAL_MS) só pra corrigir deriva do cristal, e
// grava o resultado de volta no DS3231 — assim, mesmo sem WiFi por dias,
// o relógio segue correto (RTC) e não regride se a rede cair.
//
// Sem mutex: o Wire/I2C do RTC só é tocado em dois momentos que nunca se
// sobrepõem — uma vez em begin() (core 1, antes de qualquer task existir)
// e depois só dentro da própria ntpSyncTask (core 0). getDisplay*String()
// só lê o relógio de sistema (time()/localtime_r), que é seguro entre
// tasks/cores sem lock adicional.
// =====================================================================
class TimeManager {
public:
    void begin();

    bool isTimeValid() const;   // true assim que há alguma hora confiável (RTC ou NTP)
    bool isRTCPresent() const;

    // true se o DS3231 relatou ter perdido energia (lostPower()/OSF) na
    // última vez que checamos (só no boot — o DS3231 não expõe tensão de
    // bateria contínua, só essa flag de "parou de contar em algum momento
    // desde a última vez que a hora foi ajustada"). Indica bateria CR2032
    // fraca ou ausente.
    bool isBatteryLow() const;

    String getDisplayTimeString(); // "13:45"
    String getDisplayDateString(); // "SEX, 04 SET"
    uint8_t second() const;        // segundo atual (0-59) — usado pra piscar coisas na tela

private:
    static void ntpSyncTask(void* param);
    bool syncFromNTP(); // bloqueante (até NTP_TIMEOUT_MS), só chamado dentro da ntpSyncTask
    void seedFromRTC(); // settimeofday() a partir do DS3231, só chamado em begin()

    RTC_DS3231 _rtc;
    bool _rtcPresent = false;
    bool _batteryLow = false;
    std::atomic<bool> _timeValid{false};

    TaskHandle_t _ntpTaskHandle = nullptr;
};

extern TimeManager RtcClock;
