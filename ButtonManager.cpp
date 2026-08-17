#include "ButtonManager.h"

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

uint8_t ButtonManager::pinFor(ButtonId buttonId) const {
    switch (buttonId) {
        case ButtonId::Btn1: return Pins::BTN_1;
        case ButtonId::Btn2: return Pins::BTN_2;
        case ButtonId::Btn3: return Pins::BTN_3;
        case ButtonId::Btn4: return Pins::BTN_4;
        case ButtonId::Btn5: return Pins::BTN_5;
        default: return 255;
    }
}

bool ButtonManager::readPressedState(uint8_t pin) const {
    if (pin == 255) {
        return false;
    }

    const int raw = digitalRead(pin);
    return BUTTON_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

void ButtonManager::begin() {
    const uint8_t pins[BUTTON_COUNT] = {
        Pins::BTN_1,
        Pins::BTN_2,
        Pins::BTN_3,
        Pins::BTN_4,
        Pins::BTN_5
    };

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        states[i].pin = pins[i];
        states[i].rawState = false;
        states[i].stableState = false;
        states[i].clicked = false;
        states[i].released = false;
        states[i].lastToggleMs = millis();

        if (states[i].pin == 255) {
            Serial.printf("[ButtonManager] BTN_%d not configured. Update config.h before use.\n", i + 1);
            continue;
        }

        pinMode(states[i].pin, INPUT_PULLUP);
        states[i].rawState = readPressedState(states[i].pin);
        states[i].stableState = states[i].rawState;
    }

    initialized = true;
}

void ButtonManager::update() {
    if (!initialized) {
        return;
    }

    const uint32_t now = millis();

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        ButtonState& state = states[i];

        if (state.pin == 255) {
            state.clicked = false;
            state.released = false;
            continue;
        }

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
    if (index >= BUTTON_COUNT) {
        return false;
    }

    return states[index].stableState;
}

bool ButtonManager::wasClicked(ButtonId buttonId) {
    const uint8_t index = static_cast<uint8_t>(buttonId);
    if (index >= BUTTON_COUNT) {
        return false;
    }

    const bool result = states[index].clicked;
    states[index].clicked = false;
    return result;
}

bool ButtonManager::wasReleased(ButtonId buttonId) {
    const uint8_t index = static_cast<uint8_t>(buttonId);
    if (index >= BUTTON_COUNT) {
        return false;
    }

    const bool result = states[index].released;
    states[index].released = false;
    return result;
}

bool ButtonManager::button1Pressed() const { return isPressed(ButtonId::Btn1); }
bool ButtonManager::button2Pressed() const { return isPressed(ButtonId::Btn2); }
bool ButtonManager::button3Pressed() const { return isPressed(ButtonId::Btn3); }
bool ButtonManager::button4Pressed() const { return isPressed(ButtonId::Btn4); }
bool ButtonManager::button5Pressed() const { return isPressed(ButtonId::Btn5); }

bool ButtonManager::button1Clicked() { return wasClicked(ButtonId::Btn1); }
bool ButtonManager::button2Clicked() { return wasClicked(ButtonId::Btn2); }
bool ButtonManager::button3Clicked() { return wasClicked(ButtonId::Btn3); }
bool ButtonManager::button4Clicked() { return wasClicked(ButtonId::Btn4); }
bool ButtonManager::button5Clicked() { return wasClicked(ButtonId::Btn5); }

bool ButtonManager::button1Released() { return wasReleased(ButtonId::Btn1); }
bool ButtonManager::button2Released() { return wasReleased(ButtonId::Btn2); }
bool ButtonManager::button3Released() { return wasReleased(ButtonId::Btn3); }
bool ButtonManager::button4Released() { return wasReleased(ButtonId::Btn4); }
bool ButtonManager::button5Released() { return wasReleased(ButtonId::Btn5); }
