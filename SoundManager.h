#pragma once
#include <Arduino.h>
#include <atomic>
#include "config.h"

// =====================================================================
// SoundManager — MAX98357A (alto-falante) + buzzer piezo, MONO.
//
// Tudo que toca som é NÃO-BLOQUEANTE pra quem chama: cada método público
// só enfileira um pedido e retorna na hora; uma única task dedicada
// ("sound_worker") consome a fila e faz o trabalho de fato (que é
// bloqueante — escrever no I2S ou segurar um tone() por alguns
// milissegundos/segundos). Isso existe especificamente pra nunca travar
// quem chama (loop(), botões) esperando um som terminar — antes disso,
// um botão apertado durante um som em andamento (o pior caso era o
// phantomcigar, ~8s) simplesmente não era lido, porque Buttons.update()
// não rodava nesse meio tempo.
//
// Como só UMA task consome a fila, sons nunca se sobrepõem/embaralham no
// I2S ou no buzzer — a serialização é automática, sem precisar de nenhum
// "espera a task anterior terminar" manual (que existia antes só pra
// isso, em AlarmManager).
// =====================================================================
class SoundManager {
public:
    void begin();

    // Beep curto de confirmação no boot (1-2 tons).
    void beepBoot();

    // Clique curto de feedback ao apertar um botão.
    void playClick();

    // "pi-pi-pi": 3 bipes curtos e agudos — confirma que o alarme entrou
    // em modo soneca.
    void playSnoozeConfirm();

    // "pi-po": nota aguda seguida de grave — confirma que o alarme foi
    // desligado (dispensado ou desligado sozinho).
    void playAlarmOff();

    // Toca a amostra "phantomcigar" embutida (AlarmSample.h) no alto-falante.
    void playPhantomCigar();

    // Padrão de toque de alarme, repetindo até activeFlag virar false (ou
    // até um limite de segurança interno). useBuzzer escolhe buzzer vs
    // amostra. Quem chama (AlarmManager) só passa o endereço do seu
    // próprio atomic<bool> de "tocando agora" — a task do SoundManager
    // fica checando esse ponteiro sozinha, sem AlarmManager precisar
    // esperar nada.
    void requestRingPattern(std::atomic<bool>* activeFlag, bool useBuzzer);

private:
    enum class Cmd : uint8_t { Click, BootBeep, SnoozeConfirm, AlarmOff, PhantomCigar, Ring };

    struct Request {
        Cmd cmd;
        std::atomic<bool>* ringActiveFlag = nullptr; // só usado quando cmd==Ring
        bool ringUseBuzzer = false;                   // só usado quando cmd==Ring
    };

    bool _initialized = false;
    QueueHandle_t _requestQueue = nullptr;

    static void workerTask(void* param);
    void processRequest(const Request& req); // bloqueante — só chamado de dentro da workerTask
    void enqueue(const Request& req);

    // Primitivas bloqueantes de baixo nível — privadas de propósito, só a
    // própria workerTask pode chamar (garante que nunca duas coisas
    // escrevem no I2S/buzzer ao mesmo tempo).
    void playToneBlocking(float freqHz, uint32_t durationMs, float amplitude = 0.25f);
    void playSilenceBlocking(uint32_t durationMs);
    void playSampleBlocking(const uint8_t* samples, size_t count, uint32_t sampleRate);
    void playBuzzerToneBlocking(float freqHz, uint32_t durationMs);
};

extern SoundManager Sound;
