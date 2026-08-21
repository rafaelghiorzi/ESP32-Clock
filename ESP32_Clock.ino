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

WeatherData latestWeather = {0.0f, 0.0f, 0.0f, 0.0f};
bool weatherReady = false;

String latestDisplayedTime = "";
String latestDisplayedDate = "";
WeatherData latestDisplayedWeather = {0.0f, 0.0f, 0.0f, 0.0f};
bool weatherDisplayed = false;

unsigned long lastSecondCheck = 0;
const unsigned long SECOND_INTERVAL = 1000UL; // 1 segundo
uint8_t currentLightingScene = 0;
unsigned long lastLightingSceneMs = 0;
const unsigned long LIGHTING_SCENE_RESET_MS = 10000UL;

void weatherTask(void* param) {
    for (;;) {
        if (internet.isConnected()) {
            latestWeather = internet.fetchWeatherData();
            weatherReady = true;
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

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Display.begin();
    Display.drawStaticClockFrame();

    Buttons.begin();
    Sound.begin();

    timeManager.begin();
    internet.connect();

    // Preferir RTC como fonte de tempo se disponível, caso contrário usar NTP
    if (timeManager.isRTCValid()) {
        timeManager.setFromRTC();
    } else {
        timeManager.syncFromNTP();
    }

    // fetch inicial de dados
    latestWeather = internet.fetchWeatherData();
    weatherReady = true;

    xTaskCreatePinnedToCore(weatherTask, "Weather Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(ntpTask, "NTP Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(wifiTask, "WiFi Task", 8192, NULL, 1, NULL, 0);

    latestDisplayedTime = timeManager.getDisplayTimeString();
    latestDisplayedDate = timeManager.getDisplayDateString();

    Display.updateTime(latestDisplayedTime.c_str());
    Display.updateDate(latestDisplayedDate.c_str());
    Display.updateAlarm("08:30");

    if (weatherReady) {
        Display.updateWeather(
            (int)round(latestWeather.temperature),
            (int)round(latestWeather.humidity),
            (int)round(latestWeather.minTemp),
            (int)round(latestWeather.maxTemp)
        );
    }
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
        lastLightingSceneMs = millis();
        Sound.playClick();
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

        String currentTime = timeManager.getDisplayTimeString();
        String currentDate = timeManager.getDisplayDateString();

        if (currentTime != latestDisplayedTime) {
            latestDisplayedTime = currentTime;
            Display.updateTime(currentTime.c_str());
        }

        if (currentDate != latestDisplayedDate) {
            latestDisplayedDate = currentDate;
            Display.updateDate(currentDate.c_str());
        }

        bool weatherChanged = !weatherDisplayed ||
            latestWeather.temperature != latestDisplayedWeather.temperature ||
            latestWeather.humidity    != latestDisplayedWeather.humidity    ||
            latestWeather.minTemp     != latestDisplayedWeather.minTemp     ||
            latestWeather.maxTemp     != latestDisplayedWeather.maxTemp;


        // flagging possible concurrency issues. This might need an FreeRTOS mutex
        if (weatherReady && weatherChanged) {
            latestDisplayedWeather = latestWeather;
            weatherDisplayed = true;
            Display.updateWeather(
                (int)round(latestWeather.temperature),
                (int)round(latestWeather.humidity),
                (int)round(latestWeather.minTemp),
                (int)round(latestWeather.maxTemp)
            );
        }
    }
} 