#include "MyNetworkManager.h"
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/time.h>
#include <cstring>

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

void MyNetworkManager::sendYeelight(const String& ip, const String& command) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock < 0) {
        Serial.println("[Yeelight] Erro ao criar socket");
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;

    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
               &timeout, sizeof(timeout));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(55443);
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (::connect(sock,
                  reinterpret_cast<struct sockaddr*>(&serverAddr),
                  sizeof(serverAddr)) == 0) {
        String fullCommand = command + "\r\n";

        send(sock,
             fullCommand.c_str(),
             fullCommand.length(),
             0);
    } else {
        Serial.println("[Yeelight] Erro ao conectar");
    }

    shutdown(sock, SHUT_RDWR);
    close(sock);
}

void MyNetworkManager::applyLightingScene(uint8_t scene) {
    const String ceilingLight = "192.168.0.182";
    const String bedsideLight = "192.168.0.126";

    switch (scene) {
        case 1:
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[6500,\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16777215,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        case 2:
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[3000,\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16750848,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
            break;
        case 3:
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"off\",\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[26367,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        case 4:
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[3000,\"smooth\",500]}");
            sendYeelight(ceilingLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[5,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16711680,\"smooth\",500]}");
            sendYeelight(bedsideLight, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        default:
            Serial.printf("[Yeelight] Cena invalida: %u\n", scene);
            break;
    }
}


