#pragma once
#include <Arduino.h>
#include <WiFi.h>

// =====================================================================
// NetLog — espelha tudo que já é escrito via Serial.print/printf/println
// também pra uma conexão telnet (porta 23), sem precisar mudar nenhuma
// chamada de log já existente no projeto.
//
// Como isso funciona sem tocar em cada Serial.print(): cada arquivo .cpp
// (não este) adiciona, DEPOIS de todos os seus próprios #include:
//
//     #include "NetLog.h"
//     #define Serial NetSerial
//
// A partir dali, todo `Serial.algumaCoisa(...)` naquele arquivo vira
// `NetSerial.algumaCoisa(...)` — que grava na USB de sempre E manda pro
// cliente telnet conectado, se houver. Importante: isso só é seguro
// porque a substituição textual só alcança o CÓDIGO escrito depois da
// linha #define — nunca o conteúdo de nenhuma biblioteca (ArduinoOTA.h,
// WebSocketsServer.h etc.), já que essas já foram incluídas antes. Por
// isso a macro nunca vai dentro de um header compartilhado (config.h) —
// só no fim do bloco de #include de cada .cpp individual.
//
// O ESP32_Clock.ino de propósito NÃO usa esse truque (o gerador de
// protótipos do Arduino IDE mexe com a ordem de #define no arquivo
// principal de um jeito imprevisível) — só chama NetSerial.begin() uma
// vez no setup(), e mantém Serial.* normal pros próprios logs dele.
//
// O servidor de telnet só sobe depois que o WiFi conecta — antes disso
// (boot, tentativas de conexão) os logs só saem pela USB mesmo, então o
// monitor serial continua necessário pra diagnosticar problema de boot/
// conexão. Só aceita 1 cliente por vez (é ferramenta de debug, não um
// serviço multiusuário) e não tem senha — mesmo modelo de confiança da
// página de alarmes (rede local já é considerada confiável).
// =====================================================================
class NetLog : public Print {
public:
    void begin(unsigned long baud);

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;

private:
    static void acceptTask(void* param);

    WiFiServer _server{23};
    WiFiClient _client;
};

extern NetLog NetSerial;
