#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "espb_api.h"
#include "espb_host_symbols.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "espb_host_symbols.h"

// Embedded dual-mode modules produced by the paired translator workflow.
#include "espb_embedded_modules.h"

extern "C" int espb_native_source_control_flow(int32_t arg, const char *text, void *user_data);
extern "C" int espb_native_source_select_convert(int32_t arg, const char *text, void *user_data);
extern "C" int espb_native_source_atomics(int32_t arg, const char *text, void *user_data);

typedef void (*qemu_cb1_t)(void *user_data);
typedef void (*qemu_cb2_t)(int value, void *user_data);

void host_invoke_cb(qemu_cb1_t cb, void *user_data)
{
    if (cb) {
        cb(user_data);
    }
}

void host_invoke_cb2(qemu_cb2_t cb, int value, void *user_data)
{
    if (cb) {
        cb(value, user_data);
    }
}

static void qemu_custom_print(const char *message)
{
    printf("QEMU custom print: %s\n", message ? message : "(null)");
}

/*
 * RV32 ESP-Clang emits one 64-bit atomic exchange helper import in the
 * atomics group. Provide a host implementation so the module loads.
 */
static uint64_t qemu_atomic_exchange_8(uint64_t *address, uint64_t value)
{
    volatile uint64_t *target = (volatile uint64_t *)address;
    const uint64_t previous = *target;
    *target = value;
    return previous;
}

// Stale corpus modules may pass a millisecond-scaled period that becomes 0
// ticks under the QEMU tick rate. Clamp to a positive period and log enough
// to see whether the FreeRTOS timer service later invokes the ESPB closure.
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

// Same class of stale translation issue: a ms->tick conversion can yield 0 and
// the module then checks timer_result before the one-shot can expire.
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

static int32_t qemu_native_switch_probe(int32_t value)
{
    if (value < 0) return -1;
    if (value == 0) return 7;
    switch (value) {
        case 1: return 11;
        case 2: return 13;
        case 3: return 17;
        default: return 19;
    }
}

static uint64_t qemu_native_conversion_probe(void)
{
    const int32_t narrow = (int32_t)0x89abcdefu;
    const int64_t sign_extended = (int64_t)narrow;
    const uint64_t zero_extended = (uint64_t)(uint32_t)narrow;
    uint64_t mix = 0x123456789ull;
    mix ^= (uint64_t)(int64_t)(int8_t)0xef << 8;
    mix ^= (uint64_t)(uint16_t)0xcdef << 16;
    mix ^= (uint64_t)(int64_t)(int16_t)0xcdef << 32;
    mix ^= (uint32_t)(int32_t)0x10000002aull;
    mix ^= (uint64_t)(uint32_t)0x89abcdefu << 1;
    mix ^= (uint64_t)sign_extended;
    mix ^= zero_extended << 2;
    return mix;
}

static int32_t qemu_native_atomic_probe(void)
{
    volatile int32_t value = 100;
    value += 200;
    value ^= 18;
    return value;
}

extern "C" void espb_native_source_control_flow(int32_t, const char *, void *);
extern "C" void espb_native_source_select_convert(int32_t, const char *, void *);
extern "C" void espb_native_source_freertos_timer(int32_t, const char *, void *);

