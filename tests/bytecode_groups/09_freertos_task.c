#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

static volatile uint32_t task_count;

JIT_HOT static void counter_task(void *pvParameter)
{
    (void)pvParameter;
    for (;;) {
        ++task_count;
        vTaskDelay(1);
    }
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    task_count = 0;

    /* Follow ESP32_PRJ_TO_LLVM/main/all_tests.c:test_second():
       create with an output TaskHandle_t, then delete using that handle. */
    TaskHandle_t counter_task_handle = NULL;
    BaseType_t created = xTaskCreate(&counter_task, "counter_task", 16384,
                                     (void *)0xAABBCCDDu, 5,
                                     &counter_task_handle);

    if (created == pdPASS && counter_task_handle != NULL) {
        /* Let the worker run, then delete exactly the task we created. */
        vTaskDelay(2);
        vTaskDelete(counter_task_handle);
        counter_task_handle = NULL;
        /* FreeRTOS idle task reclaims the deleted task's stack and TCB. */
        vTaskDelay(2);
    }

    int ok = created == pdPASS && task_count != 0;
    printf("[ESPB_GROUP] freertos_task=%s arg=%ld\n",
           ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
