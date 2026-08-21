#pragma once
#include <Arduino.h>

// =====================================================================
// PINOUT — ESP32-S3-N8R2
// =====================================================================

namespace Pins {

    // ---------- I2C — DS3231 (RTC) ----------
    constexpr uint8_t RTC_32K = 13;
    constexpr uint8_t RTC_SQW = 12;
    constexpr uint8_t I2C_SCL = 11;
    constexpr uint8_t I2C_SDA = 10;

    // ---------- I2S — MAX98357A (áudio) ----------
    constexpr uint8_t I2S_LRC  = 15;
    constexpr uint8_t I2S_BCLK = 7;
    constexpr uint8_t I2S_DIN  = 6;
    constexpr uint8_t I2S_GAIN = 5;
    constexpr uint8_t I2S_SD   = 4;

    // ---------- SPI compartilhado — TFT ILI9341 + Touch XPT2046 ----------
    constexpr uint8_t TFT_SCK   = 37;
    constexpr uint8_t TFT_MOSI  = 36;
    constexpr uint8_t TFT_MISO  = 39;
    constexpr uint8_t TFT_DC    = 35;
    constexpr uint8_t TFT_CS    = 21;
    constexpr uint8_t TFT_RST   = 47;
    constexpr uint8_t TFT_BL    = 38;
    constexpr uint8_t TOUCH_CS  = 41;
    constexpr uint8_t TOUCH_IRQ = 1;

    // ---------- Botões genéricos ----------
    // TODO: você ainda não me passou esses pinos — preencher antes de compilar
    // o módulo de botões. Deixei em 255 (inválido) de propósito para não
    // mascarar o esquecimento com um valor plausível mas errado.

    constexpr uint8_t BTN_1 = 16;
    constexpr uint8_t BTN_2 = 17;
    constexpr uint8_t BTN_3 = 18;
    constexpr uint8_t BTN_4 = 8;
    constexpr uint8_t BTN_5 = 48;

} // namespace Pins

// =====================================================================
// CONSTANTES DE DISPLAY
// =====================================================================

namespace DisplayCfg {
    constexpr uint16_t WIDTH  = 240;
    constexpr uint16_t HEIGHT = 320;
    constexpr uint8_t  ROTATION = 1;
    constexpr uint32_t SPI_FREQ_WRITE = 40'000'000;
    constexpr uint32_t SPI_FREQ_READ  = 16'000'000;
    constexpr uint32_t SPI_FREQ_TOUCH = 2'500'000;
} 
