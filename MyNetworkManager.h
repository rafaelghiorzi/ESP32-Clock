#ifndef MY_NETWORK_MANAGER_H
#define MY_NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

struct WeatherData {
    float temperature;
    float humidity;
    float minTemp;
    float maxTemp;
};

class MyNetworkManager {
private:
    const char* ssid;
    const char* password;

public:
    MyNetworkManager(const char* ssid, const char* password);

    void connect();
    bool isConnected();

    String makeGetRequest(const String& url);
    String makePostRequest(const String& url, const String& payload);

    WeatherData fetchWeatherData();
};

#endif // MY_NETWORK_MANAGER_H