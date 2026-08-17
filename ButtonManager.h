#pragma once

#include <Arduino.h>
#include "config.h"

enum class ButtonId : uint8_t {
    Btn1 = 0,
    Btn2 = 1,
    Btn3 = 2,
    Btn4 = 3,
    Btn5 = 4,
    Count = 5
};

class ButtonManager {
public:
    ButtonManager();

    void begin();
    void update();

    bool isPressed(ButtonId buttonId) const;
    bool wasClicked(ButtonId buttonId);
    bool wasReleased(ButtonId buttonId);

    bool button1Pressed() const;
    bool button2Pressed() const;
    bool button3Pressed() const;
    bool button4Pressed() const;
    bool button5Pressed() const;

    bool button1Clicked();
    bool button2Clicked();
    bool button3Clicked();
    bool button4Clicked();
    bool button5Clicked();

    bool button1Released();
    bool button2Released();
    bool button3Released();
    bool button4Released();
    bool button5Released();

private:
    struct ButtonState {
        uint8_t pin;
        bool rawState;
        bool stableState;
        bool clicked;
        bool released;
        uint32_t lastToggleMs;
    };

    static constexpr uint8_t BUTTON_COUNT = static_cast<uint8_t>(ButtonId::Count);
    static constexpr uint32_t DEBOUNCE_MS = 30u;

    ButtonState states[BUTTON_COUNT];
    bool initialized;

    uint8_t pinFor(ButtonId buttonId) const;
    bool readPressedState(uint8_t pin) const;
};
