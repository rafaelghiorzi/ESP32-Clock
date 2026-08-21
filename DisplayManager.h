#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#include <cstdint>

// -----------------------------------------------------------------------
// Classe de baixo nível (config de barramento/painel/touch da LovyanGFX)
// -----------------------------------------------------------------------

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI        _bus;
    lgfx::Panel_ILI9341  _panel;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX() {

        // Barramento SPI
        {
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = DisplayCfg::SPI_FREQ_WRITE;
            cfg.freq_read  = DisplayCfg::SPI_FREQ_READ;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            // ATENÇÃO: confirme se seu config.h usa "namespace Pins { ... }"
            // ou constantes globais (TFT_SCK direto, sem prefixo). Ajuste
            // as 4 linhas abaixo para bater com o que existe de fato.
            cfg.pin_sclk   = Pins::TFT_SCK;
            cfg.pin_mosi   = Pins::TFT_MOSI;
            cfg.pin_miso   = Pins::TFT_MISO;
            cfg.pin_dc     = Pins::TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // Painel TFT
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = Pins::TFT_CS;
            cfg.pin_rst  = Pins::TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width  = DisplayCfg::WIDTH;
            cfg.panel_height = DisplayCfg::HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true; // o touch está no mesmo barramento físico
            _panel.config(cfg);
        }

        // Backlight
        {
            auto cfg = _light.config();
            cfg.pin_bl = Pins::TFT_BL;
            cfg.invert = false;
            cfg.freq   = DisplayCfg::BACKLIGHT_PWM_FREQ;
            cfg.pwm_channel = DisplayCfg::BACKLIGHT_PWM_CHANNEL;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        // Touch XPT2046 (mesmo barramento, CS próprio)
        {
            auto cfg = _touch.config();
            cfg.x_min = 0; cfg.x_max = DisplayCfg::WIDTH - 1;
            cfg.y_min = 0; cfg.y_max = DisplayCfg::HEIGHT - 1;
            cfg.pin_int = Pins::TOUCH_IRQ;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = SPI2_HOST;
            cfg.freq = DisplayCfg::SPI_FREQ_TOUCH;
            cfg.pin_sclk = Pins::TFT_SCK;
            cfg.pin_mosi = Pins::TFT_MOSI;
            cfg.pin_miso = Pins::TFT_MISO;
            cfg.pin_cs = Pins::TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};

// -----------------------------------------------------------------------
// Interface de alto nível usada pelo resto do firmware.
// main.cpp não deve conhecer LovyanGFX diretamente — só chama estes métodos.
// -----------------------------------------------------------------------

struct ClockData {
    String weekdayDate; // ex: "Qui, 1 de Jan"
    String time;        // ex: "00:00"
    String alarmTime;   // ex: "08:30"
    bool alarmEnabled = true;
    int tempCurrent = 0;
    int tempLow = 0;
    int tempHigh = 0;
    int humidity = 0;

    bool operator!=(const ClockData& o) const {
        return weekdayDate != o.weekdayDate || time != o.time ||
               alarmTime   != o.alarmTime   || alarmEnabled != o.alarmEnabled ||
               tempCurrent != o.tempCurrent || tempLow != o.tempLow ||
               tempHigh    != o.tempHigh    || humidity != o.humidity;
    }
};

class DisplayManager {
public:
    void begin(LGFX* gfx);

    // Chame sempre que houver dado novo. A tela só é redesenhada
    // (só os campos que mudaram) e há um único pushSprite() no final.
    // Sem tearing, independente de quantos campos mudaram.
    void update(const ClockData& data);

    void setBrightness(uint8_t value); // 0-255, chame só quando o valor MUDAR

private:
    LGFX*       _gfx = nullptr;
    LGFX_Sprite _frame; // framebuffer completo, alocado em PSRAM
    ClockData   _last;
    bool        _first = true;

    void drawDate(const String& text);
    void drawTime(const String& text);
    void drawAlarm(const String& text, bool enabled);
    void drawWeather(int cur, int lo, int hi, int hum);
};

// Instância global usada por main.cpp. A DEFINIÇÃO (sem "extern") precisa
// existir em exatamente um .cpp — confirme que já não existe em outro lugar
// do seu projeto antes de adicionar, ou você terá erro de símbolo duplicado.
extern DisplayManager Display;