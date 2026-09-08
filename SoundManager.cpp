#include "SoundManager.h"
#include <driver/i2s.h>
#include <math.h>
#include "AlarmSample.h"
#include "NetLog.h"
#define Serial NetSerial // espelha os logs deste arquivo também via telnet — ver NetLog.h

SoundManager Sound;

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// Padrão de toque do alarme: mesmo timing de antes, só que agora vive
// aqui dentro (a workerTask que executa, checando ringActiveFlag entre
// cada trecho pra poder parar assim que AlarmManager sinalizar).
constexpr uint32_t RING_TIMEOUT_SAFETY_MS = 5UL * 60 * 1000; // nunca toca mais que isso, aconteça o que acontecer

void SoundManager::begin() {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AudioCfg::SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // mono — um único alto-falante
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    // Erros aqui eram ignorados antes — agora logados explicitamente, pra
    // não ficar "achando" que o I2S subiu certo quando na verdade falhou
    // silenciosamente (útil pra diagnosticar problema de alto-falante).
    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Sound] ERRO ao instalar driver I2S: %d\n", (int)err);
    }

    i2s_pin_config_t pins = {
        .bck_io_num = Pins::Audio::BCLK,
        .ws_io_num = Pins::Audio::LRC,
        .data_out_num = Pins::Audio::DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[Sound] ERRO ao configurar pinos I2S: %d\n", (int)err);
    }

    pinMode(Pins::Audio::BUZZER, OUTPUT);
    digitalWrite(Pins::Audio::BUZZER, LOW);

    _requestQueue = xQueueCreate(8, sizeof(Request));
    xTaskCreatePinnedToCore(workerTask, "sound_worker", 4096, this, 1, nullptr, 0);

    _initialized = true;
    Serial.println("[Sound] I2S mono pronto (alto-falante + buzzer), fila de reprodução ativa.");
}

// =====================================================================
// API pública — só enfileira, nunca bloqueia quem chama.
// =====================================================================

void SoundManager::enqueue(const Request& req) {
    if (!_requestQueue) return;
    if (xQueueSend(_requestQueue, &req, 0) != pdTRUE) {
        Serial.println("[Sound] fila de som cheia, pedido descartado");
    }
}

void SoundManager::beepBoot()          { enqueue({Cmd::BootBeep}); }
void SoundManager::playClick()         { enqueue({Cmd::Click}); }
void SoundManager::playSnoozeConfirm() { enqueue({Cmd::SnoozeConfirm}); }
void SoundManager::playAlarmOff()      { enqueue({Cmd::AlarmOff}); }
void SoundManager::playPhantomCigar()  { enqueue({Cmd::PhantomCigar}); }

void SoundManager::requestRingPattern(std::atomic<bool>* activeFlag, bool useBuzzer) {
    Request req{Cmd::Ring};
    req.ringActiveFlag = activeFlag;
    req.ringUseBuzzer = useBuzzer;
    enqueue(req);
}

// =====================================================================
// Worker — única task que de fato toca som, uma coisa de cada vez.
// =====================================================================

void SoundManager::workerTask(void* param) {
    auto* self = static_cast<SoundManager*>(param);
    Request req;

    for (;;) {
        if (xQueueReceive(self->_requestQueue, &req, portMAX_DELAY) == pdTRUE) {
            self->processRequest(req);
        }
    }
}

void SoundManager::processRequest(const Request& req) {
    switch (req.cmd) {
        // Regra: bipes curtos de feedback -> sempre buzzer. Sons "de
        // verdade" (amostra gravada) -> sempre alto-falante. Nada de
        // silêncio via I2S aqui — esses bipes nunca tocam no I2S, então
        // um delay() comum entre eles basta (não tem DMA de alto-falante
        // pra manter alimentado nesses casos).
        case Cmd::Click:
            playBuzzerToneBlocking(1500.0f, 40);
            break;

        case Cmd::BootBeep:
            Serial.println("[Sound] beep de confirmação de boot (buzzer)");
            playBuzzerToneBlocking(1200.0f, 80);
            delay(30);
            playBuzzerToneBlocking(1800.0f, 60);
            break;

        case Cmd::SnoozeConfirm:
            for (int i = 0; i < 3; i++) {
                playBuzzerToneBlocking(1800.0f, 70);
                delay(80);
            }
            break;

        case Cmd::AlarmOff:
            playBuzzerToneBlocking(1600.0f, 120);
            delay(40);
            playBuzzerToneBlocking(700.0f, 180);
            break;

        case Cmd::PhantomCigar:
            Serial.println("[Sound] tocando phantomcigar (alto-falante)...");
            playSampleBlocking(ALARM_SAMPLE_DATA, ALARM_SAMPLE_LEN, ALARM_SAMPLE_RATE);
            Serial.println("[Sound] phantomcigar concluído");
            break;

        case Cmd::Ring: {
            if (!req.ringActiveFlag) break;
            uint32_t startMs = millis();

            while (req.ringActiveFlag->load() && (millis() - startMs) < RING_TIMEOUT_SAFETY_MS) {
                if (req.ringUseBuzzer) {
                    playBuzzerToneBlocking(BuzzerCfg::RING_FREQ_HZ, 150);
                    if (!req.ringActiveFlag->load()) break;
                    delay(120);
                    if (!req.ringActiveFlag->load()) break;
                    playBuzzerToneBlocking(BuzzerCfg::RING_FREQ_HZ, 150);
                    if (!req.ringActiveFlag->load()) break;
                    delay(120);
                    if (!req.ringActiveFlag->load()) break;
                    playBuzzerToneBlocking(BuzzerCfg::RING_FREQ_HZ, 150);
                    if (!req.ringActiveFlag->load()) break;
                    delay(600);
                } else {
                    playSampleBlocking(ALARM_SAMPLE_DATA, ALARM_SAMPLE_LEN, ALARM_SAMPLE_RATE); // ~8s
                    if (!req.ringActiveFlag->load()) break;
                    playSilenceBlocking(300);
                }
            }
            break;
        }
    }
}

