#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

static volatile uint32_t timer_result;

JIT_HOT static void timer_callback(TimerHandle_t timer)
{
    timer_result = (uint32_t)(uintptr_t)pvTimerGetTimerID(timer) ^ 0x5a5au;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    timer_result = 0;
    // Use ticks directly: a 2 ms period rounds to zero with a 100 Hz tick rate.
    TimerHandle_t timer = xTimerCreate("espb_group_timer", 2,
                                       pdFALSE, (void *)(uintptr_t)0x1234u,
                                       timer_callback);
    BaseType_t started = pdFAIL;
    if (timer != NULL) {
        started = xTimerStart(timer, 0);
        vTaskDelay(5);
        (void)xTimerStop(timer, 0);
        (void)xTimerDelete(timer, 0);
    }
    int ok = timer != NULL && started == pdPASS && timer_result == (0x1234u ^ 0x5a5au);
    printf("[ESPB_GROUP] freertos_timer=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
