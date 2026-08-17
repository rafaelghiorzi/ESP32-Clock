#include "SoundManager.h"
#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t DEFAULT_SAMPLE_RATE = 22050u;
}

SoundManager::SoundManager()
    : initialized(false), sampleRate(DEFAULT_SAMPLE_RATE) {
}

uint16_t SoundManager::lookupNoteFrequency(char noteName, uint8_t octave) {
    static const uint8_t noteOffsets[7] = {0, 2, 4, 5, 7, 9, 11};
    static const char noteNames[7] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};

    char normalized = static_cast<char>(toupper(noteName));
    if (normalized < 'A' || normalized > 'G') {
        return 0;
    }

    uint8_t index = 0;
    while (index < 7 && noteNames[index] != normalized) {
        ++index;
    }
    if (index >= 7) {
        return 0;
    }

    int midi = (static_cast<int>(octave) + 1) * 12 + noteOffsets[index];
    return static_cast<uint16_t>(round(440.0f * pow(2.0f, (midi - 69) / 12.0f)));
}

void SoundManager::begin() {
    if (initialized) {
        return;
    }

    pinMode(Pins::I2S_SD, OUTPUT);
    digitalWrite(Pins::I2S_SD, HIGH);
    pinMode(Pins::I2S_GAIN, OUTPUT);
    digitalWrite(Pins::I2S_GAIN, HIGH);

    i2s_config_t config;
    memset(&config, 0, sizeof(config));
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = sampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STANDARDS;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins;
    memset(&pins, 0, sizeof(pins));
    pins.bck_io_num = Pins::I2S_BCLK;
    pins.ws_io_num = Pins::I2S_LRC;
    pins.data_out_num = Pins::I2S_DIN;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[SoundManager] i2s_driver_install failed: %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) {
        Serial.printf("[SoundManager] i2s_set_pin failed: %d\n", err);
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }

    i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    initialized = true;
}

void SoundManager::stop() {
    if (!initialized) {
        return;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
}

void SoundManager::playTone(uint16_t frequencyHz, uint16_t durationMs, int16_t amplitude) {
    if (!initialized || frequencyHz == 0 || durationMs == 0) {
        return;
    }

    const uint32_t totalSamples = (sampleRate * durationMs) / 1000UL;
    const uint32_t period = sampleRate / frequencyHz;
    const uint32_t halfPeriod = period / 2UL;

    int16_t buffer[128];
    size_t written = 0;

    for (uint32_t i = 0; i < totalSamples; ++i) {
        const uint32_t phase = (i % period);
        const int16_t sample = (phase < halfPeriod) ? amplitude : -amplitude;
        buffer[i % 128] = sample;

        if (((i + 1) % 128) == 0 || i == (totalSamples - 1)) {
            const size_t chunkBytes = ((i % 128) + 1) * sizeof(int16_t);
            i2s_write(I2S_NUM_0, buffer, chunkBytes, &written, portMAX_DELAY);
        }
    }
}

void SoundManager::playClick() {
    playTone(2000, 60, 7000);
    delay(25);
    playTone(2600, 45, 5000);
}

void SoundManager::playRtttl(const char* tune) {
    if (tune == nullptr || *tune == '\0') {
        playClick();
        return;
    }

    const char* cursor = tune;
    while (*cursor != '\0' && *cursor != ':') {
        ++cursor;
    }

    if (*cursor != ':') {
        playClick();
        return;
    }

    const char* notes = cursor + 1;
    uint8_t octave = 6;
    uint8_t duration = 4;
    uint16_t bpm = 63;

    // Minimal RTTTL parsing. We ignore the header metadata and play a small demo
    // melody if the string contains simple note sequences like "8c6,8g,8a,8g".
    (void)octave;
    (void)duration;
    (void)bpm;

    const char* token = notes;
    for (uint8_t index = 0; index < 8; ++index) {
        while (*token == ',' || *token == ' ' || *token == '\t') {
            ++token;
        }

        if (*token == '\0') {
            break;
        }

        char note = *token;
        uint8_t noteDuration = duration;
        uint8_t noteOctave = octave;

        if (note >= '1' && note <= '9') {
            noteDuration = static_cast<uint8_t>(note - '0');
            ++token;
            note = *token;
        }

        if (note == 'p' || note == 'P') {
            delay((60000 / bpm) / noteDuration);
            ++token;
            continue;
        }

        if (note >= 'A' && note <= 'G') {
            if (token[1] == '#') {
                ++token;
            }
            if (token[1] >= '0' && token[1] <= '9') {
                noteOctave = static_cast<uint8_t>(token[1] - '0');
                ++token;
            }

            uint16_t frequency = lookupNoteFrequency(note, noteOctave);
            if (frequency > 0) {
                playTone(frequency, (60000 / bpm) / noteDuration, 5000);
            }
        }

        while (*token != '\0' && *token != ',') {
            ++token;
        }
        if (*token == ',') {
            ++token;
        }
    }
}
