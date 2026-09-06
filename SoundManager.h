#pragma once
#include <Arduino.h>
#include "config.h"

// =====================================================================
// SoundManager — MAX98357A via I2S, MONO (um único alto-falante).
// Lógica de geração de onda (fade in/out ~5ms) adaptada do sketch de
// bancada já validado no hardware novo (limpo, sem estouro) — lá o teste
// escrevia estéreo (L+R) só porque era um teste de bancada; aqui escreve
// um único canal.
// =====================================================================
class SoundManager {
public:
    void begin();

    // Beep curto de confirmação no boot (1-2 tons) — não a melodia em
    // loop infinito, que era só teste de bancada.
    void beepBoot();

    // Clique curto de feedback ao apertar um botão (Etapa 3).
    void playClick();

    // "pi-pi-pi": 3 bipes curtos e agudos — confirma que o alarme entrou
    // em modo soneca.
    void playSnoozeConfirm();

    // "pi-po": nota aguda seguida de grave — confirma que o alarme foi
    // desligado (dispensado ou desligado sozinho).
    void playAlarmOff();

    // Toca um tom com fade-in/fade-out (~5ms) para não saltar de fase
    // abruptamente nas bordas (evita clique de início/fim de nota).
    // amplitude vai de 0.0 a 1.0 (fração do range de int16_t).
    void playTone(float freqHz, uint32_t durationMs, float amplitude = 0.25f);

    // Toca uma amostra PCM crua de 8 bits sem sinal e mono (o formato de
    // dado de um WAV 8-bit comum, sem o cabeçalho) — cada byte vai de 0 a
    // 255 com 128 = silêncio. Troca a taxa de amostragem do I2S por
    // sampleRate durante a reprodução e restaura AudioCfg::SAMPLE_RATE
    // no final, então não interfere no playTone()/beepBoot() depois.
    void playSample(const uint8_t* samples, size_t count, uint32_t sampleRate);

private:
    bool _initialized = false;

    void playSilence(uint32_t durationMs);
};

extern SoundManager Sound;
