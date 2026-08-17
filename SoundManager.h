#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

class SoundManager {
public:
    SoundManager();

    void begin();
    void stop();
    void playTone(uint16_t frequencyHz, uint16_t durationMs = 120, int16_t amplitude = 7000);
    void playClick();
    void playRtttl(const char* tune);

private:
    bool initialized;
    uint32_t sampleRate;

    static uint16_t lookupNoteFrequency(char noteName, uint8_t octave);
};
