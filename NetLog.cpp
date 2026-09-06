#include "NetLog.h"

// Nota: este arquivo NÃO usa "#define Serial NetSerial" — precisa acessar
// o Serial de verdade (USB) por baixo dos panos.
NetLog NetSerial;

void NetLog::begin(unsigned long baud) {
    Serial.begin(baud); // USB continua funcionando exatamente como sempre
    xTaskCreatePinnedToCore(acceptTask, "netlog_accept", 3072, this, 1, nullptr, 0);
}

size_t NetLog::write(uint8_t c) {
    Serial.write(c);
    if (_client && _client.connected()) _client.write(c);
    return 1;
}

size_t NetLog::write(const uint8_t* buffer, size_t size) {
    Serial.write(buffer, size);
    if (_client && _client.connected()) _client.write(buffer, size);
    return size;
}

void NetLog::acceptTask(void* param) {
    auto* self = static_cast<NetLog*>(param);
    bool serverStarted = false;

    for (;;) {
        if (!serverStarted && WiFi.status() == WL_CONNECTED) {
            self->_server.begin();
            self->_server.setNoDelay(true);
            serverStarted = true;
            Serial.println("[NetLog] servidor de log ligado -> 'telnet <ip-do-relogio> 23'");
        }

        if (serverStarted && self->_server.hasClient()) {
            if (self->_client && self->_client.connected()) {
                self->_server.accept().stop(); // já tem 1 cliente, recusa o novo
            } else {
                self->_client = self->_server.accept();
                Serial.println("[NetLog] cliente conectado via telnet");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
