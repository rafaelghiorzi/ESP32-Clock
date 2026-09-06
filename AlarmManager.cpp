#include "AlarmManager.h"
#include "TimeManager.h"
#include "SoundManager.h"
#include "ConnManager.h"
#include "NetLog.h"
#define Serial NetSerial // espelha os logs deste arquivo também via telnet — ver NetLog.h

AlarmManager Alarms;

void AlarmManager::begin() {
    _mutex = xSemaphoreCreateMutex();
    loadFromNVS();
    Serial.printf("[Alarm] %u slots carregados da memória (NVS)\n", MAX_ALARMS);
    for (uint8_t i = 0; i < MAX_ALARMS; i++) {
        if (_alarms[i].enabled) {
            Serial.printf("[Alarm]   slot %u: %02d:%02d dias=0x%02X repete=%d \"%s\"\n",
                          i, _alarms[i].hour, _alarms[i].minute, _alarms[i].daysMask,
                          _alarms[i].repeat, _alarms[i].label);
        }
    }
}

void AlarmManager::loadFromNVS() {
    _prefs.begin("alarms", false);
    for (uint8_t i = 0; i < MAX_ALARMS; i++) {
        char key[4];
        snprintf(key, sizeof(key), "a%u", i);
        Alarm a;
        size_t n = _prefs.getBytes(key, &a, sizeof(Alarm));
        _alarms[i] = (n == sizeof(Alarm)) ? a : Alarm{};
    }
}

void AlarmManager::saveToNVS(uint8_t index) {
    char key[4];
    snprintf(key, sizeof(key), "a%u", index);
    _prefs.putBytes(key, &_alarms[index], sizeof(Alarm));
}

Alarm AlarmManager::get(uint8_t index) const {
    if (index >= MAX_ALARMS) return Alarm{};
    xSemaphoreTake(_mutex, portMAX_DELAY);
    Alarm copy = _alarms[index];
    xSemaphoreGive(_mutex);
    return copy;
}

bool AlarmManager::set(uint8_t index, const Alarm& alarm) {
    if (index >= MAX_ALARMS) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _alarms[index] = alarm;
    xSemaphoreGive(_mutex);

    saveToNVS(index);
    _lastChangeMs.store(millis());
    Serial.printf("[Alarm] slot %u atualizado: %02d:%02d dias=0x%02X repete=%d ativo=%d \"%s\"\n",
                  index, alarm.hour, alarm.minute, alarm.daysMask, alarm.repeat,
                  alarm.enabled, alarm.label);
    return true;
}

uint32_t AlarmManager::getLastChangeMs() const {
    return _lastChangeMs.load();
}

// =====================================================================
// Disparo agendado (checagem 1x/minuto) + máquina de estados de toque
// =====================================================================

