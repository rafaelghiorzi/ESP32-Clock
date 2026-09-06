#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "config.h"

// =====================================================================
// AlarmManager — até 5 alarmes, cada um com dias da semana + "repete".
//
// Um alarme com repeat=true é recorrente (toca em todo dia marcado, toda
// semana). Um alarme com repeat=false funciona como timer/despertador de
// uso único: toca na próxima ocorrência de algum dia marcado e se
// desliga sozinho (enabled=false) depois de disparar — não precisa
// desmarcar manualmente.
//
// Persistência em NVS (Preferences) — os alarmes sobrevivem a reset/
// queda de energia. Escreve só o slot que mudou (não os 5 de uma vez),
// já que updates via web são raros e cada escrita na NVS gasta um ciclo
// de flash.
//
// Máquina de estados do toque (tudo isso roda só no core 1, disparado por
// update()/handleButton4()/handleButton5() chamados do loop() — por isso
// os campos de tempo/estado do soneca abaixo NÃO são atomic, só _ringing/
// _ringingIndex são, porque a ringTask no core 0 só lê esses dois):
//
//   disparo -> toca até 3 min -----+-- BTN4 a qualquer momento -> desliga (pi-po)
//                                  |
//                                  +-- BTN5 (1a vez) -> soneca (pi-pi-pi + aviso na tela)
//                                  |
//                                  +-- ninguém aperta nada em 3 min -> soneca automática
//
//   soneca (5 min, silencioso) -> toca de novo até 3 min --+-- BTN4 -> desliga (pi-po)
//                                                           +-- BTN5 (2a vez) -> desliga (pi-po)
//                                                           +-- ninguém aperta nada -> desliga sozinho
//
// Concorrência: `_alarms` é lido por update() (chamado do loop(), core 1)
// e escrito pelo WebManager (rodando numa task no core 0) -> protegido
// por mutex, igual ao padrão já usado pra clima em ConnManager.
//
// Limitação conhecida: se dois alarmes caírem no mesmo minuto exato, só
// o primeiro encontrado toca (evita sobrepor dois padrões de som). Caso
// de uso raríssimo pra um relógio de mesa; não vale a complexidade de
// enfileirar toques.
// =====================================================================

struct Alarm {
    bool    enabled  = false;
    bool    repeat   = true;   // true=recorrente, false=toca uma vez e desliga sozinho
    uint8_t hour     = 7;
    uint8_t minute   = 0;
    uint8_t daysMask = 0;      // bit0=domingo .. bit6=sábado (bate com tm_wday)
    char    label[16] = "Alarme";
};

class AlarmManager {
public:
    void begin();

    // Chame 1x/segundo do loop(). Faz duas coisas, sempre nessa ordem:
    // checa se algum alarme deve disparar agora (throttle interno de 1
    // checagem por minuto) e avança a máquina de estados de toque/soneca
    // (essa parte roda every tick, sem throttle, pros timeouts de 3/5 min
    // ficarem precisos).
    void update();

    // Botões físicos, só fazem algo enquanto isRinging()==true.
    void handleButton4(); // sempre desliga (pi-po)
    void handleButton5(); // 1a vez: soneca (pi-pi-pi). 2a vez: desliga (pi-po)

    void dismissActive(); // usado pela página web ("Parar") — igual ao BTN4
    bool isRinging() const;
    String getRingingLabel() const; // label do alarme tocando agora, ou "" se nenhum

    // Mensagem transiente pra mostrar na tela (ex.: "Toque em 5 minutos!").
    // Retorna false quando a janela de exibição já expirou.
    bool getMessage(String& out) const;

    static constexpr uint8_t MAX_ALARMS = 5;
    uint8_t count() const { return MAX_ALARMS; }

    Alarm get(uint8_t index) const;             // cópia segura (mutex)
    bool  set(uint8_t index, const Alarm& alarm); // valida, grava em memória + NVS

    // millis() da última vez que um alarme foi criado/editado pela web, ou
    // 0 se nunca (desde o boot). Usado junto com o timestamp de clima do
    // ConnManager pro indicador "sincronizado há Xmin" da tela.
    uint32_t getLastChangeMs() const;

    // Para a linha de "Alarme HH:MM" da tela (Etapa 5): o próximo alarme
    // habilitado que vai disparar dali pra frente, olhando até 7 dias.
    struct NextAlarmInfo {
        bool    any    = false;
        uint8_t hour   = 0;
        uint8_t minute = 0;
    };
    NextAlarmInfo getNextAlarm() const;

private:
    void loadFromNVS();
    void saveToNVS(uint8_t index);
    void startRinging(uint8_t index);
    static void ringTask(void* param);

    void enterSnooze();     // para o som, agenda novo toque em 5min, toca pi-pi-pi, seta mensagem
    void dismissRinging();  // para o som de vez, toca pi-po
    void waitForRingTaskToStop(); // espera (bounded) a ringTask sair antes de tocar pi-pi-pi/pi-po
    void setMessage(const char* text, uint32_t durationMs);

    Alarm _alarms[MAX_ALARMS];
    SemaphoreHandle_t _mutex = nullptr;
    Preferences _prefs;

    std::atomic<bool>   _ringing{false};       // som tocando agora — única coisa que a ringTask (core 0) lê
    std::atomic<bool>   _ringTaskRunning{false}; // true enquanto a ringTask ainda não saiu do laço
    std::atomic<int8_t> _ringingIndex{-1};
    TaskHandle_t _ringTaskHandle = nullptr;

    // set() é chamado pela task do WebManager (core 0); lido do loop() no
    // core 1 pro indicador de status -> atomic.
    std::atomic<uint32_t> _lastChangeMs{0};

    // Só tocados no core 1 (loop()) — ver comentário de concorrência acima.
    bool     _snoozing = false;      // true durante os 5 min de espera silenciosa do soneca
    bool     _snoozeUsed = false;    // true depois que o soneca já foi usado neste ciclo de disparo
    uint32_t _ringStartMs = 0;
    uint32_t _snoozeStartMs = 0;

    char     _messageBuf[24] = "";
    uint32_t _messageUntilMs = 0;

    int _lastCheckedMinute = -1;
};

extern AlarmManager Alarms;