// =====================================================================
// Primitivas bloqueantes — só chamadas de dentro da workerTask.
//
// playToneBlocking() não é mais usada por nenhum Cmd no momento (todo
// bipe foi pro buzzer) — deixada aqui de propósito, funcional, caso algum
// dia queira um tom sintetizado saindo do alto-falante de novo.
// =====================================================================

void SoundManager::playToneBlocking(float freqHz, uint32_t durationMs, float amplitude) {
    if (!_initialized) return;

    const int totalSamples = AudioCfg::SAMPLE_RATE * durationMs / 1000;
    const int fadeSamples = min(totalSamples / 4, (int)(AudioCfg::SAMPLE_RATE / 200)); // ~5ms
    const float peakAmp = 32767.0f * amplitude;

    int16_t buffer[128]; // mono — 128 amostras por chunk
    int samplesWritten = 0;
    size_t bytesWritten;

    while (samplesWritten < totalSamples) {
        int chunk = min(128, totalSamples - samplesWritten);
        for (int i = 0; i < chunk; i++) {
            int n = samplesWritten + i;
            float t = (float)n / AudioCfg::SAMPLE_RATE;
            float amp = peakAmp;

            if (n < fadeSamples) amp *= (float)n / fadeSamples;
            else if (n >= totalSamples - fadeSamples) amp *= (float)(totalSamples - n) / fadeSamples;

            buffer[i] = (int16_t)(sinf(2.0f * PI * freqHz * t) * amp);
        }
        i2s_write(I2S_PORT, buffer, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        samplesWritten += chunk;
    }
}

// Escreve silêncio real no I2S (em vez de delay()) — mantém o DMA
// alimentado e evita o degrau de tensão que fica quando o buffer segura
// a última amostra.
void SoundManager::playSilenceBlocking(uint32_t durationMs) {
    if (!_initialized) return;

    const int totalSamples = AudioCfg::SAMPLE_RATE * durationMs / 1000;
    int16_t buffer[128] = {0};
    int samplesWritten = 0;
    size_t bytesWritten;

    while (samplesWritten < totalSamples) {
        int chunk = min(128, totalSamples - samplesWritten);
        i2s_write(I2S_PORT, buffer, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        samplesWritten += chunk;
    }
}

void SoundManager::playSampleBlocking(const uint8_t* samples, size_t count, uint32_t sampleRate) {
    if (!_initialized || samples == nullptr || count == 0) return;

    i2s_set_sample_rates(I2S_PORT, sampleRate);

    int16_t buffer[128];
    size_t written = 0;
    size_t bytesWritten;

    while (written < count) {
        size_t chunk = min((size_t)128, count - written);
        for (size_t i = 0; i < chunk; i++) {
            // 8 bits sem sinal (0..255, 128=silêncio) -> 16 bits com sinal.
            buffer[i] = (int16_t)(((int)samples[written + i] - 128) << 8);
        }
        i2s_write(I2S_PORT, buffer, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }

    i2s_set_sample_rates(I2S_PORT, AudioCfg::SAMPLE_RATE); // restaura a taxa usada por playToneBlocking()
}

void SoundManager::playBuzzerToneBlocking(float freqHz, uint32_t durationMs) {
    if (!_initialized) return;
    tone(Pins::Audio::BUZZER, (uint32_t)freqHz);
    delay(durationMs);
    noTone(Pins::Audio::BUZZER);
}