void AlarmManager::update() {
    // --- parte 1: checa a grade de horários, no máximo 1x por minuto ---
    if (RtcClock.isTimeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);

        int totalMinute = t.tm_hour * 60 + t.tm_min;
        if (totalMinute != _lastCheckedMinute) {
            _lastCheckedMinute = totalMinute;

            uint8_t todayBit = 1 << t.tm_wday; // tm_wday: 0=domingo..6=sábado
            int8_t toRing = -1;

            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (uint8_t i = 0; i < MAX_ALARMS; i++) {
                Alarm& a = _alarms[i];
                if (!a.enabled || a.hour != t.tm_hour || a.minute != t.tm_min) continue;
                if (!(a.daysMask & todayBit)) continue;

                Serial.printf("[Alarm] disparando slot %u (%02d:%02d) \"%s\"\n", i, a.hour, a.minute, a.label);

                if (!a.repeat) {
                    a.enabled = false; // timer/uso único: desliga sozinho após disparar
                    saveToNVS(i);
                }
                if (toRing < 0) toRing = i; // só toca um por vez (ver limitação no .h)
            }
            xSemaphoreGive(_mutex);

            if (toRing >= 0) {
                bool cycleActive = _ringing.load() || _snoozing;
                if (!cycleActive) {
                    _snoozeUsed = false;
                    _ringStartMs = millis();
                    startRinging((uint8_t)toRing);
                } else {
                    // Conflito: outro alarme já está tocando ou em soneca
                    // esperando pra tocar de novo, e esse aqui bateu no
                    // mesmo instante (ex.: dois alarmes 5min separados, um
                    // deles em soneca justo na hora do outro disparar). Em
                    // vez de um "vencer" silenciosamente e o outro sumir,
                    // os dois se cancelam — nenhum toca.
                    int8_t otherIndex = _ringingIndex.load();
                    Serial.printf("[Alarm] conflito: slot %u coincidiu com o ciclo ativo do slot %d -> ambos cancelados\n",
                                  toRing, otherIndex);

                    if (_ringing.load()) {
                        _ringing.store(false);
                        waitForRingTaskToStop();
                    }
                    _snoozing = false;
                    _ringingIndex.store(-1);
                    setMessage("Alarmes coincidiram, cancelados", AlarmCfg::SNOOZE_MESSAGE_MS);
                }
            }
        }
    }

    // --- parte 2: avança a máquina de estados de toque/soneca, todo tick ---
    uint32_t now = millis();

    if (_ringing.load()) {
        if (now - _ringStartMs >= AlarmCfg::RING_DURATION_MS) {
            Serial.println("[Alarm] 3 minutos tocando sem resposta");
            if (!_snoozeUsed) {
                enterSnooze();
            } else {
                dismissRinging();
            }
        }
        return;
    }

    if (_snoozing) {
        if (now - _snoozeStartMs >= _snoozeDurationMs) {
            Serial.println("[Alarm] soneca acabou, tocando de novo");
            _snoozing = false;
            _ringStartMs = now;
            startRinging((uint8_t)_ringingIndex.load());
        }
    }
}

AlarmManager::NextAlarmInfo AlarmManager::getNextAlarm() const {
    NextAlarmInfo result;
    if (!RtcClock.isTimeValid()) return result;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int nowMinutes = t.tm_hour * 60 + t.tm_min;
    int nowWday = t.tm_wday; // 0=domingo..6=sábado

    int bestDelta = 100000; // maior que qualquer delta possível (máx. real: 7*1440 = 10080)

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < MAX_ALARMS; i++) {
        const Alarm& a = _alarms[i];
        if (!a.enabled || a.daysMask == 0) continue;

        int alarmMinutes = a.hour * 60 + a.minute;

        // Procura a ocorrência mais próxima desse alarme, olhando os
        // próximos 7 dias (hoje incluso, se ainda não passou).
        for (int dayOffset = 0; dayOffset < 7; dayOffset++) {
            int wday = (nowWday + dayOffset) % 7;
            if (!(a.daysMask & (1 << wday))) continue;
            if (dayOffset == 0 && alarmMinutes <= nowMinutes) continue; // já passou/tocando agora

            int delta = dayOffset * 1440 + alarmMinutes - nowMinutes;
            if (delta < bestDelta) {
                bestDelta = delta;
                result.any = true;
                result.hour = a.hour;
                result.minute = a.minute;
            }
            break; // achou a ocorrência mais próxima desse alarme, próximo alarme
        }
    }
    xSemaphoreGive(_mutex);

    return result;
}

// =====================================================================
// Toque / soneca / desligar
// =====================================================================

void AlarmManager::startRinging(uint8_t index) {
    if (_ringing.load()) {
        Serial.println("[Alarm] outro alarme já está tocando, ignorando esse disparo");
        return;
    }

    bool wakeLights = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    wakeLights = _alarms[index].wakeLights;
    xSemaphoreGive(_mutex);
    if (wakeLights) Conn.requestWakeLights();

    _ringingIndex.store((int8_t)index);
    _ringTaskRunning.store(true);
    _ringing.store(true);
    xTaskCreatePinnedToCore(ringTask, "alarm_ring", 4096, this, 1, &_ringTaskHandle, 0);
}

void AlarmManager::waitForRingTaskToStop() {
    // A ringTask checa _ringing entre cada bipe/pausa (granularidade de
    // ~150ms), então esperar até 500ms é sobra suficiente pra garantir que
    // ela realmente parou de escrever no I2S antes da gente tocar o som de
    // confirmação por cima — evita as duas tasks escrevendo no mesmo
    // periférico ao mesmo tempo (áudio embaralhado).
    uint32_t start = millis();
    while (_ringTaskRunning.load() && (millis() - start) < 500) {
        delay(5);
    }
}

