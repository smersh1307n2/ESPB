#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

static volatile uint32_t task_count;
static volatile uint32_t task_parameter_seen;

JIT_HOT static void counter_task(void *pvParameter)
{
    task_parameter_seen = (uint32_t)(uintptr_t)pvParameter;
    for (;;) {
        ++task_count;
        vTaskDelay(1);
    }
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    task_count = 0;
    task_parameter_seen = 0;
    TaskHandle_t counter_task_handle = NULL;

    /* Mirror ESP32_PRJ_TO_LLVM/main/all_tests.c:test_second():
       xTaskCreate(counter_task, non-null pvParameter, saved task handle,
       then vTaskDelete(handle). Keep the wait bounded for this opcode probe. */
    BaseType_t created = xTaskCreate(&counter_task, "counter_task", 8192,
                                     (void *)(uintptr_t)0xAABBCCDDu,
                                     tskIDLE_PRIORITY + 1,
                                     &counter_task_handle);
    if (created == pdPASS) {
        for (uint32_t wait = 0; wait < 100u && task_count == 0; ++wait) {
            vTaskDelay(1);
        }
    }

    int ok = created == pdPASS && counter_task_handle != NULL &&
             task_count != 0 && task_parameter_seen == 0xAABBCCDDu;
    if (created == pdPASS && counter_task_handle != NULL) {
        vTaskDelete(counter_task_handle);
        counter_task_handle = NULL;
        /* Give the idle task time to reclaim the deleted task's stack and TCB. */
        vTaskDelay(2);
    }

    printf("[ESPB_GROUP] freertos_task=%s arg=%ld\n",
           ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
