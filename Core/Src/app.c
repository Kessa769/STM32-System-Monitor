#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app.h"  
#include <stdint.h>

volatile uint32_t supervisor_success =0;
volatile uint32_t monitor_success =0;
volatile uint32_t telemetry_success =0;
volatile uint32_t workload_success =0;

void SupervisorTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();

    while (1){
        supervisor_success++;

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(100));
    }
}

void MonitorTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();

    while (1){
        monitor_success++;

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(500));
    }
}

void WorkloadTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();
    volatile uint32_t res =0;
    while (1){
        for (int i=0;i<10000;i++){
            res+=1;
        }
        workload_success++;

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(200));
    }
}


void TelemetryTask(void *argument){
    TickType_t lastWakeTime =xTaskGetTickCount();

    while (1){
        telemetry_success++;

        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_15);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void App_Init(void){
    xTaskCreate(SupervisorTask, "Supervisor", 256, NULL, 4, NULL);
    xTaskCreate(MonitorTask, "Monitor", 256, NULL, 3, NULL);
    xTaskCreate(WorkloadTask, "Workload", 256, NULL, 1, NULL);
    xTaskCreate(TelemetryTask, "Telemetry", 256, NULL, 2, NULL);
}