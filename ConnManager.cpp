#include "ConnManager.h"
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstring>
#include <ArduinoOTA.h>

ConnManager Conn;

// Definições fora da classe (necessário em pré-C++17 para membros
// static constexpr passados por referência, ex. via min()).
constexpr uint32_t ConnManager::BACKOFF_BASE_MS;
constexpr uint32_t ConnManager::BACKOFF_MAX_MS;
constexpr uint8_t  ConnManager::HARD_RESET_AFTER_ATTEMPTS;

// =====================================================================
// Setup / eventos WiFi (Etapa 1)
// =====================================================================

void ConnManager::begin() {
    _weatherMutex = xSemaphoreCreateMutex();
    _yeelightQueue = xQueueCreate(16, sizeof(YeelightCommand));
    _wifiDownSignal = xSemaphoreCreateBinary();

    WiFi.persistent(false); // credenciais já vivem em config.h, não precisa gravar na NVS
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        this->onWifiEvent(event, info);
    });

    // Mitigação de flicker (ver comentário em WifiCfg, config.h): reduz a
    // potência de TX -> reduz o pico de corrente do amplificador de rádio,
    // que é a causa mais provável do sag de tensão que pisca o backlight.
    WiFi.setTxPower(WifiCfg::TX_POWER);
    esp_wifi_set_ps(WifiCfg::POWER_SAVE_MODE); // depois do mode(), antes/depois do begin()
    WiFi.begin(WifiCfg::SSID, WifiCfg::PASSWORD);

    Serial.printf("[Conn] WiFi.begin() disparado para SSID \"%s\" (TX power reduzido, PS mode configurado), aguardando eventos...\n", WifiCfg::SSID);

    // watchdogTask em prioridade 2 (vs 1 das outras) — ela passa quase todo
    // o tempo bloqueada num semáforo/vTaskDelay, então não risca faminta
    // nenhuma outra task; a prioridade maior só garante que reagir a uma
    // desconexão não fique atrás de um fetch de clima em andamento no
    // mesmo core.
    xTaskCreatePinnedToCore(watchdogTask, "wifi_watchdog", 4096, this, 2, &_watchdogHandle, 0);
    xTaskCreatePinnedToCore(weatherTask, "weather_fetch", 8192, this, 1, &_weatherHandle, 0);
    xTaskCreatePinnedToCore(yeelightTask, "yeelight_send", 4096, this, 1, &_yeelightHandle, 0);
    xTaskCreatePinnedToCore(otaTask, "ota", 4096, this, 1, &_otaHandle, 0);
}

void ConnManager::otaTask(void* param) {
    auto* self = static_cast<ConnManager*>(param);
    bool otaStarted = false;

    for (;;) {
        // Só inicia o ArduinoOTA (que registra serviço mDNS e abre a porta
        // de upload) depois da 1a conexão — antes disso não tem rede pra
        // anunciar nada.
        if (!otaStarted && self->_connected.load()) {
            ArduinoOTA.setHostname(OtaCfg::HOSTNAME);
            ArduinoOTA.setPassword(OtaCfg::PASSWORD);

            ArduinoOTA.onStart([]() {
                const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
                Serial.printf("[OTA] iniciando atualizacao (%s)...\n", type);
            });
            ArduinoOTA.onEnd([]() {
                Serial.println("[OTA] atualizacao concluida, reiniciando...");
            });
            ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                static uint8_t lastPct = 255;
                uint8_t pct = total ? (uint8_t)((progress * 100) / total) : 0;
                if (pct != lastPct) {
                    Serial.printf("[OTA] progresso: %u%%\n", pct);
                    lastPct = pct;
                }
            });
            ArduinoOTA.onError([](ota_error_t error) {
                Serial.printf("[OTA] erro [%u]: ", error);
                switch (error) {
                    case OTA_AUTH_ERROR:    Serial.println("falha de autenticacao"); break;
                    case OTA_BEGIN_ERROR:   Serial.println("falha ao iniciar"); break;
                    case OTA_CONNECT_ERROR: Serial.println("falha de conexao"); break;
                    case OTA_RECEIVE_ERROR: Serial.println("falha ao receber"); break;
                    case OTA_END_ERROR:     Serial.println("falha ao finalizar"); break;
                    default:                Serial.println("desconhecido"); break;
                }
            });

            ArduinoOTA.begin();
            Serial.printf("[OTA] pronto — upload via WiFi em \"%s.local\" (Arduino IDE: Tools > Port)\n", OtaCfg::HOSTNAME);
            otaStarted = true;
        }

        if (otaStarted) ArduinoOTA.handle();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool ConnManager::isConnected() const {
    return _connected.load();
}

IPAddress ConnManager::localIP() const {
    return IPAddress(_ipAddr.load());
}

int8_t ConnManager::reconnectAttempts() const {
    return _reconnectAttempts.load();
}

