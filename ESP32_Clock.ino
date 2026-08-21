#include <Arduino.h>
#include "config.h"
#include "DisplayManager.h"
#include "MyNetworkManager.h"
#include "TimeManager.h"
#include "ButtonManager.h"
#include "SoundManager.h"

const char* ssid = "Rafael";
const char* password = "18161512";

MyNetworkManager internet(ssid, password);
TimeManager timeManager;
ButtonManager Buttons;
SoundManager Sound;
LGFX gfx;

// ---- Estado do clima, agora protegido por mutex ----
// weatherTask (core 0) escreve; loop() (core 1) lê. WeatherData tem 4 floats,
// a cópia da struct NÃO é atômica -> sem mutex, loop() pode ler um estado
// parcialmente escrito (metade dos campos do fetch antigo, metade do novo).
static SemaphoreHandle_t weatherMutex;
WeatherData latestWeather = {0.0f, 0.0f, 0.0f, 0.0f};
bool weatherReady = false;

ClockData   lastSentToDisplay; // snapshot do que já foi enviado ao DisplayManager
bool        firstDisplayUpdate = true;

unsigned long lastSecondCheck = 0;
const unsigned long SECOND_INTERVAL = 1000UL;
uint8_t currentLightingScene = 0;
unsigned long lastLightingSceneMs = 0;
const unsigned long LIGHTING_SCENE_RESET_MS = 10000UL;

void weatherTask(void* param) {
    for (;;) {
        if (internet.isConnected()) {
            WeatherData fresh = internet.fetchWeatherData(); // fetch fica fora do lock

            xSemaphoreTake(weatherMutex, portMAX_DELAY);
            latestWeather = fresh;
            weatherReady  = true;
            xSemaphoreGive(weatherMutex);
        } else {
            Serial.println("WiFi desconectado. Tentando reconectar...");
            internet.connect();
        }
        vTaskDelay(pdMS_TO_TICKS(600000)); // sync a cada 10 minutos
    }
}

void ntpTask(void* param) {
    for (;;) {
        if (internet.isConnected()) {
            timeManager.syncFromNTP();
        }
        vTaskDelay(pdMS_TO_TICKS(3600000)); // sync a cada 1 hora
    }
}

void wifiTask(void* param) {
    for (;;) {
        if (!internet.isConnected()) {
            Serial.println("WiFi desconectado. Tentando reconectar...");
            internet.connect();
        }
        vTaskDelay(pdMS_TO_TICKS(900000)); // check a cada 15 minutos
    }
}

// Lê latestWeather de forma segura para uma cópia local.
// Retorna false se ainda não há dado disponível.
bool readWeatherSnapshot(WeatherData& out) {
    xSemaphoreTake(weatherMutex, portMAX_DELAY);
    bool ready = weatherReady;
    if (ready) out = latestWeather;
    xSemaphoreGive(weatherMutex);
    return ready;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    weatherMutex = xSemaphoreCreateMutex();

    Display.begin(&gfx);

    Buttons.begin();
    Sound.begin();

    timeManager.begin();
    internet.connect();

    bool timeSynced = timeManager.syncFromNTP();
    if (!timeSynced && timeManager.isRTCValid()) {
        timeManager.setFromRTC();
    }

    // fetch inicial síncrono, antes das tasks existirem -> sem race aqui
    latestWeather = internet.fetchWeatherData();
    weatherReady  = true;

    xTaskCreatePinnedToCore(weatherTask, "Weather Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(ntpTask, "NTP Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(wifiTask, "WiFi Task", 8192, NULL, 1, NULL, 0);

    // Monta o primeiro snapshot e manda de uma vez só para o display.
    lastSentToDisplay.weekdayDate  = timeManager.getDisplayDateString();
    lastSentToDisplay.time         = timeManager.getDisplayTimeString();
    lastSentToDisplay.alarmTime    = "08:30";
    lastSentToDisplay.alarmEnabled = true;
    lastSentToDisplay.tempCurrent  = (int)round(latestWeather.temperature);
    lastSentToDisplay.tempLow      = (int)round(latestWeather.minTemp);
    lastSentToDisplay.tempHigh     = (int)round(latestWeather.maxTemp);
    lastSentToDisplay.humidity     = (int)round(latestWeather.humidity);

    Display.update(lastSentToDisplay); // uma única transação/push
    firstDisplayUpdate = false;
}

void loop() {
    unsigned long now = millis();

    Buttons.update();

    if (Buttons.button1Clicked()) {
        Serial.println("[Button] BTN_1 clicked");
        internet.sendYeelight("192.168.1.157", "{\"id\":1,\"method\":\"toggle\",\"params\":[]}");
    }
    if (Buttons.button2Clicked()) {
        Serial.println("[Button] BTN_2 clicked");
        internet.sendYeelight("192.168.1.121", "{\"id\":1,\"method\":\"toggle\",\"params\":[]}");
    }
    if (Buttons.button3Clicked()) {
        Serial.println("[Button] BTN_3 clicked");
        currentLightingScene = (currentLightingScene % 4) + 1;
        delay(300);
        internet.applyLightingScene(currentLightingScene);
    }
    if (Buttons.button4Clicked()) {
        Serial.println("[Button] BTN_4 clicked");
        Sound.playClick();
    }
    if (Buttons.button5Clicked()) {
        Serial.println("[Button] BTN_5 clicked");
        Sound.playClick();
    }

    if (currentLightingScene != 0 &&
        millis() - lastLightingSceneMs >= LIGHTING_SCENE_RESET_MS) {
        currentLightingScene = 0;
    }

    if (now - lastSecondCheck >= SECOND_INTERVAL) {
        lastSecondCheck = now;

        if (!internet.isConnected()) {
            Serial.println("[Loop] WiFi desconectado. Tentando reconectar...");
            internet.connect();
        }

        timeManager.update();

        // Monta o snapshot inteiro do que a tela deveria mostrar agora...
        ClockData next = lastSentToDisplay;
        next.weekdayDate = timeManager.getDisplayDateString();
        next.time        = timeManager.getDisplayTimeString();
        // alarmTime/alarmEnabled ficam como estão até você ter um AlarmManager real

        WeatherData w;
        if (readWeatherSnapshot(w)) {
            next.tempCurrent = (int)round(w.temperature);
            next.tempLow     = (int)round(w.minTemp);
            next.tempHigh    = (int)round(w.maxTemp);
            next.humidity    = (int)round(w.humidity);
        }

        // ...e manda para o display em UMA chamada só. O próprio ClockDisplay
        // decide internamente o que redesenhar (dirty-tracking por campo) e
        // faz um único pushSprite() no fim -> nada de escrita parcial no painel.
        Display.update(next);
        lastSentToDisplay = next;
    }
}