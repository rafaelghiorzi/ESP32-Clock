// V1.1 — pinout remapeado pra montagem em placa perfurada (ver README.md
// para a tabela completa de mudanças em relação à v1.0).

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
    namespace Display {
        constexpr uint8_t CS   = 4;
        constexpr uint8_t RST  = 5;
        constexpr uint8_t DC   = 6;
        constexpr uint8_t MOSI = 7;
        constexpr uint8_t SCK  = 15;
        constexpr int8_t  MISO = -1; // não conectado

        // Backlight passa por um MOSFET IRLZ44N (dreno no 3.3V, fonte no
        // pino "LED" do display, gate aqui via resistor de 100Ω +
        // pull-down de 10kΩ pro GND) -> PWM real via LEDC, em vez de
        // ligado direto no 3.3V sem controle nenhum.
        constexpr uint8_t BACKLIGHT = 16;
    }

    // ---------- Áudio — MAX98357A (I2S, mono) ----------
    // GAIN fixo em 5V (ganho 6dB) e SD flutuando (soma L+R / mono) são só
    // fiação, sem GPIO associado. VIN/GND levam desacoplamento (cerâmico +
    // eletrolítico, os dois em paralelo entre VIN e GND, não em série).
    namespace Audio {
        constexpr uint8_t LRC  = 9;  // WS
        constexpr uint8_t BCLK = 10;
        constexpr uint8_t DIN  = 11;

        // Buzzer piezo PASSIVO (não ativo — precisa de sinal de frequência
        // variável pra tocar notas diferentes). Driver a transistor NPN
        // (MPS2222) pra ganhar volume: o buzzer fica entre 5V e o Coletor,
        // Emissor no GND, e este pino aciona a Base através de um
        // resistor de 4.7kΩ — o GPIO só controla o transistor, não
        // alimenta o buzzer diretamente (dá uma oscilação de quase 5V no
        // buzzer em vez dos 3.3V de um GPIO puro, bem mais alto).
        constexpr uint8_t BUZZER = 47;
    }

    // ---------- RTC — DS3231 (I2C) ----------
    // Módulo breakout comum (com bateria CR2032) já tem pull-ups 4.7k
    // onboard — não adicionar externamente. VCC vem do 3.3V por fio
    // isolado direto (não passa pelo pino RX nem por nenhum outro pino
    // no caminho).
    // ATENÇÃO: SCL/SDA aqui não são os pinos default do Wire no S3 —
    // chamar Wire.begin(Pins::RTC::SDA, Pins::RTC::SCL) explicitamente.
    namespace RTC {
        constexpr uint8_t SDA = 1;
        constexpr uint8_t SCL = 2;
    }

    // ---------- Botões (5x) ----------
    // Do lado oposto da placa em relação ao resto — mas o DS3231 e sua
    // fiação ocupam bastante espaço logo ao lado de 1/2, então os botões
    // precisaram ficar mais espalhados/afastados dali do que o ideal.
    //
    // ATENÇÃO — histórico: por dificuldade física de montagem, esses
    // botões chegaram a ser fiados em 45/0/35/36/37. NÃO fazer isso de
    // novo: 35/36/37 são a PSRAM octal (o controlador de PSRAM usa esse
    // barramento o tempo inteiro, não só no boot — um botão nesses pinos
    // gera disputa elétrica constante com a PSRAM, travando o sistema via
    // watchdog e arriscando dano ao pino/chip; não tem fix de firmware
    // possível para isso).
    //
    // 0 e 45 são strapping pins, mas funcionam aqui: botão simples
    // fio-a-fio pro GND, sem resistor externo, então o pull-down/pull-up
    // interno do próprio chip prevalece no reset e nada muda de nível
    // (só cuidado: resetar a placa segurando o BTN3/GPIO0 força modo de
    // download em vez de bootar normal). GPIO46 foi propositalmente
    // EVITADO: é entrada-somente e não tem pull-up interno (diferente de
    // todos os outros pinos), então o INPUT_PULLUP do ButtonManager não
    // funcionaria nele sem um resistor de pull-up externo.
    namespace Buttons {
        constexpr uint8_t BTN1 = 38;
        constexpr uint8_t BTN2 = 39;
        constexpr uint8_t BTN3 = 0;
        constexpr uint8_t BTN4 = 45;
        constexpr uint8_t BTN5 = 21;
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

// =====================================================================
// BACKLIGHT — brilho automático via PWM (LEDC) no MOSFET IRLZ44N.
// =====================================================================
namespace BacklightCfg {
    constexpr uint32_t PWM_FREQ       = 20'000; // 20kHz, acima do audível — sem risco de "apito" no MOSFET/LED
    constexpr uint8_t  PWM_RESOLUTION = 8;       // 8 bits -> duty 0-255

    constexpr uint8_t DAY_BRIGHTNESS   = 255; // 100%
    constexpr uint8_t NIGHT_BRIGHTNESS = 20;  // ~8%, bem escurecido mas ainda legível no escuro

    // Janela "noturna": das 21h até as 6h do dia seguinte.
    constexpr uint8_t NIGHT_START_HOUR = 21;
    constexpr uint8_t NIGHT_END_HOUR   = 6;
}

// =====================================================================
// BUZZER — agora com driver a transistor (ver Pins::Audio::BUZZER),
// alimentado em 5V em vez de bater direto no GPIO de 3.3V — bem mais alto
// que antes. Ainda assim, piezos têm um pico de volume bem pronunciado na
// frequência de ressonância deles (varia por modelo, comum entre ~2 e
// 4.5kHz pros de 12mm) — mude o valor abaixo e teste ao vivo pra achar o
// ponto mais alto do seu buzzer específico.
// =====================================================================
namespace BuzzerCfg {
    constexpr float RING_FREQ_HZ = 1500.0f;
}
