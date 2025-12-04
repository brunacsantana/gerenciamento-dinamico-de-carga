#include <Wire.h>
#include <Adafruit_VL53L0X.h>

#define LED_MODE_RM 4
#define LED_MODE_EDF 16
#define LED_PROC 17
#define LED_ALERTA 2

#define BTN_MODE 19
#define BTN_APERIODIC 23

// Objeto do sensor VL53L0X (Time-of-Flight)
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

SemaphoreHandle_t xMutexDistancia;
SemaphoreHandle_t xSemAperiodico;

volatile int distancia_mm = 0;
volatile int media_distancia = 0;

enum Escalonador
{
    SCHED_RM, // Rate Monotonic - Prioridades fixas baseadas no período
    SCHED_EDF // Earliest Deadline First - Prioridades dinâmicas baseadas em deadlines
};
volatile Escalonador modoAtual = SCHED_RM; // Modo inicial: Rate Monotonic

/**
 * Estrutura para armazenar informações de controle e métricas de cada tarefa.
 * Permite monitoramento em tempo real e ajuste dinâmico de prioridades.
 */
struct TaskControl
{
    TaskHandle_t handle;
    int id;
    const char *nome;
    int periodo_ms;
    int deadline_relativo;
    TickType_t last_wake_tick;
    int64_t last_wake_time_us;
    int exec_time_us;
    int prioridade_rm;
};

TaskControl tSensor = {NULL, 1, "Sensor", 100, 100, 0, 0, 0, 3};
TaskControl tFilter = {NULL, 2, "Filtro", 200, 200, 0, 0, 0, 2};
TaskControl tLogger = {NULL, 3, "Logger", 400, 400, 0, 0, 0, 1};

/**
 * Função de queima de CPU (busy-wait)
 * Utilizada para simular carga de processamento nas tarefas.
 *
 * @param ms Tempo em milissegundos para ocupar a CPU
 */
void burnCPU(int ms)
{
    unsigned long end = millis() + ms;
    while (millis() < end)
        __asm__("nop");
}

/**
 * ISR do botão de carga aperiódica.
 * Dispara a tarefa aperiódica que simula uma carga extra no sistema.
 * Implementa debounce por software (200ms).
 */
void IRAM_ATTR isrBtnAperiodic()
{
    static unsigned long last_time = 0;
    if (millis() - last_time > 200) // Debounce de 200ms
    {
        xSemaphoreGiveFromISR(xSemAperiodico, NULL);
        last_time = millis();
    }
}

/**
 * ISR do botão de alternância de modo.
 * Alterna entre os modos de escalonamento RM e EDF.
 * Implementa debounce por software (500ms).
 */
void IRAM_ATTR isrBtnMode()
{
    static unsigned long last_time = 0;
    if (millis() - last_time > 500) // Debounce de 500ms
    {
        if (modoAtual == SCHED_RM)
            modoAtual = SCHED_EDF;
        else
            modoAtual = SCHED_RM;
        last_time = millis();
    }
}

void taskSensorRead(void *pvParams)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(tSensor.periodo_ms);

    // Inicializa o timestamp de referência para cálculo de jitter
    tSensor.last_wake_time_us = esp_timer_get_time();

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // Cálculo de jitter
        int64_t now_us = esp_timer_get_time();
        int64_t jitter = (now_us - tSensor.last_wake_time_us) - (tSensor.periodo_ms * 1000);
        tSensor.last_wake_time_us = now_us;
        tSensor.last_wake_tick = xLastWakeTime; // Salva para uso no cálculo de deadline (EDF)

        Serial.printf("[EXEC] %s \t| Jitter: %lld us\n", tSensor.nome, jitter);

        int64_t start = esp_timer_get_time();
        digitalWrite(LED_PROC, HIGH);

        if (xSemaphoreTake(xMutexDistancia, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            // Leitura do sensor VL53L0X
            VL53L0X_RangingMeasurementData_t measure;

            // Realiza medição de distância
            lox.rangingTest(&measure, false);

            // Verifica se a leitura é válida (RangeStatus != 4 indica sucesso)
            if (measure.RangeStatus != 4)
            {
                distancia_mm = measure.RangeMilliMeter;
            }
            else
            {
                distancia_mm = -1; // Indica erro ou objeto fora de alcance
            }

            xSemaphoreGive(xMutexDistancia);
        }

        // Simula carga de processamento adicional
        burnCPU(5);

        digitalWrite(LED_PROC, LOW);

        // Registra o tempo de execução
        tSensor.exec_time_us = (int)(esp_timer_get_time() - start);
    }
}

