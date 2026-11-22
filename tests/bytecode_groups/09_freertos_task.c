#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

static volatile uint32_t task_result;
static volatile uint32_t task_done;

JIT_HOT static void task_worker(void *parameter)
{
    uint32_t value = (uint32_t)(uintptr_t)parameter;
    task_result = value * 3u + 1u;
    task_done = 1u;
    /* Keep the task alive until app_main deletes it by the saved handle. */
    for (;;) {
        vTaskDelay(1);
    }
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    task_result = 0;
    task_done = 0;
    TaskHandle_t handle = NULL;
    // ESP-IDF expresses task stack depth in bytes; the ESPB callback needs room for its trampoline.
    BaseType_t created = xTaskCreate(task_worker, "espb_group_task", 8192,
                                     (void *)(uintptr_t)13u, tskIDLE_PRIORITY + 1,
                                     &handle);
    if (created == pdPASS) {
        /* Bounded wait for worker completion before deleting its task. */
        for (uint32_t wait = 0; wait < 100u && !task_done; ++wait) {
            vTaskDelay(1);
        }
    }
    int ok = created == pdPASS && task_done == 1u && task_result == 40u;
    if (created == pdPASS && handle != NULL) {
        /* vTaskDelete takes TaskHandle_t, not TaskHandle_t*: pass handle. */
        vTaskDelete(handle);
        handle = NULL;
        /* Let the idle task reclaim the deleted task's stack and TCB. */
        vTaskDelay(2);
    }
    printf("[ESPB_GROUP] freertos_task=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
