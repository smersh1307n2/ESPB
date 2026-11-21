#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

static volatile uint32_t task_result;

JIT_HOT static void task_worker(void *parameter)
{
    uint32_t value = (uint32_t)(uintptr_t)parameter;
    task_result = value * 3u + 1u;
    vTaskDelete(NULL);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    task_result = 0;
    TaskHandle_t handle = NULL;
    BaseType_t created = xTaskCreate(task_worker, "espb_group_task", 2048,
                                     (void *)(uintptr_t)13u, tskIDLE_PRIORITY + 1,
                                     &handle);
    if (created == pdPASS) {
        for (volatile uint32_t spin = 0; spin < 100000u && task_result == 0; ++spin) {
        }
    }
    int ok = created == pdPASS && task_result == 40u;
    printf("[ESPB_GROUP] freertos_task=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