void AlarmManager::enterSnooze() {
    int8_t idx = _ringingIndex.load();
    uint8_t snoozeMinutes = 5; // fallback se por algum motivo o índice não for válido
    if (idx >= 0 && idx < (int8_t)MAX_ALARMS) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        snoozeMinutes = _alarms[idx].snoozeMinutes;
        xSemaphoreGive(_mutex);
    }
    if (snoozeMinutes == 0) snoozeMinutes = 5; // proteção extra (nunca soneca de 0min)

    Serial.printf("[Alarm] soneca ativada, toca de novo em %u minuto(s)\n", snoozeMinutes);
    _ringing.store(false);
    waitForRingTaskToStop();

    _snoozing = true;
    _snoozeUsed = true;
    _snoozeStartMs = millis();
    _snoozeDurationMs = (uint32_t)snoozeMinutes * 60000UL;

    char msg[24];
    snprintf(msg, sizeof(msg), "Toque em %u minuto%s!", snoozeMinutes, snoozeMinutes == 1 ? "" : "s");
    setMessage(msg, AlarmCfg::SNOOZE_MESSAGE_MS);

    Sound.playSnoozeConfirm(); // "pi-pi-pi"
}

void AlarmManager::dismissRinging() {
    Serial.println("[Alarm] alarme desligado");
    _ringing.store(false);
    waitForRingTaskToStop();

    _snoozing = false;
    _ringingIndex.store(-1);

    Sound.playAlarmOff(); // "pi-po"
}

void AlarmManager::handleButton4() {
    if (!_ringing.load()) return;
    dismissRinging();
}

void AlarmManager::handleButton5() {
    if (!_ringing.load()) return;
    if (!_snoozeUsed) {
        enterSnooze();
    } else {
        dismissRinging();
    }
}

void AlarmManager::dismissActive() {
    if (_ringing.load()) dismissRinging();
}

bool AlarmManager::isRinging() const {
    return _ringing.load();
}

String AlarmManager::getRingingLabel() const {
    int8_t idx = _ringingIndex.load();
    if (idx < 0 || idx >= (int8_t)MAX_ALARMS) return String();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    String label = _alarms[idx].label;
    xSemaphoreGive(_mutex);
    return label;
}

void AlarmManager::setMessage(const char* text, uint32_t durationMs) {
    strncpy(_messageBuf, text, sizeof(_messageBuf) - 1);
    _messageBuf[sizeof(_messageBuf) - 1] = '\0';
    _messageUntilMs = millis() + durationMs;
}

bool AlarmManager::getMessage(String& out) const {
    if (millis() >= _messageUntilMs) return false;
    out = _messageBuf;
    return true;
}

void AlarmManager::ringTask(void* param) {
    auto* self = static_cast<AlarmManager*>(param);

    int8_t idx = self->_ringingIndex.load();
    AlarmSound soundType = AlarmSound::Sample;
    if (idx >= 0 && idx < (int8_t)MAX_ALARMS) {
        xSemaphoreTake(self->_mutex, portMAX_DELAY);
        soundType = self->_alarms[idx].sound;
        xSemaphoreGive(self->_mutex);
    }

    while (self->_ringing.load()) {
        if (soundType == AlarmSound::Buzzer) {
            Sound.playBuzzerTone(1500.0f, 150);
            if (!self->_ringing.load()) break;
            delay(120);
            if (!self->_ringing.load()) break;
            Sound.playBuzzerTone(1500.0f, 150);
            if (!self->_ringing.load()) break;
            delay(120);
            if (!self->_ringing.load()) break;
            Sound.playBuzzerTone(1500.0f, 150);
            if (!self->_ringing.load()) break;
            delay(600);
        } else {
            Sound.playPhantomCigar(); // ~8s
            if (!self->_ringing.load()) break;
            delay(300);
        }
    }

    self->_ringTaskRunning.store(false);
    self->_ringTaskHandle = nullptr;
    Serial.println("[Alarm] parou de tocar");
    vTaskDelete(nullptr);
}
