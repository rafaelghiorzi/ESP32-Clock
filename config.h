#pragma once
#include <Arduino.h>
#include <WiFi.h> // wifi_power_t / wifi_ps_type_t usados em WifiCfg abaixo

// =====================================================================
// PINOUT — ESP32-S3 N16R8 (hardware novo)
//
// Pinos reservados, NUNCA usar para periféricos:
//   GPIO0, 3, 45, 46   -> strapping pins
//   GPIO26-32          -> flash SPI interno
//   GPIO33-37          -> PSRAM octal (N16R8 usa octal, não quad)
//   GPIO19, 20         -> USB nativo (D-/D+), modo "Hardware CDC and JTAG"
//   GPIO43, 44         -> UART0, evitados por precaução (log serial)
// =====================================================================

namespace Pins {

    // ---------- Display ILI9341 (SPI, sem touch) ----------
    // LED do backlight vai direto em 3.3V, sem GPIO dedicado nesta revisão.
    namespace Display {
        constexpr uint8_t CS   = 14;
        constexpr uint8_t RST  = 13;
        constexpr uint8_t DC   = 12;
        constexpr uint8_t MOSI = 11;
        constexpr uint8_t SCK  = 10;
        constexpr int8_t  MISO = -1; // não conectado
    }

    // ---------- Áudio — MAX98357A (I2S, mono) ----------
    // GAIN fixo em 5V (ganho 6dB) e SD flutuando (soma L+R / mono) são só
    // fiação, sem GPIO associado.
    namespace Audio {
        constexpr uint8_t LRC  = 6; // WS
        constexpr uint8_t BCLK = 5;
        constexpr uint8_t DIN  = 4;

        // Buzzer piezo PASSIVO (não ativo — precisa de sinal de frequência
        // variável pra tocar notas diferentes; um buzzer ativo só apita
        // numa frequência fixa e não serve aqui). Ainda não montado.
        // Ligação: GPIO -> resistor série de ~100Ω -> um terminal do
        // buzzer; outro terminal no GND. O resistor protege o GPIO/limita
        // corrente de pico; não é estritamente obrigatório pra um buzzer
        // pequeno, mas é barato e recomendado. Não precisa de transistor
        // pra volumes baixos/médios (a corrente de um piezo é bem baixa);
        // se quiser mais volume depois, um NPN tipo 2N2222 como driver
        // simples ajuda.
        constexpr uint8_t BUZZER = 9;
    }

    // ---------- RTC — DS3231 (I2C) — usado a partir da Etapa 4 ----------
    // Módulo breakout comum (com bateria CR2032) já tem pull-ups 4.7k
    // onboard — não adicionar externamente.
    // ATENÇÃO: SCL/SDA aqui não são os pinos default do Wire no S3 —
    // chamar Wire.begin(Pins::RTC::SDA, Pins::RTC::SCL) explicitamente.
    namespace RTC {
        constexpr uint8_t SCL = 1;
        constexpr uint8_t SDA = 2;
    }

    // ---------- Botões (5x) — usados a partir da Etapa 3 ----------
    namespace Buttons {
        constexpr uint8_t BTN1 = 15;
        constexpr uint8_t BTN2 = 16;
        constexpr uint8_t BTN3 = 17;
        constexpr uint8_t BTN4 = 18;
        constexpr uint8_t BTN5 = 8;
    }

} // namespace Pins

// =====================================================================
// DISPLAY
// =====================================================================
namespace DisplayCfg {
    constexpr uint16_t WIDTH  = 240;
    constexpr uint16_t HEIGHT = 320;
    constexpr uint8_t  ROTATION = 1;
    constexpr uint32_t SPI_FREQ_WRITE = 40'000'000;
    constexpr uint32_t SPI_FREQ_READ  = 16'000'000;
}

// =====================================================================
// ÁUDIO
// =====================================================================
namespace AudioCfg {
    constexpr uint32_t SAMPLE_RATE = 44100;
}

// =====================================================================
// WIFI (Etapa 1) — credenciais versionadas de propósito, igual ao legado
// =====================================================================
namespace WifiCfg {
    constexpr const char* SSID     = "Rafael";
    constexpr const char* PASSWORD = "18161512";