void ConnManager::onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    // Handler roda no contexto da task de eventos do framework — mantido
    // rápido de propósito: só atualiza estado e loga, nunca bloqueia em
    // rede nem chama WiFi.reconnect() diretamente (evita reentrância).
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("[Conn] STA iniciada");
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            // ssid não é garantidamente null-terminated (buffer fixo de 32
            // bytes) -> imprime só os ssid_len bytes de fato preenchidos.
            Serial.printf("[Conn] associado ao AP \"%.*s\" (canal %d)\n",
                          info.wifi_sta_connected.ssid_len, (const char*)info.wifi_sta_connected.ssid,
                          info.wifi_sta_connected.channel);
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            _connected.store(true);
            _ipAddr.store((uint32_t)info.got_ip.ip_info.ip.addr);
            _reconnectAttempts.store(0);
            Serial.printf("[Conn] conectado! IP=%s RSSI=%ddBm\n",
                          IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(), WiFi.RSSI());
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            _connected.store(false);
            wifi_err_reason_t reason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
            Serial.printf("[Conn] desconectado! motivo=%d (%s)\n", reason, WiFi.disconnectReasonName(reason));
            xSemaphoreGive(_wifiDownSignal); // acorda a watchdogTask (ela dorme até isso acontecer)
            break;
        }

        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            _connected.store(false);
            Serial.println("[Conn] IP perdido");
            xSemaphoreGive(_wifiDownSignal);
            break;

        default:
            break;
    }
}

void ConnManager::watchdogTask(void* param) {
    auto* self = static_cast<ConnManager*>(param);

    for (;;) {
        // Bloqueia sem consumir CPU até o handler de evento sinalizar uma
        // desconexão — enquanto conectado, essa task não acorda por tempo
        // nenhuma vez (era um wake-up periódico redundante antes, já que
        // ARDUINO_EVENT_WIFI_STA_GOT_IP já zera _reconnectAttempts sozinho).
        xSemaphoreTake(self->_wifiDownSignal, portMAX_DELAY);

        // Enquanto não reconectar, tenta com backoff exponencial. Não
        // depende de um novo evento a cada tentativa: um único sinal de
        // "caiu" basta pra manter esse laço tentando até voltar.
        while (!self->_connected.load()) {
            int8_t attempts = self->_reconnectAttempts.load();
            // Expoente do backoff é limitado a um valor pequeno e fixo (não
            // ao contador bruto, que pode crescer indefinidamente) — evita
            // shift por uma quantidade grande (comportamento indefinido).
            uint8_t shift = (attempts < 5) ? attempts : 5;
            uint32_t backoff = min(BACKOFF_BASE_MS << shift, BACKOFF_MAX_MS);
            vTaskDelay(pdMS_TO_TICKS(backoff));

            if (self->_connected.load()) break; // reconectou sozinho enquanto dormíamos

            Serial.printf("[Conn][watchdog] desconectado, tentativa #%d (backoff=%ums)\n", attempts + 1, backoff);

            if (attempts >= HARD_RESET_AFTER_ATTEMPTS) {
                Serial.println("[Conn][watchdog] muitas falhas -> reset completo da STA");
                WiFi.disconnect(true);
                delay(100);
                WiFi.begin(WifiCfg::SSID, WifiCfg::PASSWORD);
            } else {
                WiFi.reconnect();
            }

            int8_t next = (attempts < 127) ? (attempts + 1) : attempts;
            self->_reconnectAttempts.store(next);
        }

        self->_reconnectAttempts.store(0);
        // Descarta qualquer sinal extra acumulado durante a reconexão (ex.:
        // LOST_IP chegando no meio do processo) pra voltar a dormir limpo.
        xSemaphoreTake(self->_wifiDownSignal, 0);
    }
}

// =====================================================================
// Clima (Etapa 2) — só memória, nunca toca em tela/RTC.
// =====================================================================

