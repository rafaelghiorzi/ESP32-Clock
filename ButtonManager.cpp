#include "ButtonManager.h"
#include "NetLog.h"
#define Serial NetSerial // espelha os logs deste arquivo também via telnet — ver NetLog.h

ButtonManager Buttons;

namespace {
constexpr bool BUTTON_ACTIVE_LOW = true;
}

ButtonManager::ButtonManager()
    : initialized(false) {
    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        states[i].pin = 255;
        states[i].rawState = false;
        states[i].stableState = false;
        states[i].clicked = false;
        states[i].released = false;
        states[i].lastToggleMs = 0;
    }
}

bool ButtonManager::readPressedState(uint8_t pin) const {
    if (pin == 255) return false;
    const int raw = digitalRead(pin);
    return BUTTON_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

void ButtonManager::begin() {
    const uint8_t pins[BUTTON_COUNT] = {
        Pins::Buttons::BTN1,
        Pins::Buttons::BTN2,
        Pins::Buttons::BTN3,
        Pins::Buttons::BTN4,
        Pins::Buttons::BTN5
    };

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        states[i].pin = pins[i];
        states[i].rawState = false;
        states[i].stableState = false;
        states[i].clicked = false;
        states[i].released = false;
        states[i].lastToggleMs = millis();

        pinMode(states[i].pin, INPUT_PULLUP);
        states[i].rawState = readPressedState(states[i].pin);
        states[i].stableState = states[i].rawState;
    }

    initialized = true;
    Serial.println("[Buttons] pronto (5 botões, debounce 30ms)");
}

void ButtonManager::update() {
    if (!initialized) return;

    const uint32_t now = millis();

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        ButtonState& state = states[i];
        const bool rawPressed = readPressedState(state.pin);

        if (rawPressed != state.rawState) {
            state.rawState = rawPressed;
            state.lastToggleMs = now;
        }

        if ((now - state.lastToggleMs) >= DEBOUNCE_MS) {
            if (rawPressed != state.stableState) {
                state.stableState = rawPressed;
                if (rawPressed) {
                    state.clicked = true;
                    state.released = false;
                } else {
                    state.clicked = false;
                    state.released = true;
                }
            }
        }
    }
}

bool ButtonManager::isPressed(ButtonId buttonId) const {
    const uint8_t index = static_cast<uint8_t>(buttonId);
    if (index >= BUTTON_COUNT) return false;
    return states[index].stableState;
}

bool ButtonManager::wasClicked(ButtonId buttonId) {
    const uint8_t index = static_cast<uint8_t>(buttonId);
    if (index >= BUTTON_COUNT) return false;
    const bool result = states[index].clicked;
    states[index].clicked = false;
    return result;
}

bool ButtonManager::wasReleased(ButtonId buttonId) {
    const uint8_t index = static_cast<uint8_t>(buttonId);
    if (index >= BUTTON_COUNT) return false;
    const bool result = states[index].released;
    states[index].released = false;
    return result;
}

bool ButtonManager::button1Clicked() { return wasClicked(ButtonId::Btn1); }
bool ButtonManager::button2Clicked() { return wasClicked(ButtonId::Btn2); }
bool ButtonManager::button3Clicked() { return wasClicked(ButtonId::Btn3); }
bool ButtonManager::button4Clicked() { return wasClicked(ButtonId::Btn4); }
bool ButtonManager::button5Clicked() { return wasClicked(ButtonId::Btn5); }
