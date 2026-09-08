#include "DisplayManager.h"
#include "NetLog.h"
#define Serial NetSerial // espelha os logs deste arquivo também via telnet — ver NetLog.h

DisplayManager Display;

// =====================================================================
// Ícones (bitmaps XBM, gerados originalmente pelo Lopaka) — restaurados
// do código legado (deprecated/DisplayManager.cpp.txt), mesmos bytes.
// =====================================================================
static const unsigned char image_arrow_down_bits[] = {
    0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,
    0xcc,0xc0,0xcc,0xc0,0x3f,0x00,0x3f,0x00,0x0c,0x00,0x0c,0x00
};

static const unsigned char image_arrow_up_bits[] = {
    0x0c,0x00,0x0c,0x00,0x3f,0x00,0x3f,0x00,0xcc,0xc0,0xcc,0xc0,0x0c,0x00,0x0c,0x00,
    0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00
};

static const unsigned char image_clock_alarm_bits[] = {
    0x79,0x3c,0xb3,0x9a,0xed,0x6e,0xd0,0x16,0xa0,0x0a,0x41,0x04,0x41,0x04,0x81,0x02,
    0xc1,0x06,0x82,0x02,0x44,0x04,0x48,0x04,0x20,0x08,0x10,0x10,0x2d,0x68,0x43,0x84
};

static const unsigned char image_drop_bits[] = {
    0x00,0x00,0x03,0xc0,0x06,0x60,0x0c,0x30,0x0c,0x30,0x18,0x18,0x10,0x08,0x10,0x08,
    0x10,0x08,0x10,0x28,0x10,0x28,0x18,0xf8,0x1c,0x38,0x0f,0xf0,0x00,0x00,0x00,0x00
};

static const unsigned char image_weather_temperature_bits[] = {
    0x1c,0x00,0x22,0x02,0x2b,0x05,0x2a,0x02,0x2b,0x38,0x2a,0x60,0x2b,0x40,0x2a,0x40,
    0x2a,0x60,0x49,0x38,0x9c,0x80,0xae,0x80,0xbe,0x80,0x9c,0x80,0x41,0x00,0x3e,0x00
};

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

constexpr uint16_t PASTEL_RED    = rgb565(255, 145, 155);
constexpr uint16_t PASTEL_CYAN   = rgb565(150, 235, 235);
constexpr uint16_t PASTEL_BLUE   = rgb565(125, 190, 255);
constexpr uint16_t PASTEL_ORANGE = rgb565(255, 190, 125);

// =====================================================================
// Temas — cada um define o PAPEL de cada cor, não um valor fixo (ex.:
// "warning" é laranja vivo no Escuro/Noite, mas mais escuro no Claro pra
// manter contraste em fundo claro). Selecionável pela web, persistido na
// NVS. Índice 0 é o padrão de sempre (Escuro).
// =====================================================================
static const DisplayTheme THEMES[] = {
    // name,            bg,                    textPrimary,           textMuted,
    //   warning,               alarmDisabled,         alarmIconOn,   alarmIconOff,
    //   ringingText,   iconTempHot, iconTempCold, iconHumidity
    { "Escuro", TFT_BLACK, TFT_WHITE, TFT_LIGHTGREY,
      TFT_ORANGE, TFT_GOLD, PASTEL_ORANGE, TFT_DARKGREY,
      TFT_RED, PASTEL_RED, PASTEL_CYAN, PASTEL_BLUE },

    { "Claro", rgb565(245, 245, 245), rgb565(20, 20, 25), rgb565(120, 120, 125),
      rgb565(200, 80, 0), rgb565(150, 115, 0), rgb565(220, 130, 30), rgb565(190, 190, 190),
      rgb565(200, 30, 30), rgb565(200, 60, 70), rgb565(30, 140, 170), rgb565(40, 100, 190) },

    // "Noite": tudo em tons de vermelho — o vermelho preserva melhor a
    // visão noturna que branco/azul/ciano.
    { "Noite", TFT_BLACK, rgb565(190, 25, 25), rgb565(95, 20, 20),
      rgb565(170, 85, 10), rgb565(120, 55, 10), rgb565(180, 60, 20), rgb565(70, 20, 20),
      rgb565(255, 40, 40), rgb565(180, 40, 40), rgb565(150, 50, 50), rgb565(140, 40, 60) },

    { "Nascer do Sol", rgb565(15, 10, 30), rgb565(255, 230, 205), rgb565(180, 140, 150),
      rgb565(255, 100, 60), rgb565(200, 150, 120), rgb565(255, 160, 60), rgb565(120, 90, 110),
      rgb565(255, 90, 90), rgb565(255, 120, 90), rgb565(255, 190, 140), rgb565(230, 140, 180) },
};
static constexpr uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// Escalador de bitmap 1bpp "nearest neighbor" pixel a pixel — os ícones
// são pequenos (15x16 no máximo) e só redesenhados quando o campo muda,
// então o custo é desprezível.
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

