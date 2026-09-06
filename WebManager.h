#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "config.h"

// =====================================================================
// WebManager — servidor HTTP leve pra configurar os alarmes pelo celular/
// PC na mesma rede. Só usa bibliotecas que já vêm com o core arduino-esp32
// (WebServer.h, ESPmDNS.h) — nenhuma dependência nova.
//
// A página (HTML+CSS+JS, tudo num arquivo só, sem framework) fica como
// uma string na flash — sem SPIFFS/LittleFS, sem filesystem pra montar.
//
// Roda numa task própria pinada ao core 0 (mesmo core de todo o resto de
// rede), chamando _server.handleClient() a cada 10ms — não bloqueia o
// loop()/display no core 1.
// =====================================================================
class WebManager {
public:
    void begin();

private:
    WebServer _server{80};

    void handleRoot();
    void handleGetAlarms();
    void handlePostAlarm();
    void handleStatus();
    void handleDismiss();
    void handleWakeLights();
    void handleNotFound();

    static void task(void* param);
    TaskHandle_t _taskHandle = nullptr;
};

extern WebManager Web;
