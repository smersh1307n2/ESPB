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

static uint64_t qemu_native_conversion_probe(int64_t wide, int32_t narrow)
{
    const uint8_t u8 = (uint8_t)narrow;
    const int8_t s8 = (int8_t)narrow;
    const uint16_t u16 = (uint16_t)narrow;
    const int16_t s16 = (int16_t)narrow;
    const int32_t truncated = (int32_t)wide;
    const uint32_t truncated_unsigned = (uint32_t)wide;
    const int64_t sign_extended = (int64_t)narrow;
    const uint64_t zero_extended = (uint64_t)(uint32_t)narrow;

    uint64_t mix = (uint64_t)u8;
    mix ^= (uint64_t)(int64_t)s8 << 8;
    mix ^= (uint64_t)u16 << 16;
    mix ^= (uint64_t)(int64_t)s16 << 32;
    mix ^= (uint32_t)truncated;
    mix ^= (uint64_t)truncated_unsigned << 1;
    mix ^= (uint64_t)sign_extended;
    mix ^= zero_extended << 2;
    return mix;
}

static int32_t qemu_native_atomic_i32_probe(void)
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
    printf("--- ESPB native source C tests ---\n");
    espb_native_source_control_flow(1112, "native-source", NULL);
    espb_native_source_select_convert(1112, "native-source", NULL);
    espb_native_source_freertos_timer(1112, "native-source", NULL);
    printf("[ESPB_NATIVE_SOURCE] completed groups=3\n");

    espb_register_custom_index_symbol_table(qemu_fast_symbols);
    espb_register_symbol_table(0, qemu_named_symbols);

    printf("[ESPB_NATIVE] switch2=%ld switchneg=%ld switchdefault=%ld\n",
           (long)qemu_native_switch_probe(2),
           (long)qemu_native_switch_probe(-1),
           (long)qemu_native_switch_probe(99));
    printf("[ESPB_NATIVE] conversion=0x%016llx expected=0xffffcdedf8dbbd27\n",
           (unsigned long long)qemu_native_conversion_probe(0x123456789ll, (int32_t)0x89abcdefu));
    printf("[ESPB_NATIVE] atomic_i32=%ld expected=318\n",
           (long)qemu_native_atomic_i32_probe());

    char qemu_message[] = "Hello from ESPB_WORK QEMU";
    Value args[3] = {
        ESPB_I32(1112),
        ESPB_PTR(qemu_message),
        ESPB_PTR(NULL)
    };

    printf("[ESPB_TEST] module_count=%u\n", (unsigned)espb_embedded_module_count);

    // Prefer running jit__08_atomics early for diagnostics, but keep full coverage.
    size_t first_jit_index = espb_embedded_module_count;
    size_t jit_atomics_index = espb_embedded_module_count;
    for (size_t i = 0; i < espb_embedded_module_count; ++i) {
        if (strncmp(espb_embedded_modules[i].name, "jit__", 5) == 0 &&
            first_jit_index == espb_embedded_module_count) {
            first_jit_index = i;
        }
        if (strcmp(espb_embedded_modules[i].name, "jit__08_atomics.espb") == 0) {
            jit_atomics_index = i;
        }
    }

    for (size_t execution_index = 0; execution_index < espb_embedded_module_count; ++execution_index) {
        size_t module_index = execution_index;
        if (jit_atomics_index < espb_embedded_module_count &&
            execution_index == first_jit_index) {
            module_index = jit_atomics_index;
        } else if (execution_index == jit_atomics_index) {
            module_index = first_jit_index;
        }

        const EspbEmbeddedModule &module = espb_embedded_modules[module_index];

        printf("[ESPB_TEST] module=%s size=%u\n", module.name, (unsigned)module.size);

        const uint32_t heap_before_load = esp_get_free_heap_size();
        const uint32_t min_heap_before = esp_get_minimum_free_heap_size();
        const UBaseType_t stack_hwm_before = uxTaskGetStackHighWaterMark(NULL);

        espb_handle_t handle = NULL;
        EspbResult result = espb_load_module(module.data, module.size, &handle);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] load_failed=%s error=%d\n", module.name, (int)result);
            return;
        }
        const uint32_t heap_after_load = esp_get_free_heap_size();

        // Warmup path (also the only interpreter execution).
        result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] warmup_failed=%s error=%d\n", module.name, (int)result);
            espb_unload_module(handle);
            return;
        }

        const bool jit_mode = (strncmp(module.name, "jit__", 5) == 0);

        if (jit_mode) {
            // Compilation and register allocation are outside every execution timer.
            result = espb_jit_precompile_hot(handle);
            if (result != ESPB_OK) {
                printf("[ESPB_TEST] precompile_failed=%s error=%d\n", module.name, (int)result);
                espb_unload_module(handle);
                return;
            }
        }

        EspbInstance *instance = handle ? handle->instance : NULL;
        uint16_t hot = 0, compiled = 0;
        uint32_t jit_bytes = 0;
        if (instance && instance->module) {
            for (uint32_t fi = 0; fi < instance->module->num_functions; ++fi) {
                EspbFunctionBody *body = &instance->module->function_bodies[fi];
                printf("[ESPB_FRAME] module=%s function=%u vregs=%u max_reg_used=%u max_used_regs=%u frame_size=%u\n",
                       module.name, (unsigned)fi,
                       (unsigned)body->header.num_virtual_regs,
                       (unsigned)body->header.max_reg_used,
                       (unsigned)body->header.max_used_regs,
                       (unsigned)body->header.frame_size);
                if ((body->header.flags & ESPB_FUNC_FLAG_HOT) != 0) {
                    hot++;
                    if (body->is_jit_compiled) {
                        compiled++;
                        jit_bytes += (uint32_t)body->jit_code_size;
                    }
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
        const uint32_t min_heap_after = esp_get_minimum_free_heap_size();
        const UBaseType_t stack_hwm_after = uxTaskGetStackHighWaterMark(NULL);

        espb_unload_module(handle);
        const uint32_t heap_after_unload = esp_get_free_heap_size();
        const UBaseType_t stack_hwm_delta =
            stack_hwm_before > stack_hwm_after ? stack_hwm_before - stack_hwm_after : 0;
        printf("[ESPB_MEM] module=%s heap_before=%u heap_loaded=%u heap_ready=%u heap_after_exec=%u heap_after_unload=%u min_heap_before=%u min_heap_after=%u stack_hwm_before_words=%u stack_hwm_after_words=%u stack_hwm_peak_delta_words=%u\n",
               module.name, (unsigned)heap_before_load, (unsigned)heap_after_load,
               (unsigned)heap_after_ready, (unsigned)heap_after_exec,
               (unsigned)heap_after_unload, (unsigned)min_heap_before,
               (unsigned)min_heap_after, (unsigned)stack_hwm_before,
               (unsigned)stack_hwm_after, (unsigned)stack_hwm_delta);
        printf("[ESPB_TEST] module_done=%s mode=%s\n", module.name, jit_mode ? "jit" : "interpreter");
    }

    printf("--- ESPB dual-mode QEMU tests finished ---\n");
}
