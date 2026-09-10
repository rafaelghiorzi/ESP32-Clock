# ESP32 Clock

Relógio de mesa inteligente baseado em ESP32-S3, com display TFT, hora sincronizada via NTP+RTC, clima via API, controle de luzes Yeelight, até 5 alarmes configuráveis por uma página web própria, buzzer e alto-falante, brilho automático dia/noite, e atualização de firmware/logs por WiFi.

**Status: v1.1 — funcional, testado em hardware real. Pinout remapeado pra montagem em placa perfurada (veja o changelog no fim deste README).**

---

## Sumário

- [Hardware](#hardware)
- [Pinout](#pinout)
- [Fiação — pontos de atenção](#fiação--pontos-de-atenção)
- [Configuração da Arduino IDE](#configuração-da-arduino-ide)
- [Bibliotecas necessárias](#bibliotecas-necessárias)
- [Estrutura do projeto](#estrutura-do-projeto)
- [Primeira gravação](#primeira-gravação)
- [Funcionalidades](#funcionalidades)
- [Página web de alarmes](#página-web-de-alarmes)
- [Atualização por WiFi (OTA)](#atualização-por-wifi-ota)
- [Logs remotos](#logs-remotos)
- [Configuração (`config.h`)](#configuração-configh)
- [Arquitetura / decisões de design](#arquitetura--decisões-de-design)
- [Problemas conhecidos e diagnóstico](#problemas-conhecidos-e-diagnóstico)
- [Código legado](#código-legado)

---

## Hardware

| Componente | Função |
|---|---|
| ESP32-S3 N16R8 (16MB flash, 8MB PSRAM octal) | Microcontrolador principal |
| Display ILI9341 2.4"/2.8" (SPI, sem touch) | Mostra data, hora, alarme e clima |
| MOSFET IRLZ44N (canal N) | Chaveia o backlight do display via PWM (brilho automático dia/noite) |
| MAX98357A (amplificador I2S Classe D) + alto-falante | Áudio (amostra de alarme) |
| Buzzer piezo passivo 5V (12mm) | Bipes de feedback e padrão de toque do alarme |
| DS3231 (RTC, I2C, com bateria CR2032) | Mantém a hora certa mesmo sem WiFi/energia |
| 5x botões (push-button momentâneo) | Controle físico (luzes, cenas, snooze/desligar alarme) |
| Fonte 5V, ≥2A (idealmente 5A de headroom) | Alimentação — ver nota sobre decoupling abaixo |
| Capacitores de desacoplamento (cerâmico 100-220nF + eletrolítico 100-470µF) | Nos pinos de alimentação do ESP32, do display e do MAX98357A — mitiga flicker causado por picos de corrente do rádio WiFi |
| Resistores 100Ω (buzzer/gate) e 10kΩ (pull-down do gate do MOSFET) | Suporte aos circuitos acima |

Veja o histórico de commits e a conversa de desenvolvimento para o racional completo de cada escolha de componente — este README foca em "como usar/manter", não em "por que decidimos isso".

## Pinout

Placa: **ESP32-S3 N16R8**. Pinos reservados que **nunca** devem ser usados para periféricos: `GPIO0, 3, 45, 46` (strapping), `GPIO26-32` (flash SPI interno), `GPIO33-37` (PSRAM octal), `GPIO19, 20` (USB nativo), `GPIO43, 44` (UART0, evitados por precaução).

| Periférico | Sinal | GPIO |
|---|---|---|
| **Display ILI9341** | CS | 4 |
| | RESET | 5 |
| | DC | 6 |
| | MOSI (SDI) | 7 |
| | SCK | 15 |
| | MISO | não conectado |
| | Backlight (via MOSFET) | 16 |
| **MAX98357A (áudio)** | LRC (WS) | 9 |
| | BCLK | 10 |
| | DIN | 11 |
| | VIN | 5V (obrigatório) |
| | GAIN | 5V (fixo, ganho 6dB) |
| | SD | flutuando (mono) |
| **Buzzer piezo** | Base do transistor (via 4.7kΩ) | 47 |
| **DS3231 (RTC, I2C)** | SDA | 1 |
| | SCL | 2 |
| **Botões** | BTN1 | 38 |
| | BTN2 | 39 |
| | BTN3 | 40 |
| | BTN4 | 41 |
| | BTN5 | 42 |

### Mapeamento dos botões (fora do alarme tocando)

| Botão | Ação |
|---|---|
| BTN1 | Liga/desliga luz Yeelight "bedside" |
| BTN2 | Liga/desliga luz Yeelight "teto" |
| BTN3 | Cicla entre 4 cenas de iluminação pré-definidas |
| BTN4 | Bipe de teste |
| BTN5 | Toca a amostra de áudio (teste do alto-falante) |

Quando um alarme está tocando, os botões mudam de função: **BTN5** ativa soneca na primeira vez e desliga na segunda; **qualquer outro botão** desliga o alarme direto.

## Fiação — pontos de atenção

- **Backlight**: o MOSFET IRLZ44N fica em série entre o 3.3V e o pino "LED" do display (não entre o backlight e o GND — a maioria dos módulos ILI9341 de 8 pinos tem o retorno do backlight compartilhado com o GND geral do módulo). Dreno no 3.3V, fonte no pino "LED", gate recebe o GPIO16 através de um resistor de 100Ω, com um pull-down de 10kΩ entre gate e GND (evita flash aleatório do backlight no boot, antes do firmware configurar o pino).
- **Buzzer**: driver a transistor NPN (MPS2222/2N2222) pra ganhar volume — o buzzer fica entre **5V** e o **Coletor**, o **Emissor** vai pro GND, e o GPIO47 aciona a **Base** através de um resistor de 4.7kΩ. O GPIO só controla o transistor (que chaveia os 5V), não alimenta o buzzer diretamente — dá uma oscilação bem maior no buzzer do que os 3.3V de um GPIO puro conseguiriam. É um piezo **passivo** (precisa de sinal de frequência variável — um buzzer ativo não serve aqui).
- **Desacoplamento de energia**: um capacitor cerâmico (100-220nF) **e** um eletrolítico (100-470µF) **em paralelo** (cada um com uma perna no trilho de VIN/3V3 e a outra no trilho de GND — nunca um capacitor "atrás" do outro em série) o mais perto fisicamente possível dos pinos de alimentação de cada módulo (ESP32, display, MAX98357A). Isso existe especificamente para mitigar o flicker do display causado por picos de corrente de transmissão do rádio WiFi (~300-500mA em transições de microssegundos) — sem esse desacoplamento, o backlight (ligado direto na trilha de 3.3V) pisca visivelmente quando o WiFi transmite.
- **DS3231 VCC**: puxe um fio **isolado** direto do 3.3V até o VCC do módulo — não precisa (e não deve) passar pelo pino RX ou por qualquer outro pino no caminho; fio isolado pode cruzar por cima de outros pinos sem risco de curto.
- **Aterramento único**: todos os GNDs (fonte, ESP32, display, MAX98357A, DS3231, buzzer) devem se encontrar num único ponto comum.
- Terminais de parafuso (ex: saída de alto-falante do MAX98357A) são um ponto clássico de falha intermitente — prefira estanhar a ponta do fio antes de prender no parafuso, e solde por cima se o parafuso continuar instável.

## Configuração da Arduino IDE

| Campo | Valor |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | qualquer opção de 16MB com ~3MB de APP |
| Flash Mode | QIO 80MHz |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |
| USB Mode | Hardware CDC and JTAG |
| JTAG Adapter | Disabled |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Events Run On / Arduino Runs On | Core 1 |
| Upload Speed | 921600 |

## Bibliotecas necessárias

Instaláveis pelo Library Manager da Arduino IDE (nenhuma precisa de versão fixa específica, mas todas foram testadas com o core arduino-esp32 3.x):

- **LovyanGFX** — driver do display
- **RTClib** (Adafruit) — DS3231
- **ArduinoJson** (v6 ou v7) — parsing do clima e das APIs da página web
- **WebSockets** (Markus Sattler / Links2004) — push de status pra página web sem polling

Tudo o resto (`WiFi`, `WebServer`, `ESPmDNS`, `ArduinoOTA`, `Preferences`, `driver/i2s.h`, `esp_task_wdt.h`) já vem embutido no core arduino-esp32 — nenhuma instalação extra.

## Estrutura do projeto

```
ESP32_Clock.ino       setup()/loop() — orquestra todos os managers
config.h              TODOS os pinos e constantes de configuração do projeto
AlarmManager.*         5 alarmes, máquina de estados de toque/soneca, NVS
ButtonManager.*        Leitura debounced dos 5 botões
ConnManager.*          WiFi, fetch de clima, controle Yeelight, OTA
DisplayManager.*       Driver do display (LovyanGFX), UI, temas de cor, backlight PWM
SoundManager.*         Fila de reprodução de som (buzzer + alto-falante), nunca bloqueia
TimeManager.*          NTP + DS3231, timezone
WebManager.*           Servidor HTTP (página de alarmes) + WebSocket
NetLog.*                Espelha os logs também via telnet (porta 23)
AlarmSample.h           Amostra de áudio PCM embutida (~62KB, "phantomcigar")
tools/                  Scripts Python pra capturar log (USB e rede) em arquivo
deprecated/              Código da versão anterior do projeto, mantido só como referência (não compila)
```

Cada `Manager` é uma classe com uma única instância global (`Conn`, `Display`, `Sound`, `Alarms`, `RtcClock`, `Buttons`, `Web`), definida no próprio `.cpp` e declarada `extern` no `.h`. Serviços de rede/background rodam em tasks FreeRTOS pinadas ao **core 0**; `loop()` e a UI ficam no **core 1**.

## Primeira gravação

1. Abra `ESP32_Clock.ino` na Arduino IDE (a pasta do sketch precisa se chamar `ESP32_Clock`, igual ao arquivo principal).
2. Instale as bibliotecas listadas acima.
3. Ajuste `config.h`: `WifiCfg::SSID`/`PASSWORD`, os IPs em `Yeelight::`, e `OtaCfg::PASSWORD` (troque o valor padrão).
4. Configure a Arduino IDE conforme a tabela acima.
5. Grave por USB (uploads seguintes podem ser feitos por WiFi — veja OTA abaixo).
6. Abra o Serial Monitor a 115200 baud — o boot loga o motivo do reset anterior, status de WiFi/RTC/clima, e o IP assim que conectar.

Na primeira vez sem alarmes configurados, o display mostra "--:--" na linha de alarme — configure ao menos um pela página web (endereço no próximo tópico).

## Funcionalidades

- **Relógio**: data e hora sempre visíveis, atualizadas 1x/minuto; RTC (DS3231) mantém a hora certa mesmo sem WiFi, com correção via NTP a cada 6h (retry rápido a cada 5min se falhar).
- **Clima**: temperatura atual, mínima, máxima e umidade via Open-Meteo (sem API key), atualizado a cada 10 minutos.
- **5 alarmes**, cada um com: horário, dias da semana, recorrente ou disparo único (funciona como timer), soneca configurável (1-30 min), som configurável (amostra ou buzzer), opção de ligar as luzes junto.
- **Máquina de estados do alarme**: toca até 3 min → soneca automática (1x) de duração configurável → toca de novo até 3 min → desliga sozinho. BTN4 desliga a qualquer momento; BTN5 ativa soneca na 1ª vez, desliga na 2ª. Alarmes que colidem entre si se cancelam mutuamente (nunca sobrepõe som).
- **Controle de luzes Yeelight** (2 lâmpadas, IPs configuráveis) — toggle direto pelos botões, ou 4 cenas de iluminação pré-definidas, ou um preset "ligar luzes" (amarelado, brilho médio) disparável manualmente ou junto com um alarme.
- **Brilho automático do backlight** — 100% das 6h às 21h, ~8% das 21h às 6h (ajustável em `config.h`).
- **4 temas de cor** no display (Escuro, Claro, Noite, Nascer do Sol), trocáveis pela página web.
- **Indicador de status** no rodapé da tela: "Sincronizado há Xmin" (atualiza quando o clima ou os alarmes mudam), substituído por um aviso de bateria fraca do RTC quando aplicável.
- **Áudio não-bloqueante**: todo som (buzzer ou alto-falante) passa por uma fila com uma única task dedicada — apertar um botão nunca fica "travado" esperando um som anterior terminar.
- **Robustez**: Task Watchdog Timer no loop principal, monitoramento de heap livre com reboot preventivo, log do motivo do último reset, reconexão WiFi com backoff exponencial.

## Página web de alarmes

Acesse por `http://esp32clock.local/` (mDNS) ou pelo IP mostrado no Serial Monitor no boot. Permite, por alarme: horário, dias da semana, ativo/inativo, recorrente, duração da soneca, som (amostra/buzzer), opção de ligar luzes, e um botão de teste "Ligar luzes". A página recebe atualizações de status (hora, alarme tocando) via WebSocket, sem precisar recarregar.

## Atualização por WiFi (OTA)

Depois da primeira gravação por USB, uploads seguintes podem ser feitos por WiFi: na Arduino IDE, **Tools → Port**, escolha a porta de rede (`esp32clock at <IP>`) em vez da porta USB, e faça upload normalmente — vai pedir a senha configurada em `OtaCfg::PASSWORD`.

## Logs remotos

Além do Serial Monitor (USB), os mesmos logs são espelhados por telnet na porta 23 (só depois que o WiFi conectar — antes disso, só USB mesmo). Duas formas de acessar:

- Direto por telnet/PuTTY: `telnet esp32clock.local 23`
- Scripts prontos em `tools/`, que salvam os logs num arquivo com timestamp:
  ```bash
  python tools/capture_net_log.py esp32clock.local   # rede, sem instalar nada
  python tools/capture_usb_log.py COM5                # USB, precisa "pip install pyserial"
  ```

## Configuração (`config.h`)

Todas as constantes ajustáveis do projeto (pinos, credenciais, timings, valores de PWM/áudio) vivem em `config.h`, organizadas por namespace (`Pins`, `WifiCfg`, `ApiCfg`, `Yeelight`, `TimeCfg`, `OtaCfg`, `AlarmCfg`, `BacklightCfg`, `BuzzerCfg`). Comentários em cada bloco explicam o racional dos valores escolhidos. Vale destacar:

- `WifiCfg::TX_POWER` / `POWER_SAVE_MODE` — reduzidos de propósito pra mitigar flicker do display (pico de corrente de TX do rádio).
- `BuzzerCfg::RING_FREQ_HZ` — frequência do padrão de toque no buzzer; piezos têm um pico de volume na frequência de ressonância deles (varia por modelo, geralmente 2-4.5kHz pros de 12mm) — vale testar valores diferentes aqui.
- Credenciais (WiFi, OTA) ficam versionadas em texto puro de propósito, igual a um `.ino` tradicional — **troque a senha padrão do OTA** antes de expor o dispositivo.

## Arquitetura / decisões de design

- **Dois cores, uso deliberado**: tudo que faz I/O de rede ou pode bloquear (WiFi, HTTP, Yeelight, servidor web, NTP, reprodução de som) roda em tasks no **core 0**. O **core 1** só faz `loop()` — botões, cálculo do snapshot da tela, um único `pushSprite()` por tick. Tasks que ficam ociosas esperando (watchdog de WiFi, fila de Yeelight, fila de som) bloqueiam em semáforo/fila em vez de fazer polling, pra manter o core 0 realmente ocioso na maior parte do tempo.
- **Mínimo uso de rede**: clima a cada 10min, NTP a cada 6h — o relógio conta sozinho entre sincronizações.
- **Sem `String` no caminho quente**: os campos de `ClockData` (montados e comparados a cada segundo, indefinidamente) usam buffers `char[]` fixos, não `String`, pra não fragmentar o heap rodando por semanas.
- **Áudio nunca bloqueia**: uma única fila + task consomem todos os pedidos de som (buzzer ou amostra) — os métodos públicos (`playClick()`, `playPhantomCigar()` etc.) só enfileiram e retornam na hora.
- **Dirty-tracking na tela**: o display compara o novo snapshot contra o anterior campo a campo, só redesenha o que mudou, e faz um único `pushSprite()` atômico por tick — sem tearing, sem sobreposição de texto.

## Problemas conhecidos e diagnóstico

- **Flicker do backlight ao ligar/durante uso do WiFi**: mitigado via `TX_POWER`/`POWER_SAVE_MODE` reduzidos em software e pelos capacitores de desacoplamento em hardware. Se ainda notar flicker, confira primeiro se os capacitores estão mesmo instalados perto dos pinos certos.
- **Buzzer baixo (resolvido na v1.1)**: na v1.0 o buzzer era alimentado só pelos 3.3V do GPIO via resistor, sem estágio de amplificação. A v1.1 adicionou um driver a transistor NPN (MPS2222) alimentado em 5V (ver [Fiação](#fiação--pontos-de-atenção)) — se ainda estiver baixo, teste `BuzzerCfg::RING_FREQ_HZ` perto da frequência de ressonância do seu buzzer específico (varia por modelo).
- **"Precisa apertar reset físico depois de desligar/religar a energia"**: sintoma consistente com brownout no power-on (o mesmo problema de fundo do flicker) — o log do motivo do reset (`esp_reset_reason()`, impresso no boot) confirma isso na próxima ocorrência. Fix é hardware (capacitores/fonte), não há solução em software pra esse caso específico (a CPU ainda nem começou a rodar o firmware quando isso acontece).
- **Conexões intermitentes** (som cortando, etc.): comum em terminais de parafuso e jumpers dupont — teste com multímetro em modo continuidade, flexionando cada fio/conexão, até achar o ponto que falha.
- Os 5 alarmes salvos na NVS resetam pro padrão sempre que a struct `Alarm` muda de tamanho entre versões do firmware (não há migração automática) — normal ter que reconfigurar após atualizar o firmware se o changelog mencionar mudança nos campos do alarme.

## Código legado

A pasta `deprecated/` contém a versão anterior completa do firmware (antes da reescrita), com todos os arquivos renomeados pra extensão `.txt` de propósito — o Arduino compila recursivamente qualquer `.ino`/`.c`/`.cpp` dentro da pasta do sketch, então essa extensão evita que esses arquivos colidam com o código atual (que reusa os mesmos nomes de classe). Serve só como referência histórica, não é compilado.

## Changelog

### v1.0 → v1.1 — pinout remapeado pra montagem em placa perfurada

Mudança só de hardware/pinout (nenhuma pino de v1.0 tinha sido soldado ainda) — motivada por layout físico na placa perfurada e por dois circuitos novos (driver de buzzer a transistor, backlight via MOSFET). Nenhuma lógica de firmware mudou, só as constantes em `Pins::` dentro de `config.h`.

| Periférico | Sinal | GPIO v1.0 | GPIO v1.1 | Motivo |
|---|---|---|---|---|
| Display ILI9341 | CS | 14 | 4 | Reorganização de layout na perfurada |
| Display ILI9341 | RST | 13 | 5 | Reorganização de layout na perfurada |
| Display ILI9341 | DC | 12 | 6 | Reorganização de layout na perfurada |
| Display ILI9341 | MOSI | 11 | 7 | Reorganização de layout na perfurada |
| Display ILI9341 | SCK | 10 | 15 | Reorganização de layout na perfurada |
| Display ILI9341 | Backlight (gate MOSFET) | 7 | 16 | Reorganização de layout na perfurada |
| MAX98357A | LRC (WS) | 6 | 9 | Reorganização de layout na perfurada |
| MAX98357A | BCLK | 5 | 10 | Reorganização de layout na perfurada |
| MAX98357A | DIN | 4 | 11 | Reorganização de layout na perfurada |
| Buzzer piezo | Sinal | 47 (sem mudança) | 47 | Pino igual; o que mudou foi o **circuito** — de resistor série direto pra driver a transistor NPN (ver [Fiação](#fiação--pontos-de-atenção)) |
| DS3231 | SDA | 2 | 1 | Papéis de SDA/SCL trocados entre si; além disso VCC passou a vir por fio isolado direto no 3.3V (não mais dividindo caminho com o RX) |
| DS3231 | SCL | 1 | 2 | Papéis de SDA/SCL trocados entre si |
| Botão 1 | — | 15 | 38 | Botões movidos pro lado oposto da placa (layout na perfurada) |
| Botão 2 | — | 16 | 39 | Botões movidos pro lado oposto da placa (layout na perfurada) |
| Botão 3 | — | 17 | 40 | Botões movidos pro lado oposto da placa (layout na perfurada) |
| Botão 4 | — | 18 | 41 | Botões movidos pro lado oposto da placa (layout na perfurada) |
| Botão 5 | — | 8 | 42 | Botões movidos pro lado oposto da placa (layout na perfurada) — candidatos iniciais incluíam pinos de PSRAM octal (33-37) e de strapping (0/45), descartados antes de qualquer solda |

Circuitos novos que entraram junto com o remapeamento (não existiam na v1.0):
- **Backlight via MOSFET IRLZ44N** — antes ligado direto no 3.3V sem controle nenhum; agora em série entre 3.3V e o pino "LED" do display, gate no GPIO16 (100Ω série + 10kΩ pull-down).
- **Buzzer via transistor NPN (MPS2222)** — antes GPIO direto num resistor série; agora o buzzer fica entre 5V e o Coletor, Emissor no GND, GPIO47 aciona a Base via 4.7kΩ — ganho de volume real.
- **Desacoplamento de energia** — capacitor cerâmico + eletrolítico em paralelo perto da alimentação de cada módulo (ESP32, display, MAX98357A), mitigando o flicker do display causado por picos de corrente do WiFi.