void taskFilterData(void *pvParams)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(tFilter.periodo_ms);

    tFilter.last_wake_time_us = esp_timer_get_time();

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // Cálculo de jitter
        int64_t now_us = esp_timer_get_time();
        int64_t jitter = (now_us - tFilter.last_wake_time_us) - (tFilter.periodo_ms * 1000);
        tFilter.last_wake_time_us = now_us;
        tFilter.last_wake_tick = xLastWakeTime;

        Serial.printf("[EXEC] %s \t| Jitter: %lld us\n", tFilter.nome, jitter);

        int64_t start = esp_timer_get_time();

        if (xSemaphoreTake(xMutexDistancia, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            // Aplica filtro de média móvel exponencial
            if (distancia_mm != -1)
                media_distancia = (media_distancia * 0.7) + (distancia_mm * 0.3);

            xSemaphoreGive(xMutexDistancia);
        }

        // Simula carga de processamento
        burnCPU(10);

        // Registra tempo de execução
        tFilter.exec_time_us = (int)(esp_timer_get_time() - start);
    }
}

void taskLogger(void *pvParams)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(tLogger.periodo_ms);

    tLogger.last_wake_time_us = esp_timer_get_time();

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        tLogger.last_wake_tick = xLastWakeTime;

        tLogger.last_wake_time_us = esp_timer_get_time();

        int64_t start = esp_timer_get_time();

        Serial.println("--------------------------------");
        Serial.printf("STATUS: [%s] \n", (modoAtual == SCHED_RM ? "RM (Fixo)" : "EDF (Dinâmico)"));
        Serial.printf("Leitura: %d mm | Media: %d mm\n", distancia_mm, media_distancia);

        // Cálculo de utilização da CPU
        float u1 = (float)tSensor.exec_time_us / (tSensor.periodo_ms * 1000.0);
        float u2 = (float)tFilter.exec_time_us / (tFilter.periodo_ms * 1000.0);
        float uTotal = (u1 + u2) * 100.0;

        Serial.printf("CPU Load: %.2f%%\n", uTotal);
        Serial.println("--------------------------------");

        // Alerta de sobrecarga: acende LED se utilização > 60%
        if (uTotal > 60.0)
            digitalWrite(LED_ALERTA, HIGH);
        else
            digitalWrite(LED_ALERTA, LOW);

        tLogger.exec_time_us = (int)(esp_timer_get_time() - start);
    }
}

void taskAperiodic(void *pvParams)
{
    for (;;)
    {
        // Aguarda sinalização do semáforo (disparado pela ISR do botão)
        if (xSemaphoreTake(xSemAperiodico, portMAX_DELAY) == pdTRUE)
        {
            Serial.println("\n>>> [CARGA EXTRA] INICIO <<<");
            digitalWrite(LED_ALERTA, HIGH);

            // Simula processamento pesado (150ms de carga)
            burnCPU(150);

            digitalWrite(LED_ALERTA, LOW);
            Serial.println(">>> [CARGA EXTRA] FIM <<<\n");
        }
    }
}