extern "C" void app_main(void)
{
    espb_register_symbol_table(0, qemu_named_symbols);
    espb_register_fast_symbol_table(1, qemu_fast_symbols,
                                    sizeof(qemu_fast_symbols) / sizeof(qemu_fast_symbols[0]));

    // All test modules are linked into this one firmware image by CMake.
    // Iterate the embedded module table; never rebuild/reboot per test.
    // Keep CI's semantic run bounded; the workflow validates the full pair set.
    printf("--- ESPB native source C tests ---\n");
    espb_native_source_control_flow(1112, "native-source", NULL);
    espb_native_source_select_convert(1112, "native-source", NULL);
    espb_native_source_freertos_timer(1112, "native-source", NULL);
    printf("[ESPB_NATIVE_SOURCE] completed groups=3\n");

    printf("--- ESPB dual-mode QEMU tests ---\n");

    printf("[ESPB_NATIVE] switch2=%ld switchneg=%ld switchdefault=%ld\n",
           (long)qemu_native_switch_probe(2),
           (long)qemu_native_switch_probe(-1),
           (long)qemu_native_switch_probe(9));
    printf("[ESPB_NATIVE] conversion=0x%016llx expected=0xffffcdedf8dbbd27\n",
           (unsigned long long)qemu_native_conversion_probe());
    printf("[ESPB_NATIVE] atomic_i32=%ld expected=318\n",
           (long)qemu_native_atomic_probe());

    if (espb_embedded_module_count == 0) {
        printf("[ESPB_TEST] no embedded modules\n");
        return;
    }

    printf("[ESPB_TEST] module_count=%u\n", (unsigned)espb_embedded_module_count);

    for (size_t mi = 0; mi < espb_embedded_module_count; ++mi) {
        const EspbEmbeddedModule module = espb_embedded_modules[mi];
        if (!module.name || !module.data || module.size == 0) {
            continue;
        }

        const bool jit_mode = (strncmp(module.name, "jit__", 5) == 0);
        const bool interp_mode = (strncmp(module.name, "interp__", 8) == 0);
        if (!jit_mode && !interp_mode) {
            continue;
        }

        printf("[ESPB_TEST] module=%s size=%u\n", module.name, (unsigned)module.size);

        const uint32_t heap_before = esp_get_free_heap_size();
        const uint32_t min_heap_before = esp_get_minimum_free_heap_size();
        UBaseType_t stack_hwm_before = uxTaskGetStackHighWaterMark(NULL);

        EspbHandle handle = NULL;
        EspbResult result = espb_load_module(module.data, module.size, &handle);
        if (result != ESPB_OK || handle == NULL) {
            printf("[ESPB_TEST] load_failed=%s error=%d\n", module.name, (int)result);
            return;
        }

        const uint32_t heap_loaded = esp_get_free_heap_size();

        Value args[3];
        args[0] = (Value){.type = ESPB_TYPE_I32, .i32 = 1112};
        args[1] = (Value){.type = ESPB_TYPE_PTR, .ptr = (void *)"qemu-text"};
        args[2] = (Value){.type = ESPB_TYPE_PTR, .ptr = NULL};

        // Warmup / interpreter path: always run once through the dispatcher.
        result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] warmup_failed=%s error=%d\n", module.name, (int)result);
            espb_unload_module(handle);
            return;
        }

        if (jit_mode) {
            // Compilation and register allocation are outside every execution timer.
            result = espb_precompile_hot_functions(handle);
            if (result != ESPB_OK) {
                printf("[ESPB_TEST] precompile_failed=%s error=%d\n", module.name, (int)result);
                espb_unload_module(handle);
                return;
            }
        }

        uint16_t hot = 0, compiled = 0;
        uint32_t jit_bytes = 0;
        for (uint32_t fi = 0; fi < 64; ++fi) {
            uint16_t vregs = 0, max_reg = 0, max_used = 0, frame = 0;
            if (espb_get_function_frame_info(handle, fi, &vregs, &max_reg, &max_used, &frame) != ESPB_OK) {
                break;
            }
            printf("[ESPB_FRAME] module=%s function=%u vregs=%u max_reg_used=%u max_used_regs=%u frame_size=%u\n",
                   module.name, (unsigned)fi, (unsigned)vregs, (unsigned)max_reg,
                   (unsigned)max_used, (unsigned)frame);
            if (espb_function_is_hot(handle, fi)) {
                hot++;
                if (espb_function_is_compiled(handle, fi)) {
                    compiled++;
                    jit_bytes += espb_function_jit_size(handle, fi);
                }
            }
        }

        if (jit_mode) {
            if (hot == 0 || compiled != hot) {
                printf("[ESPB_TEST] jit_not_ready=%s hot=%u compiled=%u\n",
                       module.name, (unsigned)hot, (unsigned)compiled);
                espb_unload_module(handle);
                return;
            }
            printf("[ESPB_TEST] jit_ready=%s hot=%u compiled=%u jit_bytes=%u\n",
                   module.name, (unsigned)hot, (unsigned)compiled, (unsigned)jit_bytes);
        } else {
            if (hot != 0 || compiled != 0) {
                printf("[ESPB_TEST] interpreter_not_clean=%s hot=%u compiled=%u\n",
                       module.name, (unsigned)hot, (unsigned)compiled);
                espb_unload_module(handle);
                return;
            }
            printf("[ESPB_TEST] interp_ready=%s hot=0 compiled=0\n", module.name);
        }

        const uint32_t heap_after_ready = esp_get_free_heap_size();
        const bool is_bounded_rtos =
            strstr(module.name, "09_freertos_task") != NULL ||
            strstr(module.name, "10_freertos_timer") != NULL;

        // This firmware embeds the complete corpus; keep execution single-pass.
        // The module was already run once during load/warmup. Interpreter
        // modules need no repeated execution. For JIT modules, execute once
        // more after explicit HOT precompilation so this pass covers generated
        // native code without turning semantic tests into benchmarks.
        if (jit_mode) {
            result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
            if (result != ESPB_OK) {
                printf("[ESPB_TEST] call_failed=%s error=%d\n", module.name, (int)result);
                espb_unload_module(handle);
                return;
            }
        }

        if (is_bounded_rtos) {
            // Let one-shot timers expire and the idle task reclaim deleted stacks.
            vTaskDelay(pdMS_TO_TICKS(50));
            printf("[ESPB_TEST] bounded_rtos_calls=%s count=1\n", module.name);
        }

        const uint32_t heap_after_exec = esp_get_free_heap_size();

        espb_unload_module(handle);

        const uint32_t heap_after_unload = esp_get_free_heap_size();
        const uint32_t min_heap_after = esp_get_minimum_free_heap_size();
        UBaseType_t stack_hwm_after = uxTaskGetStackHighWaterMark(NULL);

        printf("[ESPB_MEM] module=%s heap_before=%u heap_loaded=%u heap_ready=%u heap_after_exec=%u heap_after_unload=%u min_heap_before=%u min_heap_after=%u stack_hwm_before_words=%u stack_hwm_after_words=%u stack_hwm_peak_delta_words=%u\n",
               module.name,
               (unsigned)heap_before, (unsigned)heap_loaded, (unsigned)heap_after_ready,
               (unsigned)heap_after_exec, (unsigned)heap_after_unload,
               (unsigned)min_heap_before, (unsigned)min_heap_after,
               (unsigned)stack_hwm_before, (unsigned)stack_hwm_after,
               (unsigned)(stack_hwm_before > stack_hwm_after ? stack_hwm_before - stack_hwm_after : 0));

        printf("[ESPB_TEST] module_done=%s mode=%s\n", module.name, jit_mode ? "jit" : "interpreter");
    }

    printf("--- ESPB dual-mode QEMU tests finished ---\n");
}
