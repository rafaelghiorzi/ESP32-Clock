#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <cstring>
#include "config.h"

// =====================================================================
// LGFX — ILI9341 via SPI, sem touch, backlight ligado direto em 3.3V.
// Configuração idêntica ao sketch de bancada já validado no hardware novo
// (sem flicker, sem estouro) — só com os pinos vindos de config.h.
// =====================================================================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void) {
        { // Barramento SPI
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = DisplayCfg::SPI_FREQ_WRITE;
            cfg.freq_read  = DisplayCfg::SPI_FREQ_READ;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = Pins::Display::SCK;
            cfg.pin_mosi = Pins::Display::MOSI;
            cfg.pin_miso = Pins::Display::MISO;
            cfg.pin_dc   = Pins::Display::DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        { // Painel
            auto cfg = _panel_instance.config();
            cfg.pin_cs  = Pins::Display::CS;
            cfg.pin_rst = Pins::Display::RST;
            cfg.pin_busy = -1;
            cfg.panel_width  = DisplayCfg::WIDTH;
            cfg.panel_height = DisplayCfg::HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.readable  = false;
            cfg.invert    = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

// =====================================================================
// ClockData — snapshot de tudo que a tela pode mostrar. DisplayManager
// compara contra o snapshot anterior campo a campo e só redesenha (e só
// envia ao painel) o que de fato mudou.
// =====================================================================
// Campos de texto como buffers fixos, não String: essa struct é montada
// e comparada 1x/segundo pra sempre, então evitar alocação/liberação de
// heap aqui é o que mais importa pra não fragmentar a memória ao longo de
// dias/semanas rodando sem reboot (String aloca no heap a cada atribuição
// de conteúdo novo). Os poucos lugares que ainda usam String (rótulo de
// alarme tocando, mensagens da web) só rodam em eventos raros, não todo
// tick — copiados pra cá via strncpy.
struct ClockData {
    char weekdayDate[16] = "";   // "SEX, 04 SET"
    char time[6] = "";           // "13:45"
    char alarmTime[6] = "";      // "07:00" — próximo alarme habilitado
    bool alarmEnabled = true;
    int  tempCurrent = 0;
    int  tempLow     = 0;
    int  tempHigh    = 0;
    int  humidity    = 0;

    // Enquanto um alarme está tocando, a linha do alarme mostra o label
    // dele piscando (blinkOn alterna a cada tick de display) no lugar da
    // hora do próximo alarme.
    bool alarmRinging = false;
    char ringingLabel[16] = "";
    bool blinkOn = true;

    // Mensagem transiente (ex.: "Toque em 5 minutos!") mostrada estática
    // (sem piscar) na mesma linha, quando não há alarme tocando agora.
    char transientMessage[32] = "";

    // Linha de status no rodapé da tela: "Sincronizado há Xmin" (cinza
    // claro) normalmente, ou um aviso (laranja) que a SOBREPÕE quando
    // statusIsWarning=true (ex.: bateria do RTC fraca) — decidido no
    // ESP32_Clock.ino, não aqui.
    char statusLine[40] = "";
    bool statusIsWarning = false;

    bool operator!=(const ClockData& o) const {
        return strcmp(weekdayDate, o.weekdayDate) != 0 || strcmp(time, o.time) != 0 ||
               strcmp(alarmTime, o.alarmTime) != 0 || alarmEnabled != o.alarmEnabled ||
               tempCurrent != o.tempCurrent || tempLow != o.tempLow ||
               tempHigh    != o.tempHigh    || humidity != o.humidity ||
               alarmRinging != o.alarmRinging || strcmp(ringingLabel, o.ringingLabel) != 0 ||
               blinkOn != o.blinkOn || strcmp(transientMessage, o.transientMessage) != 0 ||
               strcmp(statusLine, o.statusLine) != 0 || statusIsWarning != o.statusIsWarning;
    }
};

// =====================================================================
// DisplayManager — Etapa 5: redraw dinâmico.
//
// Framebuffer inteiro em sprite (PSRAM) + dirty-tracking por campo (só
// redesenha o que mudou) + um único pushSprite() atômico por update() ->
// sem tearing, não importa quantos campos mudaram.
//
// A causa mais provável da sobreposição/números fantasma do código legado
// era o retângulo de "limpeza" de cada campo ter uma altura fixa "no
// olho" (ex.: 90px pro relógio) que podia não cobrir de fato a altura
// real do glyph pra aquele tamanho de fonte. Aqui a altura de cada
// retângulo é calculada a partir da métrica real da fonte (8px de base
// vezes o textSize), então nunca fica pequena demais.
// =====================================================================
class DisplayManager {
public:
    void begin();
    void update(const ClockData& data);

private:
    LGFX _gfx;
    LGFX_Sprite _frame;
    ClockData _last;
    bool _first = true;

    // Mantido por continuidade com o sketch de bancada (ciclo de cores de
    // bring-up). Roda direto no painel, antes do sprite existir.
    void runBootColorTest();

    void drawDate(const char* text);
    void drawTime(const char* text);
    void drawAlarm(const ClockData& data); // precisa do contexto todo (ringing/blink/label)
    void drawWeather(int cur, int lo, int hi, int hum);
    void drawStatus(const ClockData& data); // linha de status no rodapé (sync/aviso)
};

extern DisplayManager Display;
