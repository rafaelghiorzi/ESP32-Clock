#include "DisplayManager.h"
#include <Arduino.h>
// =====================================================================
// Bitmaps (gerados pelo Lopaka) — ficam encapsulados aqui, não em main.cpp
// TODO: ainda não estão sendo usados em drawAlarm()/drawWeather() abaixo.
// =====================================================================

static const unsigned char PROGMEM image_arrow_down_bits[] = {
    0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,
    0xcc,0xc0,0xcc,0xc0,0x3f,0x00,0x3f,0x00,0x0c,0x00,0x0c,0x00
};

static const unsigned char PROGMEM image_arrow_up_bits[] = {
    0x0c,0x00,0x0c,0x00,0x3f,0x00,0x3f,0x00,0xcc,0xc0,0xcc,0xc0,0x0c,0x00,0x0c,0x00,
    0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00
};

static const unsigned char PROGMEM image_clock_alarm_bits[] = {
    0x79,0x3c,0xb3,0x9a,0xed,0x6e,0xd0,0x16,0xa0,0x0a,0x41,0x04,0x41,0x04,0x81,0x02,
    0xc1,0x06,0x82,0x02,0x44,0x04,0x48,0x04,0x20,0x08,0x10,0x10,0x2d,0x68,0x43,0x84
};

static const unsigned char PROGMEM image_drop_bits[] = {
    0x00,0x00,0x03,0xc0,0x06,0x60,0x0c,0x30,0x0c,0x30,0x18,0x18,0x10,0x08,0x10,0x08,
    0x10,0x08,0x10,0x28,0x10,0x28,0x18,0xf8,0x1c,0x38,0x0f,0xf0,0x00,0x00,0x00,0x00
};

// TODO: confirmar propósito — 1 byte só, parece artefato do export do Lopaka
static const unsigned char PROGMEM image_paint_17_bits[] = { 0x80 };

static const unsigned char PROGMEM image_weather_temperature_bits[] = {
    0x1c,0x00,0x22,0x02,0x2b,0x05,0x2a,0x02,0x2b,0x38,0x2a,0x60,0x2b,0x40,0x2a,0x40,
    0x2a,0x60,0x49,0x38,0x9c,0x80,0xae,0x80,0xbe,0x80,0x9c,0x80,0x41,0x00,0x3e,0x00
};

// Layout de referência (baseado no mockup)
namespace Layout {
    constexpr int DATE_Y    = 50;
    constexpr int TIME_Y    = 80;   // topo do texto grande da hora
    constexpr int ALARM_Y   = 160;
    constexpr int WEATHER_Y = 190;
}

// Instância global declarada em DisplayManager.h — DEFINIÇÃO única aqui.
DisplayManager Display;

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

constexpr uint16_t PASTEL_RED = rgb565(255, 145, 155);
constexpr uint16_t PASTEL_CYAN = rgb565(150, 235, 235);
constexpr uint16_t PASTEL_BLUE = rgb565(125, 190, 255);
constexpr uint16_t PASTEL_ORANGE = rgb565(255, 190, 125);

static void drawXbm(LGFX_Sprite& frame, int x, int y, const unsigned char* bitmap,
                    int sourceWidth, int sourceHeight, int drawWidth,
                    int drawHeight, uint16_t color) {
    int bytesPerRow = (sourceWidth + 7) / 8;
    for (int row = 0; row < drawHeight; ++row) {
        int sourceRow = row * sourceHeight / drawHeight;
        for (int column = 0; column < drawWidth; ++column) {
            int sourceColumn = column * sourceWidth / drawWidth;
            int byteIndex = sourceRow * bytesPerRow + sourceColumn / 8;
            if (bitmap[byteIndex] & (0x80 >> (sourceColumn & 7))) {
                frame.drawPixel(x + column, y + row, color);
            }
        }
    }
}

void DisplayManager::begin(LGFX* gfx) {
    _gfx = gfx;

    _gfx->init();
    _gfx->setRotation(DisplayCfg::ROTATION);
    _gfx->setBrightness(255);

    // Aloca o framebuffer inteiro em PSRAM. Em 320x240x16bpp = ~150KB,
    // tranquilo para os ~2MB de PSRAM do N8R2.
    _frame.setPsram(true);
    _frame.setColorDepth(16);
    _frame.createSprite(_gfx->width(), _gfx->height());
    _frame.fillScreen(TFT_BLACK);

    _first = true;
}

void DisplayManager::setBrightness(uint8_t value) {
    // Chame isso só quando o valor realmente mudar (ex: transição dia/noite,
    // toque do usuário). NUNCA dentro do loop de renderização — reconfigurar
    // o LEDC a cada frame é uma causa clássica de flicker no backlight.
    _gfx->setBrightness(value);
}