    // Mitigação de flicker no display: o pico de corrente de TX do rádio
    // WiFi (~300-500mA em transições de µs) causa sag na trilha de 3.3V
    // compartilhada, que aparece como flicker no backlight (ligado direto
    // em 3.3V, sem filtro). Reduzir a potência de TX reduz esse pico —
    // o roteador está perto, então não deveria custar alcance real.
    // Valores possíveis: WIFI_POWER_19_5dBm (máximo/padrão) até
    // WIFI_POWER_2dBm (mínimo). Ajuste aqui pra testar.
    constexpr wifi_power_t TX_POWER = WIFI_POWER_8_5dBm;

    // Modo de power-save do rádio. WIFI_PS_MIN_MODEM faz o rádio acordar a
    // cada beacon/DTIM do AP (~100-300ms) pra ouvir tráfego — cada wake-up
    // é um pico pequeno, o que explica flickers esporádicos mesmo depois
    // de conectado/ocioso. Como o projeto é alimentado por fonte (não
    // bateria), esse consumo extra não importa -> usa WIFI_PS_NONE, que
    // mantém o rádio sempre ligado e elimina esses wake-ups periódicos.
    constexpr wifi_ps_type_t POWER_SAVE_MODE = WIFI_PS_NONE;
}

// =====================================================================
// API DE CLIMA (Etapa 2) — Open-Meteo, sem necessidade de API key.
// Só atualiza a memória (ConnManager); tela e RTC não usam isso ainda.
// =====================================================================
namespace ApiCfg {
    constexpr const char* WEATHER_URL =
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=-15.802871"
        "&longitude=-47.894299"
        "&current=temperature_2m,relative_humidity_2m"
        "&daily=temperature_2m_min,temperature_2m_max"
        "&timezone=auto"
        "&forecast_days=1";

    constexpr uint32_t FETCH_INTERVAL_MS = 600'000; // 10 minutos, só após sucesso
    constexpr uint32_t RETRY_INTERVAL_MS = 20'000;  // enquanto sem sucesso (ex.: WiFi ainda conectando no boot)
}

// =====================================================================
// YEELIGHT (Etapa 3) — controle direto via LAN, raw TCP socket.
// =====================================================================
namespace Yeelight {
    constexpr const char* BEDSIDE_IP = "192.168.1.157";
    constexpr const char* CEILING_IP = "192.168.1.121";
    constexpr uint16_t    PORT       = 55443;
}

// =====================================================================
// HORA — NTP + DS3231 (Etapa 4). Brasília, UTC-3 fixo (Brasil não usa
// mais horário de verão desde 2019, então TZ fixa é suficiente e mais
// simples/barata que uma regra de DST).
// =====================================================================
namespace TimeCfg {
    constexpr const char* TIMEZONE    = "BRT3";
    constexpr const char* NTP_SERVER1 = "pool.ntp.org";
    constexpr const char* NTP_SERVER2 = "time.nist.gov";

    // Minimiza uso de rede: sincroniza raramente, o relógio do ESP32
    // segue contando sozinho entre uma sincronização e outra (correção
    // de deriva do cristal, não fonte primária de tempo a cada tick).
    constexpr uint32_t NTP_SYNC_INTERVAL_MS  = 6UL * 60 * 60 * 1000; // 6 horas
    constexpr uint32_t NTP_RETRY_INTERVAL_MS = 5UL * 60 * 1000;      // 5 minutos (só quando falhou)
    constexpr uint32_t NTP_TIMEOUT_MS        = 5000;
}

// =====================================================================
// OTA — upload de firmware por WiFi (ArduinoOTA), sem precisar de cabo.
// TROQUE ESSA SENHA — fica em texto puro aqui, igual às credenciais WiFi.
// =====================================================================
namespace OtaCfg {
    constexpr const char* HOSTNAME = "esp32clock";
    constexpr const char* PASSWORD = "18161512";
}

// =====================================================================
// ALARME — duração do toque, soneca única, mensagem na tela.
// =====================================================================
namespace AlarmCfg {
    constexpr uint32_t RING_DURATION_MS   = 3UL * 60 * 1000; // toca até 3min antes de soneca/desligar sozinho
    constexpr uint32_t SNOOZE_DURATION_MS = 5UL * 60 * 1000; // soneca dura 5min, silenciosa
    constexpr uint32_t SNOOZE_MESSAGE_MS  = 4000;            // "Toque em 5 minutos!" fica 4s na tela
}
