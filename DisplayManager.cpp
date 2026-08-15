#include "DisplayManager.h"

// =====================================================================
// LGFX — configuração de hardware (barramento SPI compartilhado,
// painel ILI9341, touch XPT2046)
// =====================================================================

LGFX::LGFX() {
    { // Barramento SPI
        auto cfg = _bus.config();
        cfg.spi_host   = SPI3_HOST;
        cfg.spi_mode   = 0;
        cfg.freq_write = DisplayCfg::SPI_FREQ_WRITE;
        cfg.freq_read  = DisplayCfg::SPI_FREQ_READ;
        cfg.pin_sclk   = Pins::TFT_SCK;
        cfg.pin_mosi   = Pins::TFT_MOSI;
        cfg.pin_miso   = Pins::TFT_MISO;
        cfg.pin_dc     = Pins::TFT_DC;
        _bus.config(cfg);
        _panel.setBus(&_bus);
    }

    { // Painel TFT
        auto cfg = _panel.config();
        cfg.pin_cs   = Pins::TFT_CS;
        cfg.pin_rst  = Pins::TFT_RST;
        cfg.pin_busy = -1;
        cfg.panel_width  = DisplayCfg::WIDTH;
        cfg.panel_height = DisplayCfg::HEIGHT;
        _panel.config(cfg);
    }

    { // Touch XPT2046 (mesmo barramento, CS próprio)
        auto cfg = _touch.config();
        cfg.spi_host   = SPI3_HOST;
        cfg.pin_cs     = Pins::TOUCH_CS;
        cfg.pin_int    = Pins::TOUCH_IRQ;
        cfg.bus_shared = true;
        cfg.freq       = DisplayCfg::SPI_FREQ_TOUCH;
        cfg.x_min = 0; cfg.x_max = 4095;
        cfg.y_min = 0; cfg.y_max = 4095;
        _touch.config(cfg);
        _panel.setTouch(&_touch);
    }

    setPanel(&_panel);
}

// =====================================================================
// Bitmaps (gerados pelo Lopaka) — ficam encapsulados aqui, não em main.cpp
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

static const unsigned char PROGMEM image_paint_17_bits[] = { 0x80 };

static const unsigned char PROGMEM image_weather_temperature_bits[] = {
    0x1c,0x00,0x22,0x02,0x2b,0x05,0x2a,0x02,0x2b,0x38,0x2a,0x60,0x2b,0x40,0x2a,0x40,
    0x2a,0x60,0x49,0x38,0x9c,0x80,0xae,0x80,0xbe,0x80,0x9c,0x80,0x41,0x00,0x3e,0x00
};

// =====================================================================
// DisplayManager — interface pública
// =====================================================================

DisplayManager Display; // instância global única

void DisplayManager::begin() {
    pinMode(Pins::TFT_BL, OUTPUT);
    digitalWrite(Pins::TFT_BL, HIGH);

    _lcd.init();
    _lcd.setRotation(DisplayCfg::ROTATION);

    // Force sprite buffers into PSRAM — don't compete with WiFi/task stacks
    _timeSprite.setPsram(true);
    _dateSprite.setPsram(true);
    _alarmSprite.setPsram(true);
    _weatherSprite.setPsram(true);

    _timeSprite.setColorDepth(16);
    _dateSprite.setColorDepth(16);
    _alarmSprite.setColorDepth(16);
    _weatherSprite.setColorDepth(16);

    _timeSprite.createSprite(TIME_W, TIME_H);
    _dateSprite.createSprite(DATE_W, DATE_H);
    _alarmSprite.createSprite(ALARM_W, ALARM_H);
    _weatherSprite.createSprite(WEATHER_W, WEATHER_H);

    _frameInitialized = false;
}

void DisplayManager::drawStaticClockFrame() {
    if (_frameInitialized) {
        return;
    }
    
    _lcd.fillScreen(0x0);
    _lcd.drawBitmap(54, 192, image_weather_temperature_bits, 16, 16, 0xFB0C);
    _lcd.drawBitmap(202, 191, image_drop_bits, 16, 16, 0x24BE);
    _lcd.drawBitmap(121, 157, image_clock_alarm_bits, 15, 16, 0xA53F);
    _lcd.drawBitmap(155, 192, image_arrow_up_bits, 10, 14, 0xFB2C);
    _lcd.drawBitmap(110, 192, image_arrow_down_bits, 10, 14, 0x879F);
    _lcd.drawBitmap(129, 140, image_paint_17_bits, 1, 1, 0xC618);

    _frameInitialized = true;
}

void DisplayManager::updateTime(const char* time) {
    _timeSprite.fillScreen(0x0000);
    _timeSprite.setTextColor(0xFFFF);
    _timeSprite.setTextSize(2);
    _timeSprite.setFreeFont(&fonts::FreeSans24pt7b);
    _timeSprite.drawString(time, 24, 13);   // coords relative to sprite, not screen
    _timeSprite.pushSprite(TIME_X, TIME_Y);
}

void DisplayManager::updateDate(const char* date) {
    _dateSprite.fillScreen(0x0000);
    _dateSprite.setTextColor(0xFFFF);
    _dateSprite.setTextSize(1);
    _dateSprite.setFreeFont(&fonts::FreeSans12pt7b);
    _dateSprite.drawString(date, 30, 19);
    _dateSprite.pushSprite(DATE_X, DATE_Y);
}

void DisplayManager::updateAlarm(const char* alarmTime) {
    _alarmSprite.fillScreen(0x0000);
    _alarmSprite.setTextColor(0xFFFF);
    _alarmSprite.setTextSize(1);
    _alarmSprite.setFreeFont(&fonts::FreeSans12pt7b);
    _alarmSprite.drawString(alarmTime, 21, 10);
    _alarmSprite.pushSprite(ALARM_X, ALARM_Y);
}

void DisplayManager::updateWeather(int temperature, int humidity, int tempMin, int tempMax) {
    char buf[16];
    _weatherSprite.fillScreen(0x0000);
    _weatherSprite.setTextColor(0xFFFF);
    _weatherSprite.setTextSize(1);
    _weatherSprite.setFreeFont(&fonts::FreeSans12pt7b);

    // coordinates below are relative to WEATHER_X/WEATHER_Y origin now
    snprintf(buf, sizeof(buf), "%d", temperature);
    _weatherSprite.drawString(buf, 14, 7);

    snprintf(buf, sizeof(buf), "%d%%", humidity);
    _weatherSprite.drawString(buf, 159, 6);

    snprintf(buf, sizeof(buf), "%d", tempMax);
    _weatherSprite.drawString(buf, 107, 6);

    snprintf(buf, sizeof(buf), "%d", tempMin);
    _weatherSprite.drawString(buf, 62, 7);

    _weatherSprite.pushSprite(WEATHER_X, WEATHER_Y);
}