void DisplayManager::update(const ClockData& data) {
    if (!_first && !(data != _last)) return; // nada mudou, não faz nada

    if (_first || data.weekdayDate != _last.weekdayDate) drawDate(data.weekdayDate);
    if (_first || data.time        != _last.time)        drawTime(data.time);
    if (_first || data.alarmTime   != _last.alarmTime ||
                  data.alarmEnabled != _last.alarmEnabled) drawAlarm(data.alarmTime, data.alarmEnabled);
    if (_first || data.tempCurrent != _last.tempCurrent ||
                  data.tempLow     != _last.tempLow ||
                  data.tempHigh    != _last.tempHigh ||
                  data.humidity    != _last.humidity)
        drawWeather(data.tempCurrent, data.tempLow, data.tempHigh, data.humidity);

    // Único push físico para o painel, atômico -> zero tearing/corrupção,
    // não importa quantos campos foram redesenhados no buffer acima.
    _gfx->startWrite();
    _frame.pushSprite(_gfx, 0, 0);
    _gfx->endWrite();

    _last  = data;
    _first = false;
}

void DisplayManager::drawDate(const String& text) {
    int w = _frame.width();
    _frame.fillRect(0, Layout::DATE_Y - 4, w, 34, TFT_BLACK); // limpa só a faixa da data
    _frame.setTextDatum(top_center);
    _frame.setTextColor(TFT_WHITE);
    _frame.setTextSize(2);
    _frame.drawString(text, w / 2, Layout::DATE_Y);
}

void DisplayManager::drawTime(const String& text) {
    int w = _frame.width();
    _frame.fillRect(0, Layout::TIME_Y - 10, w, 90, TFT_BLACK); // limpa a faixa da hora
    _frame.setTextDatum(top_center);
    _frame.setTextColor(TFT_WHITE);
    _frame.setTextSize(9);
    _frame.drawString(text, w / 2, Layout::TIME_Y);
}

void DisplayManager::drawAlarm(const String& text, bool enabled) {
    int w = _frame.width();
    constexpr int ICON_WIDTH = 15;
    constexpr int ICON_HEIGHT = 16;
    constexpr int ICON_TEXT_GAP = 4;

    _frame.fillRect(0, Layout::ALARM_Y - 4, w, 28, TFT_BLACK);
    _frame.setTextColor(enabled ? TFT_WHITE : TFT_GOLD);
    _frame.setTextSize(2);

    int textWidth = _frame.textWidth(text);
    int groupWidth = ICON_WIDTH + ICON_TEXT_GAP + textWidth;
    int groupX = (w - groupWidth) / 2;
    drawXbm(_frame, groupX, Layout::ALARM_Y, image_clock_alarm_bits, 15, 16,
            ICON_WIDTH, ICON_HEIGHT,
            enabled ? PASTEL_ORANGE : TFT_DARKGREY);
    _frame.setTextDatum(top_left);
    _frame.drawString(text, groupX + ICON_WIDTH + ICON_TEXT_GAP, Layout::ALARM_Y);
}

void DisplayManager::drawWeather(int cur, int lo, int hi, int hum) {
    int w = _frame.width();
    constexpr int ICON_TEXT_GAP = 4;
    constexpr int ITEM_GAP = 14;

    _frame.fillRect(0, Layout::WEATHER_Y - 4, w, 28, TFT_BLACK);
    _frame.setTextSize(2);

    _frame.setTextColor(TFT_WHITE);
    String values[] = {
        String(cur),
        String(lo),
        String(hi),
        String(hum) + "%"
    };
    const unsigned char* icons[] = {
        image_weather_temperature_bits,
        image_arrow_down_bits,
        image_arrow_up_bits,
        image_drop_bits
    };
    uint16_t iconColors[] = {
        PASTEL_RED,
        PASTEL_CYAN,
        PASTEL_RED,
        PASTEL_BLUE
    };
    int iconWidths[] = {16, 10, 10, 16};
    int iconHeights[] = {16, 14, 14, 16};
    bool isTemperature[] = {true, true, true, false};

    int itemWidths[4];
    int totalWidth = ITEM_GAP * 3;
    for (int index = 0; index < 4; ++index) {
        itemWidths[index] = iconWidths[index] + ICON_TEXT_GAP + _frame.textWidth(values[index]);
        if (isTemperature[index]) {
            itemWidths[index] += 8 + _frame.textWidth("C");
        }
        totalWidth += itemWidths[index];
    }

    int x = (w - totalWidth) / 2;
    for (int index = 0; index < 4; ++index) {
        drawXbm(_frame, x, Layout::WEATHER_Y, icons[index],
            index == 1 || index == 2 ? 10 : 16,
            index == 1 || index == 2 ? 14 : 16,
            iconWidths[index], iconHeights[index], iconColors[index]);
        _frame.setTextDatum(top_left);
        int textX = x + iconWidths[index] + ICON_TEXT_GAP;
        _frame.drawString(values[index], textX, Layout::WEATHER_Y);
        if (isTemperature[index]) {
            int degreeX = textX + _frame.textWidth(values[index]) + 3;
            _frame.drawCircle(degreeX, Layout::WEATHER_Y + 3, 2, TFT_WHITE);
            _frame.drawString("C", degreeX + 5, Layout::WEATHER_Y);
        }
        x += itemWidths[index] + ITEM_GAP;
    }
}
