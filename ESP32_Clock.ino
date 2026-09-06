#include <Arduino.h>
#include "config.h"
#include "DisplayManager.h"
#include "SoundManager.h"
#include "ConnManager.h"
#include "ButtonManager.h"
#include "TimeManager.h"
#include "AlarmManager.h"
#include "WebManager.h"
#include "AlarmSample.h" // "ding" sintetizado, prova de conceito do playSample() — ver BTN5
#include <esp_system.h>

// Nome legível pro motivo do último reset (esp_reset_reason()) — ajuda a
// diagnosticar problemas de campo (brownout, watchdog, painc, etc.) sem
// precisar reproduzir o problema com o monitor serial já aberto.
static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on (ligou na energia)";
        case ESP_RST_EXT:       return "reset externo (pino RESET/EN)";
        case ESP_RST_SW:        return "reset por software (ESP.restart())";
        case ESP_RST_PANIC:     return "panic (exceção/crash)";
        case ESP_RST_INT_WDT:   return "watchdog de interrupção";
        case ESP_RST_TASK_WDT:  return "task watchdog (uma task travou)";
        case ESP_RST_WDT:       return "outro watchdog";
        case ESP_RST_BROWNOUT:  return "brownout (queda de tensão)";
        case ESP_RST_SDIO:      return "reset via SDIO";
        default:                return "desconhecido";
    }
}

static uint8_t currentScene = 0;
static uint32_t lastWeatherLogMs = 0;
static uint32_t lastDisplayMs = 0;
constexpr uint32_t WEATHER_LOG_INTERVAL_MS = 30'000;
constexpr uint32_t DISPLAY_TICK_INTERVAL_MS = 1'000;

void setup() {
    Serial.begin(115200);
    delay(200); // pequena folga para o monitor serial (USB CDC) anexar

    Serial.println();
    Serial.println("=== ESP32_Clock — boot (Etapas 1-5 + alarmes) ===");
    Serial.printf("[System] motivo do reset anterior: %s\n", resetReasonName(esp_reset_reason()));

    Display.begin();   // sprite + ciclo de cores de bring-up, sem conteúdo dinâmico ainda
    Sound.begin();
    Sound.beepBoot();  // beep curto de confirmação, não a melodia de bancada

    Conn.begin();      // WiFi + fetch de clima (memória) + fila Yeelight, tudo no core 0
    RtcClock.begin();  // DS3231 semeia a hora agora; NTP corrige de vez em quando (core 0)
    Alarms.begin();    // carrega os 5 slots de alarme da NVS
    Web.begin();       // servidor HTTP pra configurar os alarmes pelo celular/PC
    Buttons.begin();
}