// =====================================================================
// Layout — mesmas coordenadas/tamanhos já validados no mockup estático
// da Etapa 1. GLYPH_BASE_HEIGHT é a altura da fonte padrão (GLCD/Font0)
// em textSize=1 — usada pra calcular a altura real do retângulo de
// limpeza de cada campo (ver comentário no .h sobre a causa do bug de
// sobreposição do código legado).
// =====================================================================
namespace Layout {
    constexpr int GLYPH_BASE_HEIGHT = 8;
    constexpr int ROW_PADDING = 4;

    constexpr int DATE_Y = 20;      constexpr uint8_t DATE_SIZE = 2;
    constexpr int TIME_Y = 60;      constexpr uint8_t TIME_SIZE = 6;
    // Alarme e clima subiram 15px (de 150/190 pra 135/175) pra abrir espaço
    // pra linha de status no rodapé, sem encostar nela.
    constexpr int ALARM_Y = 135;    constexpr uint8_t ALARM_SIZE = 2;
    constexpr int WEATHER_Y = 175;  constexpr uint8_t WEATHER_SIZE = 2;

    // Linha de status: fonte pequena (size 1), colada no rodapé com
    // margem de 2-3px — geometria calculada em drawStatus() a partir da
    // altura real da tela, não fixada aqui.
    constexpr uint8_t STATUS_SIZE = 1;
    constexpr int STATUS_BOTTOM_MARGIN = 3;

    constexpr int rowHeight(uint8_t textSize) {
        return GLYPH_BASE_HEIGHT * textSize + ROW_PADDING * 2;
    }
}

void DisplayManager::begin() {
    Serial.println("[Display] init()...");

    // Backlight primeiro, no brilho máximo — senão o ciclo de cores de
    // bring-up ficaria invisível com o MOSFET ainda fechado (LEDC começa
    // em duty 0 até a primeira escrita).
    ledcAttach(Pins::Display::BACKLIGHT, BacklightCfg::PWM_FREQ, BacklightCfg::PWM_RESOLUTION);
    setBrightness(BacklightCfg::DAY_BRIGHTNESS);

    _gfx.init();
    _gfx.setRotation(DisplayCfg::ROTATION);

    runBootColorTest();

    // Framebuffer inteiro em PSRAM (240x320x16bpp = ~150KB, folga enorme
    // nos 8MB octal da N16R8).
    _frame.setPsram(true);
    _frame.setColorDepth(16);
    _frame.createSprite(_gfx.width(), _gfx.height());

    _themePrefs.begin("display", false);
    uint8_t savedTheme = _themePrefs.getUChar("theme", 0);
    _themeIndex.store(savedTheme < THEME_COUNT ? savedTheme : 0);
    Serial.printf("[Display] tema: %s\n", theme().name);

    _frame.fillScreen(theme().background);

    _first = true;
    Serial.println("[Display] pronto, aguardando primeiro update()");
}

const DisplayTheme& DisplayManager::theme() const {
    uint8_t idx = _themeIndex.load();
    return THEMES[idx < THEME_COUNT ? idx : 0];
}

void DisplayManager::setTheme(uint8_t index) {
    if (index >= THEME_COUNT) return;
    _themeIndex.store(index);
    _themePrefs.putUChar("theme", index);
    _forceRedraw.store(true); // força redesenhar tudo com as cores novas no próximo update()
    Serial.printf("[Display] tema trocado pra: %s\n", THEMES[index].name);
}