void taskSupervisor(void *pvParams)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(50);

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // MODO RATE MONOTONIC (RM)
        if (modoAtual == SCHED_RM)
        {
            digitalWrite(LED_MODE_RM, HIGH);
            digitalWrite(LED_MODE_EDF, LOW);

            // Define prioridades fixas baseadas no período
            vTaskPrioritySet(tSensor.handle, tSensor.prioridade_rm);
            vTaskPrioritySet(tFilter.handle, tFilter.prioridade_rm);
            vTaskPrioritySet(tLogger.handle, tLogger.prioridade_rm);
        }
        // MODO EARLIEST DEADLINE FIRST (EDF)
        else
        {
            digitalWrite(LED_MODE_RM, LOW);
            digitalWrite(LED_MODE_EDF, HIGH);

            TickType_t now = xTaskGetTickCount();

            // Calcula deadline relativo para cada tarefa
            long dSensor = tSensor.last_wake_tick + pdMS_TO_TICKS(tSensor.periodo_ms) - now;
            long dFilter = tFilter.last_wake_tick + pdMS_TO_TICKS(tFilter.periodo_ms) - now;
            long dLogger = tLogger.last_wake_tick + pdMS_TO_TICKS(tLogger.periodo_ms) - now;

            // Prepara estruturas para ordenação
            TaskControl *tasks[3] = {&tSensor, &tFilter, &tLogger};
            long deadlines[3] = {dSensor, dFilter, dLogger};

            // Bubble Sort: ordena tarefas por deadline (menor deadline = maior prioridade)
            for (int i = 0; i < 2; i++)
            {
                for (int j = 0; j < 2 - i; j++)
                {
                    if (deadlines[j] > deadlines[j + 1])
                    {
                        // Troca deadlines
                        long tempD = deadlines[j];
                        deadlines[j] = deadlines[j + 1];
                        deadlines[j + 1] = tempD;

                        // Troca ponteiros de tarefas
                        TaskControl *tempT = tasks[j];
                        tasks[j] = tasks[j + 1];
                        tasks[j + 1] = tempT;
                    }
                }
            }

            // Atribui prioridades dinâmicas
            vTaskPrioritySet(tasks[0]->handle, 4);
            vTaskPrioritySet(tasks[1]->handle, 3);
            vTaskPrioritySet(tasks[2]->handle, 2);
        }
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(LED_MODE_RM, OUTPUT);
    pinMode(LED_MODE_EDF, OUTPUT);
    pinMode(LED_PROC, OUTPUT);
    pinMode(LED_ALERTA, OUTPUT);

    pinMode(BTN_MODE, INPUT_PULLDOWN);
    pinMode(BTN_APERIODIC, INPUT_PULLDOWN);

    Wire.begin();

    if (!lox.begin())
    {
        Serial.println(F("Aviso: VL53L0X não detectado. Usando dados simulados."));
    }

    xMutexDistancia = xSemaphoreCreateMutex();
    xSemAperiodico = xSemaphoreCreateBinary();

    attachInterrupt(digitalPinToInterrupt(BTN_MODE), isrBtnMode, RISING);
    attachInterrupt(digitalPinToInterrupt(BTN_APERIODIC), isrBtnAperiodic, RISING);

    Serial.println("--- INICIANDO SISTEMA RTOS ---");

    // Tarefas periódicas (iniciam com prioridades RM)
    xTaskCreate(taskSensorRead, "Sensor", 4096, NULL, tSensor.prioridade_rm, &tSensor.handle);
    xTaskCreate(taskFilterData, "Filter", 4096, NULL, tFilter.prioridade_rm, &tFilter.handle);
    xTaskCreate(taskLogger, "Logger", 4096, NULL, tLogger.prioridade_rm, &tLogger.handle);

    // Tarefa aperiódica (prioridade alta: 5)
    xTaskCreate(taskAperiodic, "Aperiodic", 4096, NULL, 5, NULL);

    // Tarefa supervisor (prioridade máxima: 10)
    xTaskCreate(taskSupervisor, "Supervisor", 4096, NULL, 10, NULL);
}

void loop()
{
    vTaskDelay(1000);
}