bool ConnManager::fetchWeatherData(WeatherData& out) {
    HTTPClient http;
    http.begin(ApiCfg::WEATHER_URL);

    // O servidor manda a resposta com "Transfer-Encoding: chunked" (HTTP/1.1).
    // http.getString() sabe decodificar isso, mas ler direto de
    // http.getStream() (como fazemos abaixo, de propósito, pra economizar o
    // buffer intermediário) NÃO decodifica -> os marcadores de tamanho de
    // chunk viravam lixo no meio do JSON e o parser silenciosamente não
    // achava os campos (sem erro, só zeros). useHTTP10(true) faz o cliente
    // pedir/aceitar a resposta sem chunking, então o stream vira o JSON puro.
    http.useHTTP10(true);
    Serial.println("[Weather] GET Open-Meteo...");

    int httpCode = http.GET();
    if (httpCode <= 0) {
        Serial.printf("[Weather] erro na requisicao: %s\n", http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    // Filtro: só guarda os 4 campos que de fato usamos (a API devolve
    // unidades/metadados que nunca lemos) -> parsing mais leve e um
    // documento bem menor.
    StaticJsonDocument<128> filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["daily"]["temperature_2m_min"] = true;
    filter["daily"]["temperature_2m_max"] = true;

    // Lê direto do stream HTTP em vez de materializar a resposta inteira
    // numa String antes de parsear — um buffer a menos coexistindo em heap.
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[Weather] erro ao desserializar JSON: %s\n", err.c_str());
        return false;
    }

    out.temperature = doc["current"]["temperature_2m"] | 0.0f;
    out.humidity    = doc["current"]["relative_humidity_2m"] | 0.0f;
    out.minTemp     = doc["daily"]["temperature_2m_min"][0] | 0.0f;
    out.maxTemp     = doc["daily"]["temperature_2m_max"][0] | 0.0f;

    Serial.printf("[Weather] recebido: temp=%.1fC min=%.1f max=%.1f umid=%.0f%%\n",
                  out.temperature, out.minTemp, out.maxTemp, out.humidity);
    return true;
}

bool ConnManager::getWeatherSnapshot(WeatherData& out) const {
    if (!_weatherMutex) return false;

    xSemaphoreTake(_weatherMutex, portMAX_DELAY);
    bool ready = _weatherReady.load();
    if (ready) out = _latestWeather;
    xSemaphoreGive(_weatherMutex);
    return ready;
}

uint32_t ConnManager::getLastWeatherUpdateMs() const {
    return _lastWeatherUpdateMs.load();
}

void ConnManager::weatherTask(void* param) {
    auto* self = static_cast<ConnManager*>(param);

    for (;;) {
        bool ok = false;

        if (self->_connected.load()) {
            WeatherData fresh; // fetch fica fora do lock
            ok = self->fetchWeatherData(fresh);
            if (ok) {
                xSemaphoreTake(self->_weatherMutex, portMAX_DELAY);
                self->_latestWeather = fresh;
                xSemaphoreGive(self->_weatherMutex);
                self->_weatherReady.store(true);
                self->_lastWeatherUpdateMs.store(millis());
            }
        } else {
            Serial.println("[Weather] sem WiFi ainda, tentando de novo em breve");
        }

        // Retry rápido enquanto não conseguiu (ex.: WiFi ainda conectando
        // no boot) -> não fica até 10 minutos sem dado só porque a
        // primeira tentativa correu antes do handshake terminar.
        vTaskDelay(pdMS_TO_TICKS(ok ? ApiCfg::FETCH_INTERVAL_MS : ApiCfg::RETRY_INTERVAL_MS));
    }
}

// =====================================================================
// Yeelight (Etapa 3) — raw TCP socket, fire-and-forget, via fila+task
// própria pra nunca bloquear quem chamou (ex.: botão no core 1).
// =====================================================================

void ConnManager::enqueueYeelight(const char* ip, const char* json) {
    if (!_yeelightQueue) return;

    YeelightCommand cmd{};
    strncpy(cmd.ip, ip, sizeof(cmd.ip) - 1);
    strncpy(cmd.json, json, sizeof(cmd.json) - 1);

    if (xQueueSend(_yeelightQueue, &cmd, 0) != pdTRUE) {
        Serial.println("[Yeelight] fila cheia, comando descartado");
    }
}

void ConnManager::requestYeelightToggle(const char* ip) {
    enqueueYeelight(ip, "{\"id\":1,\"method\":\"toggle\",\"params\":[]}");
}

void ConnManager::requestLightingScene(uint8_t scene) {
    switch (scene) {
        case 1:
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[6500,\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16777215,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        case 2:
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[3000,\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16750848,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
            break;
        case 3:
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"off\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[26367,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        case 4:
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[3000,\"smooth\",500]}");
            enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[5,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16711680,\"smooth\",500]}");
            enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[100,\"smooth\",500]}");
            break;
        default:
            Serial.printf("[Yeelight] cena invalida: %u\n", scene);
            break;
    }
}

void ConnManager::requestWakeLights() {
    Serial.println("[Yeelight] preset 'ligar luzes' (amarelado, brilho medio)");
    enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
    enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_ct_abx\",\"params\":[3000,\"smooth\",500]}");
    enqueueYeelight(Yeelight::CEILING_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
    enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_power\",\"params\":[\"on\",\"smooth\",500]}");
    enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_rgb\",\"params\":[16750848,\"smooth\",500]}");
    enqueueYeelight(Yeelight::BEDSIDE_IP, "{\"id\":1,\"method\":\"set_bright\",\"params\":[50,\"smooth\",500]}");
}

void ConnManager::sendYeelightBlocking(const char* ip, const char* json) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        Serial.println("[Yeelight] erro ao criar socket");
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Yeelight::PORT);
    serverAddr.sin_addr.s_addr = inet_addr(ip);

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) == 0) {
        char buf[176];
        int len = snprintf(buf, sizeof(buf), "%s\r\n", json);
        send(sock, buf, len, 0);
    } else {
        Serial.printf("[Yeelight] erro ao conectar em %s\n", ip);
    }

    shutdown(sock, SHUT_RDWR);
    close(sock);
}

void ConnManager::yeelightTask(void* param) {
    auto* self = static_cast<ConnManager*>(param);
    YeelightCommand cmd;

    for (;;) {
        if (xQueueReceive(self->_yeelightQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (!self->_connected.load()) {
                Serial.println("[Yeelight] sem WiFi, comando descartado");
                continue;
            }
            self->sendYeelightBlocking(cmd.ip, cmd.json);
        }
    }
}