uint8_t DisplayManager::getTheme() const {
    return _themeIndex.load();
}

void DisplayManager::setBrightness(uint8_t value) {
    if (value == _lastBrightness) return; // já está nesse brilho, não reescreve o LEDC à toa
    _lastBrightness = value;
    ledcWrite(Pins::Display::BACKLIGHT, value);
    Serial.printf("[Display] brilho do backlight: %u/255\n", value);
}

uint8_t DisplayManager::themeCount() {
    return THEME_COUNT;
}

const char* DisplayManager::themeName(uint8_t index) {
    if (index >= THEME_COUNT) return "";
    return THEMES[index].name;
}

void DisplayManager::runBootColorTest() {
    const uint32_t colors[] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK };
    const char* names[]     = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
    for (int i = 0; i < 5; i++) {
        _gfx.fillScreen(colors[i]);
        _gfx.setTextDatum(top_left);
        _gfx.setTextColor(i == 4 ? TFT_WHITE : TFT_BLACK);
        _gfx.setTextSize(2);
        _gfx.setCursor(10, 10);
        _gfx.println(names[i]);
        delay(500);
    }
}

void DisplayManager::update(const ClockData& data) {
    bool force = _first || _forceRedraw.exchange(false);
    if (!force && !(data != _last)) return; // nada mudou, não faz nada (nem push)

    if (force) _frame.fillScreen(theme().background); // tema novo -> limpa tudo antes de redesenhar

    if (force || strcmp(data.weekdayDate, _last.weekdayDate) != 0)
        drawDate(data.weekdayDate);

    if (force || strcmp(data.time, _last.time) != 0)
        drawTime(data.time);

    bool alarmChanged = strcmp(data.alarmTime, _last.alarmTime) != 0 || data.alarmEnabled != _last.alarmEnabled ||
                        data.alarmRinging != _last.alarmRinging || strcmp(data.ringingLabel, _last.ringingLabel) != 0 ||
                        data.blinkOn != _last.blinkOn || strcmp(data.transientMessage, _last.transientMessage) != 0;
    if (force || alarmChanged)
        drawAlarm(data);

    if (force || data.tempCurrent != _last.tempCurrent || data.tempLow != _last.tempLow ||
                 data.tempHigh != _last.tempHigh || data.humidity != _last.humidity)
        drawWeather(data.tempCurrent, data.tempLow, data.tempHigh, data.humidity);

    if (force || strcmp(data.statusLine, _last.statusLine) != 0 || data.statusIsWarning != _last.statusIsWarning)
        drawStatus(data);

    // Único push físico pro painel, atômico -> zero tearing, não importa
    // quantos campos foram redesenhados no sprite acima.
    _gfx.startWrite();
    _frame.pushSprite(&_gfx, 0, 0);
    _gfx.endWrite();

    _last = data;
    _first = false;
}

void DisplayManager::drawDate(const char* text) {
    const DisplayTheme& t = theme();
    int w = _frame.width();
    int h = Layout::rowHeight(Layout::DATE_SIZE);
    _frame.fillRect(0, Layout::DATE_Y - Layout::ROW_PADDING, w, h, t.background);

    _frame.setTextDatum(top_center);
    _frame.setTextColor(t.textPrimary);
    _frame.setTextSize(Layout::DATE_SIZE);
    _frame.drawString(text, w / 2, Layout::DATE_Y);
}

void DisplayManager::drawTime(const char* text) {
    const DisplayTheme& t = theme();
    int w = _frame.width();
    int h = Layout::rowHeight(Layout::TIME_SIZE);
    _frame.fillRect(0, Layout::TIME_Y - Layout::ROW_PADDING, w, h, t.background);

    _frame.setTextDatum(top_center);
    _frame.setTextColor(t.textPrimary);
    _frame.setTextSize(Layout::TIME_SIZE);
    _frame.drawString(text, w / 2, Layout::TIME_Y);
}

