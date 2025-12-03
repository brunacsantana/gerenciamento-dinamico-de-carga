# Sistema Reativo de Tempo Real com Gerenciamento Dinâmico de Carga

**Projeto final**: Sistemas de Tempo Real

---

## 📋 Sumário

- [Visão Geral](#-visão-geral)
- [Características do Sistema](#-características-do-sistema)
- [Hardware Necessário](#-hardware-necessário)
- [Arquitetura do Sistema](#-arquitetura-do-sistema)
- [Algoritmos de Escalonamento](#-algoritmos-de-escalonamento)
- [Instalação e Configuração](#-instalação-e-configuração)
- [Como Usar](#-como-usar)
- [Métricas e Monitoramento](#-métricas-e-monitoramento)
- [Resultados Esperados](#-resultados-esperados)
- [Troubleshooting](#-troubleshooting)
- [Referências](#-referências)

---

## 🎯 Visão Geral

Este projeto implementa um **sistema embarcado de tempo real** no ESP32 utilizando FreeRTOS, capaz de executar múltiplas tarefas com diferentes características temporais e alternar dinamicamente entre dois algoritmos clássicos de escalonamento:

- **Rate Monotonic (RM)**: Prioridades fixas baseadas no período
- **Earliest Deadline First (EDF)**: Prioridades dinâmicas baseadas em deadlines

O sistema demonstra conceitos fundamentais de sistemas de tempo real, incluindo:

- Escalonamento periódico e aperiódico
- Sincronização e exclusão mútua
- Monitoramento de carga de CPU
- Medição de jitter e tempos de execução
- Resposta a sobrecarga

### Objetivos Principais

1. **Executar tarefas periódicas e aperiódicas** com controle de prioridade
2. **Alternar entre RM e EDF** em tempo de execução
3. **Monitorar e medir** utilização de CPU, jitter e tempos de execução
4. **Exibir métricas** em tempo real via Serial Monitor
5. **Detectar e sinalizar** condições de sobrecarga

---

## ✨ Características do Sistema

### Tarefas Periódicas

| Tarefa     | Período | Deadline | Prioridade RM | Função                           |
| ---------- | ------- | -------- | ------------- | -------------------------------- |
| **Sensor** | 100ms   | 100ms    | 3 (alta)      | Leitura do sensor VL53L0X        |
| **Filtro** | 200ms   | 200ms    | 2 (média)     | Filtragem de dados (média móvel) |
| **Logger** | 400ms   | 400ms    | 1 (baixa)     | Exibição de status e métricas    |

### Tarefas Aperiódicas

| Tarefa          | Gatilho | Prioridade  | Função                 |
| --------------- | ------- | ----------- | ---------------------- |
| **Carga Extra** | Botão   | 5 (alta)    | Simula carga de 150ms  |
| **Supervisor**  | 50ms    | 10 (máxima) | Gerencia escalonamento |

### Recursos de Sincronização

- **Mutex**: Proteção de variáveis compartilhadas (distância)
- **Semáforo Binário**: Sinalização de evento aperiódico

---

## 🔧 Hardware Necessário

### Componentes Principais

1. **ESP32 DevKit** (qualquer versão com WiFi/Bluetooth)
2. **Sensor VL53L0X** (Time-of-Flight) - I2C
3. **4 LEDs** (qualquer cor)
4. **2 Botões** (pushbutton normalmente aberto)
5. **Resistores**: 4x 220Ω (para LEDs) + 2x 10kΩ (pull-down opcional)
6. **Breadboard e jumpers**

### Diagrama de Conexões

```
ESP32          Componente
-----          ----------
GPIO 4    ->   LED_MODE_RM (+ resistor 220Ω -> GND)
GPIO 16   ->   LED_MODE_EDF (+ resistor 220Ω -> GND)
GPIO 17   ->   LED_PROC (+ resistor 220Ω -> GND)
GPIO 2    ->   LED_ALERTA (+ resistor 220Ω -> GND)

GPIO 19   ->   BTN_MODE (outro terminal -> 3.3V)
GPIO 23   ->   BTN_APERIODIC (outro terminal -> 3.3V)

GPIO 21   ->   SDA (VL53L0X)
GPIO 22   ->   SCL (VL53L0X)
3.3V      ->   VCC (VL53L0X)
GND       ->   GND (VL53L0X)
```

**Nota:** O ESP32 possui pull-downs internos habilitados via `INPUT_PULLDOWN`, mas resistores externos de 10kΩ podem ser adicionados para maior confiabilidade.

---

## 🏗️ Arquitetura do Sistema

### Estrutura de Tarefas

```
┌─────────────────────────────────────────────────────────┐
│                   SUPERVISOR (Prioridade 10)            │
│         Gerencia RM ↔ EDF / Ajusta prioridades         │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│              TAREFAS PERIÓDICAS                         │
├─────────────────┬─────────────────┬─────────────────────┤
│ SENSOR (100ms)  │ FILTRO (200ms)  │ LOGGER (400ms)      │
│ Prioridade: 3/4 │ Prioridade: 2/3 │ Prioridade: 1/2     │
└─────────────────┴─────────────────┴─────────────────────┘
                            │
                            ▼
              ┌─────────────────────────┐
              │  TAREFA APERIÓDICA      │
              │  (Disparada por botão)  │
              │  Prioridade: 5          │
              └─────────────────────────┘
```

### Fluxo de Dados

```
VL53L0X Sensor -> [Mutex] -> distancia_mm
                                  │
                                  ▼
                             Tarefa Filtro
                                  │
                                  ▼
                        [Mutex] -> media_distancia
                                  │
                                  ▼
                            Tarefa Logger
                                  │
                                  ▼
                          Serial Monitor (Usuário)
```

---

## 📊 Algoritmos de Escalonamento

### Rate Monotonic (RM)

**Princípio:** Prioridades fixas inversamente proporcionais ao período.

**Atribuição de Prioridades:**

- Sensor (T=100ms) → Prioridade 3 (mais alta)
- Filtro (T=200ms) → Prioridade 2
- Logger (T=400ms) → Prioridade 1 (mais baixa)

**Condição de Escalonabilidade:**

```
U = Σ(Ci/Ti) ≤ n(2^(1/n) - 1)

Para 3 tarefas: U ≤ 0.78 (78%)
```

**Vantagens:**

- ✅ Simples de implementar
- ✅ Comportamento previsível
- ✅ Baixo overhead computacional

**Desvantagens:**

- ❌ Não é ótimo (pode rejeitar conjuntos escalonáveis)
- ❌ Utilização teórica máxima ~78% para 3 tarefas

### Earliest Deadline First (EDF)

**Princípio:** Prioridades dinâmicas baseadas no deadline mais próximo.

**Cálculo de Deadline:**

```cpp
deadline_absoluto = last_wake_tick + periodo
deadline_relativo = deadline_absoluto - agora
```

**Atribuição Dinâmica:**

- Tarefa com deadline mais próximo → Prioridade 4
- Tarefa com deadline intermediário → Prioridade 3
- Tarefa com deadline mais distante → Prioridade 2

**Condição de Escalonabilidade:**

```
U = Σ(Ci/Ti) ≤ 1.0 (100%)
```

**Vantagens:**

- ✅ Ótimo para sistemas preemptivos
- ✅ Utilização teórica máxima de 100%
- ✅ Melhor para conjuntos de tarefas com períodos variados

**Desvantagens:**

- ❌ Maior overhead (recálculo de prioridades)
- ❌ Comportamento menos previsível em sobrecarga
- ❌ Difícil de analisar no pior caso

### Implementação da Alternância

```cpp
void taskSupervisor(void *pvParams) {
    for (;;) {
        if (modoAtual == SCHED_RM) {
            // Prioridades fixas
            vTaskPrioritySet(tSensor.handle, 3);
            vTaskPrioritySet(tFilter.handle, 2);
            vTaskPrioritySet(tLogger.handle, 1);
        } else { // SCHED_EDF
            // Calcula deadlines e ordena
            calcularDeadlines();
            ordenarPorDeadline();
            atribuirPrioridadesDinamicas();
        }
        vTaskDelayUntil(&xLastWakeTime, 50ms);
    }
}
```

---

## 🚀 Instalação e Configuração

### Requisitos de Software

- **Arduino IDE** 2.x ou superior
- **Placa ESP32** instalada via Boards Manager
- **Bibliotecas necessárias:**
  - `Wire.h` (incluída com ESP32)
  - `Adafruit_VL53L0X` (via Library Manager)

### Passo a Passo

1. **Instalar Arduino IDE**

   ```bash
   # Linux
   sudo apt install arduino

   # Ou baixar de: https://www.arduino.cc/en/software
   ```

2. **Adicionar suporte ao ESP32**

   - Abrir Arduino IDE
   - File → Preferences → Additional Boards Manager URLs
   - Adicionar: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Buscar "ESP32" → Install

3. **Instalar biblioteca do sensor**

   - Sketch → Include Library → Manage Libraries
   - Buscar: "Adafruit VL53L0X"
   - Install (incluirá dependências automaticamente)

4. **Configurar placa**

   - Tools → Board → ESP32 Arduino → ESP32 Dev Module
   - Tools → Upload Speed → 115200
   - Tools → Port → Selecionar porta do ESP32 (ex: `/dev/ttyUSB0`)

5. **Montar hardware** conforme diagrama de conexões

6. **Carregar código**

   - Abrir `projeto-final.cpp` (ou `.ino`)
   - Sketch → Upload (Ctrl+U)

7. **Abrir Serial Monitor**
   - Tools → Serial Monitor
   - Baud rate: 115200

---

## 🎮 Como Usar

### Inicialização

1. Conecte o ESP32 ao computador via USB
2. Abra o Serial Monitor (115200 baud)
3. Aguarde a mensagem: `--- INICIANDO SISTEMA RTOS ---`
4. Sistema inicia automaticamente em **modo RM**

### Controles

| Ação              | Botão                   | Função                        |
| ----------------- | ----------------------- | ----------------------------- |
| Alternar RM ↔ EDF | BTN_MODE (GPIO 19)      | Muda modo de escalonamento    |
| Disparar carga    | BTN_APERIODIC (GPIO 23) | Adiciona 150ms de carga extra |

### Indicadores LED

| LED          | Estado   | Significado                           |
| ------------ | -------- | ------------------------------------- |
| LED_MODE_RM  | Aceso    | Modo Rate Monotonic ativo             |
| LED_MODE_EDF | Aceso    | Modo EDF ativo                        |
| LED_PROC     | Piscando | Tarefa em execução                    |
| LED_ALERTA   | Aceso    | Sobrecarga (CPU > 60%) ou carga extra |

### Saída Serial (Exemplo)

```
--- INICIANDO SISTEMA RTOS ---
[EXEC] Sensor  | Jitter: 245 us
[EXEC] Filtro  | Jitter: -112 us
--------------------------------
STATUS: [RM (Fixo)]
Leitura: 458 mm | Media: 455 mm
CPU Load: 32.45%
--------------------------------
[EXEC] Sensor  | Jitter: 89 us

>>> [CARGA EXTRA] INICIO <<<
>>> [CARGA EXTRA] FIM <<<

--------------------------------
STATUS: [EDF (Dinâmico)]
Leitura: 462 mm | Media: 457 mm
CPU Load: 78.23%
--------------------------------
```

---

## 📈 Métricas e Monitoramento

### Métricas Implementadas

#### 1. **Jitter** (Variabilidade de Ativação)

- **Fórmula:** `Jitter = (tempo_atual - tempo_anterior) - periodo_esperado`
- **Unidade:** Microssegundos (μs)
- **Interpretação:**
  - Jitter < 500μs: ✅ Bom
  - Jitter 500-2000μs: ⚠️ Aceitável
  - Jitter > 2000μs: ❌ Indica sobrecarga

#### 2. **Tempo de Execução** (WCET - Worst Case Execution Time)

- **Medição:** `esp_timer_get_time()` antes e depois da execução
- **Armazenado em:** `TaskControl.exec_time_us`
- **Uso:** Cálculo de utilização de CPU

#### 3. **Utilização de CPU**

- **Fórmula:**
  ```
  U_total = Σ(exec_time_us / (periodo_ms × 1000)) × 100%
  ```
- **Exemplo:**
  ```
  Sensor: (8000μs / 100000μs) = 0.08 = 8%
  Filtro: (12000μs / 200000μs) = 0.06 = 6%
  Total: 14%
  ```
- **Limites:**
  - U < 60%: ✅ Normal
  - 60% ≤ U < 78%: ⚠️ Atenção (próximo do limite RM)
  - U ≥ 78%: ❌ Sobrecarga (LED_ALERTA acende)

#### 4. **Deadline Miss Detection**

- Não implementado explicitamente, mas pode ser inferido por:
  - Jitter crescente
  - Utilização > 100% no EDF
  - Comportamento errático das tarefas

---

## 📊 Resultados Esperados

### Comparação de Desempenho

| Métrica         | RM (Carga Normal) | EDF (Carga Normal) | RM (Sobrecarga)        | EDF (Sobrecarga) |
| --------------- | ----------------- | ------------------ | ---------------------- | ---------------- |
| Jitter médio    | ~200μs            | ~300μs             | ~1000μs                | ~800μs           |
| CPU (%)         | 35%               | 35%                | 85%                    | 90%              |
| Previsibilidade | ✅ Alta           | ⚠️ Média           | ✅ Degrada linearmente | ❌ Imprevisível  |
| Deadline misses | 0                 | 0                  | Poucos                 | Muitos           |

### Observações Práticas

**Rate Monotonic:**

- ✅ Comportamento consistente
- ✅ Fácil de depurar
- ❌ Desperdiça capacidade (limite 78%)

**EDF:**

- ✅ Melhor utilização de CPU
- ✅ Adapta-se melhor a variações de carga
- ❌ Mais overhead de recálculo
- ❌ Colapso abrupto em sobrecarga

---

## 🛠️ Troubleshooting

### Problemas Comuns

#### 1. Sensor VL53L0X não detectado

**Sintoma:** Mensagem "Aviso: VL53L0X não detectado"

**Soluções:**

- Verificar conexões I2C (SDA=21, SCL=22)
- Testar sensor com scanner I2C:
  ```cpp
  Wire.begin();
  Wire.beginTransmission(0x29); // Endereço padrão VL53L0X
  if (Wire.endTransmission() == 0) Serial.println("Sensor OK");
  ```
- Substituir por leitura simulada (código já tem fallback)

#### 2. ESP32 reinicia constantemente

**Sintoma:** Watchdog timeout, boot loop

**Soluções:**

- Reduzir `burnCPU()` nas tarefas
- Aumentar stack size (`4096` → `8192`)
- Verificar se há loop infinito sem `vTaskDelay()`

#### 3. Jitter muito alto (> 5ms)

**Sintoma:** Sistema instável, LEDs irregulares

**Soluções:**

- Reduzir carga das tarefas
- Verificar se utilização < 78% (RM) ou < 100% (EDF)
- Desabilitar Bluetooth/WiFi se não usado:
  ```cpp
  btStop();
  WiFi.mode(WIFI_OFF);
  ```

#### 4. Compilação falha - Biblioteca não encontrada

**Sintoma:** `fatal error: Adafruit_VL53L0X.h: No such file`

**Soluções:**

- Reinstalar biblioteca via Library Manager
- Verificar dependências: `Adafruit Unified Sensor`
- Limpar cache: Sketch → Delete Build Folder

---

## 📚 Referências

### Bibliográficas

1. **Liu, C. L., & Layland, J. W.** (1973). _Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment_. Journal of the ACM (JACM), 20(1), 46-61.

2. **Buttazzo, G. C.** (2011). _Hard Real-Time Computing Systems: Predictable Scheduling Algorithms and Applications_ (3rd ed.). Springer.

3. **FreeRTOS Documentation** - https://www.freertos.org/Documentation/

4. **ESP32 Technical Reference Manual** - Espressif Systems

### Código e Bibliotecas

- **FreeRTOS**: https://github.com/espressif/arduino-esp32/tree/master/cores/esp32
- **Adafruit VL53L0X**: https://github.com/adafruit/Adafruit_VL53L0X
- **ESP32 Arduino Core**: https://github.com/espressif/arduino-esp32

### Ferramentas

- **Arduino IDE**: https://www.arduino.cc/
- **Fritzing** (para diagramas): https://fritzing.org/
- **Serial Plotter**: Integrado no Arduino IDE

---

## 📝 Licença

Este projeto foi desenvolvido para fins educacionais como parte do Trabalho Final da disciplina de Sistemas de Tempo Real da UFSC.
