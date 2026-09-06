#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "config.h"

// =====================================================================
// ConnManager — WiFi (Etapa 1) + fetch de clima em memória (Etapa 2) +
// controle Yeelight (Etapa 3).
//
// Nome deliberadamente diferente de "NetworkManager"/"Network": o core
// arduino-esp32 3.x já expõe uma classe NetworkManager e uma instância
// global `Network` (Network.h / NetworkEvents.h, usados internamente por
// WiFi.h) — reusar esses nomes causaria conflito de símbolo/import.
// Também evitamos "WiFiManager" (colide com a lib de terceiros tzapu).
//
// Arquitetura: conexão WiFi não-bloqueante orientada a eventos
// (WiFi.onEvent), com uma task leve de segurança pinada ao core 0 que só
// age quando o auto-reconnect do driver não se recupera sozinho. O fetch
// de clima roda numa task própria no core 0, escrevendo o resultado numa
// struct protegida por mutex (múltiplos campos -> não cabe em atomic). O
// envio de comandos Yeelight roda numa terceira task no core 0, consumindo
// uma fila — assim um clique de botão (core 1) nunca bloqueia esperando o
// socket TCP (que pode levar até ~500ms por comando).
//
// PONTO DE EXTENSÃO (não implementado ainda):
//   - Etapa 4 (NTP + DS3231): sincronização de hora, ainda sem lugar aqui.
// =====================================================================

struct WeatherData {
    float temperature = 0.0f;
    float humidity     = 0.0f;
    float minTemp       = 0.0f;
    float maxTemp       = 0.0f;
};

class ConnManager {
public:
    void begin();

    bool isConnected() const;
    IPAddress localIP() const;
    int8_t reconnectAttempts() const;

    // Etapa 2 — copia o snapshot mais recente de clima para `out` sob
    // mutex. Retorna false se ainda não houve nenhum fetch bem-sucedido.
    bool getWeatherSnapshot(WeatherData& out) const;

    // Etapa 3 — enfileira comandos Yeelight (não bloqueante, seguro para
    // chamar direto do loop()/handler de botão no core 1). O envio real
    // (socket TCP, até ~500ms) acontece na yeelightTask, no core 0.
    void requestYeelightToggle(const char* ip);
    void requestLightingScene(uint8_t scene);

private:
    void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static void watchdogTask(void* param);
    static void weatherTask(void* param);
    static void yeelightTask(void* param);

    // bloqueante (HTTP), só chamado dentro de weatherTask. Retorna false em
    // caso de erro de rede/parse -> weatherTask usa isso pra decidir entre
    // retry rápido (falhou) ou intervalo normal (sucesso).
    bool fetchWeatherData(WeatherData& out);

    struct YeelightCommand {
        char ip[16];
        char json[160];
    };
    void enqueueYeelight(const char* ip, const char* json);
    void sendYeelightBlocking(const char* ip, const char* json); // raw socket, só chamado dentro de yeelightTask

    std::atomic<bool>     _connected{false};
    std::atomic<uint32_t> _ipAddr{0};
    std::atomic<int8_t>   _reconnectAttempts{0};

    // Semáforo binário: o handler de evento avisa a watchdogTask só quando
    // desconecta (STA_DISCONNECTED/LOST_IP). Enquanto conectado, a task
    // fica bloqueada em xSemaphoreTake(portMAX_DELAY) -> zero wake-ups
    // periódicos, CPU do core 0 realmente ociosa (antes acordava a cada
    // WATCHDOG_IDLE_PERIOD_MS só pra zerar um contador que o evento
    // ARDUINO_EVENT_WIFI_STA_GOT_IP já zera sozinho).
    SemaphoreHandle_t _wifiDownSignal = nullptr;

    SemaphoreHandle_t _weatherMutex = nullptr;
    WeatherData       _latestWeather{};
    std::atomic<bool> _weatherReady{false};

    QueueHandle_t _yeelightQueue = nullptr;

    TaskHandle_t _watchdogHandle  = nullptr;
    TaskHandle_t _weatherHandle   = nullptr;
    TaskHandle_t _yeelightHandle  = nullptr;

    static constexpr uint32_t BACKOFF_BASE_MS = 2000;
    static constexpr uint32_t BACKOFF_MAX_MS  = 60000;
    static constexpr uint8_t  HARD_RESET_AFTER_ATTEMPTS = 5;
};

extern ConnManager Conn;