void DisplayManager::drawAlarm(const ClockData& data) {
    const DisplayTheme& t = theme();
    int w = _frame.width();
    int h = Layout::rowHeight(Layout::ALARM_SIZE);
    _frame.fillRect(0, Layout::ALARM_Y - Layout::ROW_PADDING, w, h, t.background);

    if (data.alarmRinging) {
        // Fase "apagada" do pisca-pisca: só limpa (já feito acima) e sai.
        if (!data.blinkOn) return;

        _frame.setTextDatum(top_center);
        _frame.setTextColor(t.ringingText);
        _frame.setTextSize(Layout::ALARM_SIZE);
        _frame.drawString(data.ringingLabel, w / 2, Layout::ALARM_Y);
        return;
    }

    if (data.transientMessage[0] != '\0') {
        _frame.setTextDatum(top_center);
        _frame.setTextColor(t.warning);
        _frame.setTextSize(Layout::ALARM_SIZE);
        _frame.drawString(data.transientMessage, w / 2, Layout::ALARM_Y);
        return;
    }

    constexpr int ICON_WIDTH = 15;
    constexpr int ICON_HEIGHT = 16;
    constexpr int ICON_TEXT_GAP = 4;

    _frame.setTextColor(data.alarmEnabled ? t.textPrimary : t.alarmDisabled);
    _frame.setTextSize(Layout::ALARM_SIZE);

    int textWidth = _frame.textWidth(data.alarmTime);
    int groupWidth = ICON_WIDTH + ICON_TEXT_GAP + textWidth;
    int groupX = (w - groupWidth) / 2;

    drawXbm(_frame, groupX, Layout::ALARM_Y, image_clock_alarm_bits, 15, 16,
            ICON_WIDTH, ICON_HEIGHT,
            data.alarmEnabled ? t.alarmIconOn : t.alarmIconOff);

    _frame.setTextDatum(top_left);
    _frame.drawString(data.alarmTime, groupX + ICON_WIDTH + ICON_TEXT_GAP, Layout::ALARM_Y);
}

void DisplayManager::drawWeather(int cur, int lo, int hi, int hum) {
    const DisplayTheme& t = theme();
    int w = _frame.width();
    int h = Layout::rowHeight(Layout::WEATHER_SIZE);
    constexpr int ICON_TEXT_GAP = 4;
    constexpr int ITEM_GAP = 14;

    _frame.fillRect(0, Layout::WEATHER_Y - Layout::ROW_PADDING, w, h, t.background);
    _frame.setTextSize(Layout::WEATHER_SIZE);
    _frame.setTextColor(t.textPrimary);

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
    uint16_t iconColors[] = { t.iconTempHot, t.iconTempCold, t.iconTempHot, t.iconHumidity };
    int iconWidths[]  = {16, 10, 10, 16};
    int iconHeights[] = {16, 14, 14, 16};
    bool isTemperature[] = {true, true, true, false};

    int itemWidths[4];
    int totalWidth = ITEM_GAP * 3;
    for (int index = 0; index < 4; ++index) {
        itemWidths[index] = iconWidths[index] + ICON_TEXT_GAP + _frame.textWidth(values[index]);
        if (isTemperature[index]) itemWidths[index] += 8 + _frame.textWidth("C");
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
            _frame.drawCircle(degreeX, Layout::WEATHER_Y + 3, 2, t.textPrimary);
            _frame.drawString("C", degreeX + 5, Layout::WEATHER_Y);
        }

        x += itemWidths[index] + ITEM_GAP;
    }
}

void DisplayManager::drawStatus(const ClockData& data) {
    const DisplayTheme& t = theme();
    int w = _frame.width();
    int h = _frame.height();

    int textHeight = Layout::GLYPH_BASE_HEIGHT * Layout::STATUS_SIZE;
    int y = h - Layout::STATUS_BOTTOM_MARGIN - textHeight; // topo do texto, alinhado ao datum top_center
    int clearY = y - 2;
    int clearH = textHeight + 4;

    _frame.fillRect(0, clearY, w, clearH, t.background);

    if (data.statusLine[0] == '\0') return; // nada a mostrar, só limpa

    _frame.setTextDatum(top_center);
    _frame.setTextColor(data.statusIsWarning ? t.warning : t.textMuted);
    _frame.setTextSize(Layout::STATUS_SIZE);
    _frame.drawString(data.statusLine, w / 2, y);
}