void loop() {
    Buttons.update();

    bool b1 = Buttons.button1Clicked();
    bool b2 = Buttons.button2Clicked();
    bool b3 = Buttons.button3Clicked();
    bool b4 = Buttons.button4Clicked();
    bool b5 = Buttons.button5Clicked();

    if (Alarms.isRinging()) {
        // BTN5 tem lógica própria (soneca na 1a vez, desliga na 2a).
        // BTN4 sempre desliga. B1/B2/B3 também só desligam aqui — não
        // queremos acidentalmente mexer nas luzes às 6h tentando calar
        // o alarme.
        if (b5) {
            Alarms.handleButton5();
        } else if (b1 || b2 || b3 || b4) {
            Alarms.handleButton4();
        }
    } else {
        if (b1) {
            Serial.println("[Button] BTN1 -> toggle luz bedside");
            Conn.requestYeelightToggle(Yeelight::BEDSIDE_IP);
            Sound.playClick();
        }
        if (b2) {
            Serial.println("[Button] BTN2 -> toggle luz teto");
            Conn.requestYeelightToggle(Yeelight::CEILING_IP);
            Sound.playClick();
        }
        if (b3) {
            currentScene = (currentScene % 4) + 1;
            Serial.printf("[Button] BTN3 -> cena de iluminação %u\n", currentScene);
            Conn.requestLightingScene(currentScene);
            Sound.playClick();
        }
        if (b4) {
            Serial.println("[Button] BTN4 -> click");
            Sound.playClick();
        }
        if (b5) {
            // Teste do SoundManager::playSample() (item novo, ver conversa) —
            // toca a amostra PCM de AlarmSample.h em vez do clique de sempre.
            Serial.println("[Button] BTN5 -> teste de playSample() (AlarmSample.h)");
            Sound.playSample(ALARM_SAMPLE_DATA, ALARM_SAMPLE_LEN, ALARM_SAMPLE_RATE);
        }
    }

    uint32_t now = millis();

    // Verifica alarmes 1x/seg (throttle interno por minuto já embutido).
    Alarms.update();

    // Etapa 2: só loga o snapshot de clima em memória.
    if (now - lastWeatherLogMs >= WEATHER_LOG_INTERVAL_MS) {
        lastWeatherLogMs = now;
        WeatherData w;
        if (Conn.getWeatherSnapshot(w)) {
            Serial.printf("[Weather] snapshot em memória: temp=%.1fC min=%.1f max=%.1f umid=%.0f%%\n",
                          w.temperature, w.minTemp, w.maxTemp, w.humidity);
        } else {
            Serial.println("[Weather] ainda sem dado (aguardando primeiro fetch)");
        }
    }

    // Etapa 5: monta o snapshot da tela e manda pro DisplayManager, que
    // decide sozinho o que redesenhar (dirty-tracking por campo) e só
    // faz o pushSprite físico se algo de fato mudou.
    if (now - lastDisplayMs >= DISPLAY_TICK_INTERVAL_MS) {
        lastDisplayMs = now;

        if (RtcClock.isTimeValid()) {
            ClockData data;
            RtcClock.getDisplayDateString(data.weekdayDate, sizeof(data.weekdayDate));
            RtcClock.getDisplayTimeString(data.time, sizeof(data.time));

            data.alarmRinging = Alarms.isRinging();
            if (data.alarmRinging) {
                // Enquanto toca: mostra o nome do alarme piscando no lugar
                // da hora (fase liga/desliga amarrada ao segundo real do
                // relógio, então o pisca-pisca fica com cadência estável).
                String label = Alarms.getRingingLabel();
                strncpy(data.ringingLabel, label.c_str(), sizeof(data.ringingLabel) - 1);
                data.blinkOn = (RtcClock.second() % 2 == 0);
            } else {
                String msg;
                if (Alarms.getMessage(msg)) {
                    // Mensagem transiente (ex.: "Toque em 5 minutos!" logo
                    // após o BTN5 ativar a soneca) tem prioridade sobre o
                    // próximo alarme por alguns segundos.
                    strncpy(data.transientMessage, msg.c_str(), sizeof(data.transientMessage) - 1);
                } else {
                    AlarmManager::NextAlarmInfo next = Alarms.getNextAlarm();
                    if (next.any) {
                        snprintf(data.alarmTime, sizeof(data.alarmTime), "%02d:%02d", next.hour, next.minute);
                        data.alarmEnabled = true;
                    } else {
                        strncpy(data.alarmTime, "--:--", sizeof(data.alarmTime) - 1);
                        data.alarmEnabled = false;
                    }
                }
            }

            WeatherData w;
            if (Conn.getWeatherSnapshot(w)) {
                data.tempCurrent = (int)(w.temperature + 0.5f);
                data.tempLow     = (int)(w.minTemp + 0.5f);
                data.tempHigh    = (int)(w.maxTemp + 0.5f);
                data.humidity    = (int)(w.humidity + 0.5f);
            }

            // Linha de status no rodapé: aviso de bateria do RTC tem
            // prioridade e sobrepõe o indicador de sincronização normal.
            if (RtcClock.isBatteryLow()) {
                strncpy(data.statusLine, "Bateria do RTC fraca - troque a CR2032", sizeof(data.statusLine) - 1);
                data.statusIsWarning = true;
            } else {
                uint32_t lastSync = max(Conn.getLastWeatherUpdateMs(), Alarms.getLastChangeMs());
                if (lastSync > 0) {
                    uint32_t elapsedMin = (millis() - lastSync) / 60000UL;
                    if (elapsedMin < 1) snprintf(data.statusLine, sizeof(data.statusLine), "Sincronizado agora");
                    else if (elapsedMin < 60) snprintf(data.statusLine, sizeof(data.statusLine), "Sincronizado ha %lumin", (unsigned long)elapsedMin);
                    else snprintf(data.statusLine, sizeof(data.statusLine), "Sincronizado ha %luh", (unsigned long)(elapsedMin / 60));
                }
                data.statusIsWarning = false;
            }

            Display.update(data);
        }
        // Sem hora válida ainda (sem RTC e sem NTP): tela fica preta
        // (deixada assim pelo boot color test) até a primeira hora chegar.
    }

    delay(20); // pequeno respiro pro scheduler; debounce dos botões é de 30ms, sobra margem
}
