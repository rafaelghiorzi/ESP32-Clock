#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "config.h"

// =====================================================================
// WebManager — servidor HTTP leve pra configurar os alarmes pelo celular/
// PC na mesma rede, mais um WebSocket (porta 81) só pra empurrar mudanças
// de status (hora/data/alarme tocando) em vez do cliente ficar perguntando
// a cada N segundos.
//
// A biblioteca "WebSockets" de Markus Sattler (arduinoWebSockets) é uma
// DEPENDÊNCIA NOVA — precisa instalar pelo Library Manager da Arduino IDE
// antes de compilar essa versão. Mantive o WebServer síncrono de sempre
// pra tudo que já funcionava (CRUD de alarmes) e só ADICIONEI o WebSocket
// ao lado, pra minimizar risco de quebrar algo que já estava funcionando.
//
// A página (HTML+CSS+JS, tudo num arquivo só, sem framework) fica como
// uma string na flash — sem SPIFFS/LittleFS, sem filesystem pra montar.
//
// Roda numa task própria pinada ao core 0 (mesmo core de todo o resto de
// rede) — chamando _server.handleClient() e _webSocket.loop() a cada
// 10ms, e checando se há algo novo pra empurrar a cada ~500ms — não
// bloqueia o loop()/display no core 1.
// =====================================================================
class WebManager {
public:
    void begin();

private:
    WebServer _server{80};
    WebSocketsServer _webSocket{81};

    void handleRoot();
    void handleGetAlarms();
    void handlePostAlarm();
    void handleStatus();
    void handleDismiss();
    void handleWakeLights();
    void handleNotFound();

    void onWsEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length);
    String buildStatusJson() const; // usado tanto por /api/status quanto pelo broadcast do WebSocket
    void broadcastStatusIfChanged();

    static void task(void* param);
    TaskHandle_t _taskHandle = nullptr;

    // Snapshot do que foi mandado pela última vez via WebSocket, pra só
    // empurrar de novo quando algo de fato mudou (nada de ficar mandando
    // JSON idêntico a cada 500ms).
    bool     _lastBroadcastRinging = false;
    uint32_t _lastBroadcastChangeMs = 0;
    char     _lastBroadcastTime[6] = "";
};

extern WebManager Web;
