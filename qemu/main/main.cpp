#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "espb_api.h"
#include "espb_interpreter_runtime.h"
#include "espb_jit_precompile.h"
#include "espb_embedded_modules.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "espb_host_symbols.h"
#include "espb_fast_symbols.h"

// The opaque public handle is intentionally opened only in this QEMU test
// translation unit. Keep this layout in sync with espb_api.c; production API
// users continue to see an opaque handle.
struct espb_module_handle_t {
    EspbInstance *instance;
    EspbModule *module;
};

typedef void (*qemu_cb1_t)(void *);
typedef void (*qemu_cb2_t)(int, void *);

__attribute__((noinline, optimize("O0")))
void host_invoke_cb(qemu_cb1_t cb, void *user_data)
{
    if (cb) cb(user_data);
}

__attribute__((noinline, optimize("O0")))
void host_invoke_cb2(qemu_cb2_t cb, int value, void *user_data)
{
    if (cb) cb(value, user_data);
}

void my_custom_print(const char *message)
{
    printf("QEMU custom print: %s\n", message ? message : "(null)");
}

void native_set_magic_number(int *out_value)
{
    if (out_value) *out_value = 42;
}

/*
 * RV32 ESP-Clang emits one 64-bit atomic exchange helper import in the
 * targeted atomics probe. QEMU runs this deterministic single-threaded
 * semantic case; this helper provides the expected sequential exchange
 * semantics without changing firmware/toolchain atomics or production code.
 * This wrapper is intentionally QEMU-test-only and single-threaded.
 */
uint64_t qemu_atomic_exchange_8(volatile void *address,
                                uint64_t value,
                                int memory_order)
{
    (void)memory_order;
    volatile uint64_t *target = (volatile uint64_t *)address;
    const uint64_t previous = *target;
    *target = value;
    return previous;
}

// The checked-in timer test module can carry a millisecond-to-tick zero
// period from an earlier translation. Keep QEMU progressing and report the
// shim explicitly; FreeRTOS requires a positive tick period.
static TimerHandle_t qemu_xTimerCreate(const char *name,
                                       TickType_t period,
                                       UBaseType_t auto_reload,
                                       void *timer_id,
                                       TimerCallbackFunction_t callback)
{
    TickType_t requested = period;
    if (period < 2) {
        printf("[ESPB_RTOS] timer_period_clamped name=%s requested=%u -> 2 cb=%p id=%p\n",
               name ? name : "(null)", (unsigned)requested,
               (void *)callback, timer_id);
        period = 2;
    } else {
        printf("[ESPB_RTOS] timer_create name=%s period=%u cb=%p id=%p\n",
               name ? name : "(null)", (unsigned)period,
               (void *)callback, timer_id);
    }
    TimerHandle_t handle = xTimerCreate(name, period, auto_reload, timer_id, callback);
    if (handle == NULL) {
        printf("[ESPB_RTOS] timer_create_failed name=%s\n", name ? name : "(null)");
    }
    return handle;
}

static void qemu_vTaskDelay(TickType_t ticks)
{
    if (ticks == 0) {
        printf("[ESPB_RTOS] vTaskDelay_clamped_from_zero -> 5\n");
        ticks = 5;
    }
    vTaskDelay(ticks);
}

static const EspbSymbolFast qemu_fast_symbols[] = {
#include "symbols/custom_fast.sym"
};

static const EspbSymbol qemu_named_symbols[] = {
    {"printf", (const void*)&printf},
    {"host_invoke_cb", (const void*)&host_invoke_cb},
    {"host_invoke_cb2", (const void*)&host_invoke_cb2},
    {"__atomic_exchange_8", (const void*)&qemu_atomic_exchange_8},
    // These modules use the regular named-import path (not IDF fast indices).
    {"vTaskDelay", (const void*)&qemu_vTaskDelay},
    {"xTaskCreatePinnedToCore", (const void*)&xTaskCreatePinnedToCore},
    {"vTaskDelete", (const void*)&vTaskDelete},
    {"xTaskGetTickCount", (const void*)&xTaskGetTickCount},
    {"xTimerCreate", (const void*)&qemu_xTimerCreate},
    {"xTimerGenericCommand", (const void*)&xTimerGenericCommand},
    {"pvTimerGetTimerID", (const void*)&pvTimerGetTimerID},
    ESP_ELFSYM_END
};
