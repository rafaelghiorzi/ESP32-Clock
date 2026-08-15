 #pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

// -----------------------------------------------------------------------
// Classe de baixo nível (config de barramento/painel/touch da LovyanGFX)
// -----------------------------------------------------------------------

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX();
};

// -----------------------------------------------------------------------
// Interface de alto nível usada pelo resto do firmware.
// main.cpp não deve conhecer LovyanGFX diretamente — só chama estes métodos.
// -----------------------------------------------------------------------

class DisplayManager {
public:
    void begin();
    void drawStaticClockFrame();

    void updateTime(const char* time);
    void updateDate(const char* date);
    void updateAlarm(const char* alarmTime);
    void updateWeather(int temperature, int humidity, int tempMin, int tempMax);

    // Acesso direto, só para casos que realmente precisem (ex: debug/touch)
    LGFX& raw() { return _lcd; }

private:
    LGFX _lcd;
    bool _frameInitialized = false;

    lgfx::LGFX_Sprite _timeSprite{&_lcd};
    lgfx::LGFX_Sprite _dateSprite{&_lcd};
    lgfx::LGFX_Sprite _alarmSprite{&_lcd};
    lgfx::LGFX_Sprite _weatherSprite{&_lcd};

    static constexpr int TIME_X = 20,  TIME_Y = 55,  TIME_W = 200, TIME_H = 90;
    static constexpr int DATE_X = 60,  DATE_Y = 20,  DATE_W = 150, DATE_H = 30;
    static constexpr int ALARM_X = 120, ALARM_Y = 145, ALARM_W = 100, ALARM_H = 25;
    static constexpr int WEATHER_X = 60, WEATHER_Y = 183, WEATHER_W = 180, WEATHER_H = 32;
};

// Instância global única (padrão singleton simples, suficiente pra um firmware embarcado)
extern DisplayManager Display;

