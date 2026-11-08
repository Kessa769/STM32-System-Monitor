#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app.h"  
#include "usbd_cdc_if.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

#define HB_timeout_threshold 3
#define H_HB_sup (1u << 0)
#define H_HB_mon (1u << 1)
#define H_HB_TEL (1u << 2) 
#define H_HB_WRK (1u << 3) 
#define H_LOW_STK (1u << 4)
#define H_LOW_HEAP (1u << 5)

#define min_stack_bytes 128
#define min_heap_bytes 1024

typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t stack_sup;   /* bytes still free */
    uint32_t stack_mon;
    uint32_t stack_tel;
    uint32_t stack_wrk;
} MemStats_t;

static volatile MemStats_t mem_stats;

TaskHandle_t SupervisorHandle;
TaskHandle_t MonitorHandle;
TaskHandle_t TelemetryHandle;
TaskHandle_t WorkloadHandle;

volatile uint32_t supervisor_success =0;
volatile uint32_t monitor_success =0;
volatile uint32_t telemetry_success =0;
volatile uint32_t workload_success =0;

enum {T_sup=0,T_mon,T_tel,T_wrk,ntasks};

static const uint32_t periode_ms[ntasks] = {100, 500, 1000, 200};

static volatile TickType_t hb_tick[ntasks];
static volatile uint32_t health =0;

static volatile uint32_t last_dwt_us =0;
static volatile uint32_t worst_dwt_us =0;

enum{T_idle =ntasks, T_others, T_count};

static volatile uint64_t cy_cons[T_count];
static uint32_t last_cycle =0;
static uint32_t cur_slot =T_idle;

static volatile uint32_t cpu_pm[T_count];
static volatile uint32_t cpu_total_pm =0;

static inline void Heartbeat(int id){
        hb_tick[id] =xTaskGetTickCount();
}

void MonitorMemory(void){
    mem_stats.free_heap     = xPortGetFreeHeapSize();
    mem_stats.min_free_heap = xPortGetMinimumEverFreeHeapSize();
    mem_stats.stack_sup = uxTaskGetStackHighWaterMark(SupervisorHandle) * sizeof(StackType_t);
    mem_stats.stack_mon = uxTaskGetStackHighWaterMark(MonitorHandle)    * sizeof(StackType_t);
    mem_stats.stack_tel = uxTaskGetStackHighWaterMark(TelemetryHandle)  * sizeof(StackType_t);
    mem_stats.stack_wrk = uxTaskGetStackHighWaterMark(WorkloadHandle)   * sizeof(StackType_t);
}

void Trace_SwitchedIn(void *tcb){
    uint32_t now = DWT->CYCCNT;
    cy_cons[cur_slot] += (uint32_t)(now - last_cycle);
    last_cycle =now;

    if (tcb == (void *)SupervisorHandle) {cur_slot = T_sup;}
    else if (tcb == (void *)MonitorHandle) {cur_slot = T_mon;}
    else if (tcb == (void *)TelemetryHandle) {cur_slot = T_tel;}
    else if (tcb == (void *)WorkloadHandle) {cur_slot = T_wrk;}
    else if (tcb == (void *)xTaskGetIdleTaskHandle()) {cur_slot = T_idle;}
    else {cur_slot = T_others;}
}

static void MonitorCPU(void)
{
    static uint64_t prev[T_count];            /* snapshot from the last call */
    uint64_t snap[T_count], delta, total = 0, d[T_count];

    taskENTER_CRITICAL();                     /* 64-bit reads are not atomic on Cortex-M4 */
    for (int i = 0; i < T_count; i++) snap[i] = cy_cons[i];
    taskEXIT_CRITICAL();

    for (int i = 0; i < T_count; i++) {
        delta   = snap[i] - prev[i];          /* cycles used in the last 500 ms */
        prev[i] = snap[i];
        d[i]    = delta;
        total  += delta;
    }
    if (total == 0) return;

    for (int i = 0; i < T_count; i++)
        cpu_pm[i] = (uint32_t)((d[i] * 1000u) / total);

    cpu_total_pm = 1000u - cpu_pm[T_idle];
}

void SupervisorTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();
    const TickType_t delay = pdMS_TO_TICKS(1000);
    
    while (1){
        supervisor_success++;
        Heartbeat(T_sup);

        uint32_t h=0;
        TickType_t now = xTaskGetTickCount();

        if (now > delay){
            for (int i=0;i<ntasks;i++){
                if ((now -hb_tick[i]> pdMS_TO_TICKS(periode_ms[i]*HB_timeout_threshold))){
                    h |= (1u << i);
                }
            }
        if (mem_stats.stack_sup < min_stack_bytes ||
                mem_stats.stack_mon < min_stack_bytes ||
                mem_stats.stack_tel < min_stack_bytes ||
                mem_stats.stack_wrk < min_stack_bytes){
                h |= H_LOW_STK;}

            if (mem_stats.min_free_heap < min_heap_bytes){
                h |= H_LOW_HEAP;}
        }
        health = h;

        if (h == 0) {HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12);}
        else{HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);}
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(100));
    }
}

void MonitorTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();

    for(;;){
        monitor_success++;
        Heartbeat(T_mon);
        MonitorMemory();
        MonitorCPU();

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(500));
    }
}

void WorkloadTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();
    volatile uint32_t res =0;
    while (1){
        uint32_t start = DWT->CYCCNT;

        for (int i=0;i<10000;i++){
            res+=1;
        }
        uint32_t cycle = DWT->CYCCNT -start;
        uint32_t cycle_us = cycle /(SystemCoreClock/1000000u);

        last_dwt_us= cycle_us;

        if(cycle_us > worst_dwt_us){
            worst_dwt_us= cycle_us;
        }
        Heartbeat(T_wrk);
        workload_success++;

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(200));
    }
}


void TelemetryTask(void *argument){
    TickType_t lastWakeTime = xTaskGetTickCount();
    static char tx_buffer[192];

    for(;;){
        telemetry_success++;
        Heartbeat(T_tel);
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_15);

        if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
            int len = snprintf(tx_buffer, sizeof(tx_buffer),
                "t=%lu cpu=%lu pm[sup=%lu mon=%lu tel=%lu wrk=%lu usb=%lu] heap=%lu min=%lu stk[sup=%lu mon=%lu tel=%lu wrk=%lu] wl=%lu/%luus hp=0x%02lX %s \r\n",
                (unsigned long)xTaskGetTickCount(),
                (unsigned long)cpu_total_pm,
                (unsigned long)cpu_pm[T_sup], 
                (unsigned long)cpu_pm[T_mon],
                (unsigned long)cpu_pm[T_tel], 
                (unsigned long)cpu_pm[T_wrk],
                (unsigned long)cpu_pm[T_others],
                (unsigned long)mem_stats.free_heap,
                (unsigned long)mem_stats.min_free_heap,
                (unsigned long)mem_stats.stack_sup,
                (unsigned long)mem_stats.stack_mon,
                (unsigned long)mem_stats.stack_tel,
                (unsigned long)mem_stats.stack_wrk,
                (unsigned long)last_dwt_us,
                (unsigned long)worst_dwt_us,
                (unsigned long)health,
                health ? "Fault" : "OK");
            CDC_Transmit_FS((uint8_t*)tx_buffer, len);
        }
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}

static void DWT_Init(void){
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT =0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void App_Init(void){
    DWT_Init();
    xTaskCreate(SupervisorTask, "Supervisor", 256, NULL, 4, &SupervisorHandle);
    xTaskCreate(MonitorTask, "Monitor", 256, NULL, 3, &MonitorHandle);
    xTaskCreate(WorkloadTask, "Workload", 256, NULL, 1, &WorkloadHandle);
    xTaskCreate(TelemetryTask, "Telemetry", 256, NULL, 2, &TelemetryHandle);
}