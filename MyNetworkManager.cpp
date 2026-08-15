#include "MyNetworkManager.h"
#include <ArduinoJson.h>

MyNetworkManager::MyNetworkManager(const char* ssid, const char* password) {
    this->ssid = ssid;
    this->password = password;
}

void MyNetworkManager::connect() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

bool MyNetworkManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String MyNetworkManager::makeGetRequest(const String& url) {
    if (!isConnected()) {
        Serial.println("[HTTP] Erro: Sem conexao com o Wi-Fi");
        return "";
    }

    HTTPClient http;
    String payload = "";

    http.begin(url);
    Serial.print("[HTTP] Enviando GET para: ");
    Serial.println(url);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        Serial.print("[HTTP] Codigo de resposta: ");
        Serial.println(httpResponseCode);
        payload = http.getString();
    } else {
        Serial.print("[HTTP] Erro na requisicao GET: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

String MyNetworkManager::makePostRequest(const String& url, const String& jsonPayload) {
    if (!isConnected()) {
        Serial.println("[HTTP] Erro: Sem conexao com o Wi-Fi");
        return "";
    }

    HTTPClient http;
    String payload = "";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    Serial.print("[HTTP] Enviando POST para: ");
    Serial.println(url);

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
        Serial.print("[HTTP] Codigo de resposta: ");
        Serial.println(httpResponseCode);
        payload = http.getString();
    } else {
        Serial.print("[HTTP] Erro na requisicao POST: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

WeatherData MyNetworkManager::fetchWeatherData() {
    String url =
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=-15.802841"
        "&longitude=-47.894250"
        "&current=temperature_2m,relative_humidity_2m"
        "&daily=temperature_2m_min,temperature_2m_max"
        "&timezone=auto"
        "&forecast_days=1";

    String json = makeGetRequest(url);

    WeatherData data = {0.0f, 0.0f, 0.0f, 0.0f};

    if (json.length() == 0) {
        Serial.println("[Weather] Erro: Nenhum dado recebido da API");
        return data;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("[Weather] Erro ao desserializar JSON: ");
        Serial.println(error.c_str());
        return data;
    }

    data.temperature = doc["current"]["temperature_2m"] | 0.0f;
    data.humidity = doc["current"]["relative_humidity_2m"] | 0.0f;
    data.minTemp = doc["daily"]["temperature_2m_min"][0] | 0.0f;
    data.maxTemp = doc["daily"]["temperature_2m_max"][0] | 0.0f;

    Serial.println("[Weather] Dados recebidos! retornando...");
    
    return data;
}