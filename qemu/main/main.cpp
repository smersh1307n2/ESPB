#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "espb_api.h"
#include "espb_interpreter_runtime.h"
#include "espb_jit_precompile.h"
#include "espb_embedded_modules.h"
#include "esp_timer.h"
#include "esp_cpu.h"
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
typedef void (*qemu_jit_func_t)(EspbInstance *, Value *);

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

static const EspbSymbolFast qemu_fast_symbols[] = {
#include "symbols/custom_fast.sym"
};

static const EspbSymbol qemu_named_symbols[] = {
    {"printf", (const void*)&printf},
    {"host_invoke_cb", (const void*)&host_invoke_cb},
    {"host_invoke_cb2", (const void*)&host_invoke_cb2},
    {"__atomic_exchange_8", (const void*)&qemu_atomic_exchange_8},
    // These modules use the regular named-import path (not IDF fast indices).
    {"vTaskDelay", (const void*)&vTaskDelay},
    {"xTaskCreatePinnedToCore", (const void*)&xTaskCreatePinnedToCore},
    {"vTaskDelete", (const void*)&vTaskDelete},
    {"xTaskGetTickCount", (const void*)&xTaskGetTickCount},
    {"xTimerCreate", (const void*)&xTimerCreate},
    {"xTimerGenericCommand", (const void*)&xTimerGenericCommand},
    {"pvTimerGetTimerID", (const void*)&pvTimerGetTimerID},
    ESP_ELFSYM_END
};

static bool qemu_find_exported_function(
    espb_handle_t handle,
    const char *function_name,
    uint32_t *out_global_func_idx,
    EspbFunctionBody **out_body,
    const EspbFuncSignature **out_signature)
{
    if (!handle || !handle->module || !handle->instance || !function_name) return false;
    EspbModule *module = handle->module;

    for (uint32_t i = 0; i < module->num_exports; ++i) {
        const EspbExportDesc *exp = &module->exports[i];
        if (exp->kind != ESPB_IMPORT_KIND_FUNC || !exp->name ||
            strcmp(exp->name, function_name) != 0) {
            continue;
        }

        const uint32_t local_func_idx = exp->index;
        if (local_func_idx >= module->num_functions) return false;
        const uint32_t sig_idx = module->function_signature_indices[local_func_idx];
        if (sig_idx >= module->num_signatures) return false;

        if (out_global_func_idx) *out_global_func_idx = module->num_imported_funcs + local_func_idx;
        if (out_body) *out_body = &module->function_bodies[local_func_idx];
        if (out_signature) *out_signature = &module->signatures[sig_idx];
        return true;
    }
    return false;
}

static void qemu_sort_samples(int64_t *samples_us, uint32_t *samples_cycles, int count)
{
    for (int i = 1; i < count; ++i) {
        const int64_t value_us = samples_us[i];
        int j = i;
        while (j > 0 && samples_us[j - 1] > value_us) {
            samples_us[j] = samples_us[j - 1];
            --j;
        }
        samples_us[j] = value_us;

        const uint32_t value_cycles = samples_cycles[i];
        j = i;
        while (j > 0 && samples_cycles[j - 1] > value_cycles) {
            samples_cycles[j] = samples_cycles[j - 1];
            --j;
        }
        samples_cycles[j] = value_cycles;
    }
}

// Interpreter core measurement: context allocation/free and public name lookup
// are outside the timer; the timer covers espb_call_function and its bytecode
// execution/frame setup.
static bool qemu_benchmark_interpreter_core(
    espb_handle_t handle,
    const char *function_name,
    const Value *args,
    uint32_t args_count)
{
    uint32_t global_func_idx = 0;
    const EspbFuncSignature *signature = NULL;
    if (!qemu_find_exported_function(handle, function_name, &global_func_idx, NULL, &signature) ||
        !signature || signature->num_params > args_count) {
        printf("[ESPB_BENCH_INTERP_CORE] function=%s unavailable_or_args_mismatch\n", function_name);
        return false;
    }

    enum { SAMPLE_COUNT = 1 };
    int64_t samples_us[SAMPLE_COUNT];
    uint32_t samples_cycles[SAMPLE_COUNT];

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        ExecutionContext *exec_ctx = init_execution_context();
        if (!exec_ctx) {
            printf("[ESPB_BENCH_INTERP_CORE] function=%s context_alloc_failed\n", function_name);
            return false;
        }

        Value result = {};
        const int64_t start_us = esp_timer_get_time();
        const uint32_t start_cycles = esp_cpu_get_cycle_count();
        const EspbResult rc = espb_call_function(handle->instance, exec_ctx, global_func_idx, args, &result);
        const uint32_t end_cycles = esp_cpu_get_cycle_count();
        samples_us[i] = esp_timer_get_time() - start_us;
        samples_cycles[i] = end_cycles - start_cycles;
        free_execution_context(exec_ctx);

        if (rc != ESPB_OK) {
            printf("[ESPB_BENCH_INTERP_CORE] function=%s call_failed rc=%d\n",
                   function_name, (int)rc);
            return false;
        }
    }

    qemu_sort_samples(samples_us, samples_cycles, SAMPLE_COUNT);
    printf("[ESPB_BENCH_INTERP_CORE] function=%s median_us=%lld median_cycles=%u samples=%u\n",
           function_name, (long long)samples_us[SAMPLE_COUNT / 2],
           (unsigned)samples_cycles[SAMPLE_COUNT / 2], (unsigned)SAMPLE_COUNT);
    return true;
}

// Raw JIT measurement: compilation/warmup, register-buffer initialization and
// argument copying are all completed before the timer starts. The timed call is
// the generated JitFunc(instance, v_regs) entry itself.
static bool qemu_benchmark_raw_jit(
    espb_handle_t handle,
    const char *function_name,
    const Value *args,
    uint32_t args_count)
{
    EspbFunctionBody *body = NULL;
    const EspbFuncSignature *signature = NULL;
    if (!qemu_find_exported_function(handle, function_name, NULL, &body, &signature) ||
        !body || !signature || !body->is_jit_compiled || !body->jit_code ||
        signature->num_params > args_count) {
        printf("[ESPB_BENCH_RAW_JIT] function=%s jit_entry_unavailable_or_args_mismatch\n", function_name);
        return false;
    }

    uint16_t actual_regs = body->header.num_virtual_regs;
    if (actual_regs > 256) actual_regs = 256;
    if (actual_regs < 8) actual_regs = 8;

    uint32_t copied_args = signature->num_params;
    if (copied_args > actual_regs) {
        printf("[ESPB_BENCH_RAW_JIT] function=%s argument_registers_exceed_frame\n", function_name);
        return false;
    }

    Value v_regs[256] __attribute__((aligned(8)));
    qemu_jit_func_t jit_func = (qemu_jit_func_t)body->jit_code;

    enum { SAMPLE_COUNT = 5 };
    int64_t samples_us[SAMPLE_COUNT];
    uint32_t samples_cycles[SAMPLE_COUNT];

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        // Reset the complete virtual-register input state outside the timed region.
        memset(v_regs, 0, (size_t)actual_regs * sizeof(Value));
        if (args && copied_args > 0) {
            memcpy(v_regs, args, (size_t)copied_args * sizeof(Value));
        }

        const int64_t start_us = esp_timer_get_time();
        const uint32_t start_cycles = esp_cpu_get_cycle_count();
        jit_func(handle->instance, v_regs);
        const uint32_t end_cycles = esp_cpu_get_cycle_count();
        samples_us[i] = esp_timer_get_time() - start_us;
        samples_cycles[i] = end_cycles - start_cycles;
    }

    qemu_sort_samples(samples_us, samples_cycles, SAMPLE_COUNT);
    printf("[ESPB_BENCH_RAW_JIT] function=%s median_us=%lld median_cycles=%u regs=%u args=%u samples=%u\n",
           function_name, (long long)samples_us[SAMPLE_COUNT / 2],
           (unsigned)samples_cycles[SAMPLE_COUNT / 2], (unsigned)actual_regs,
           (unsigned)copied_args, (unsigned)SAMPLE_COUNT);
    return true;
}


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
    volatile int32_t value = 10;
    const int32_t old_add = __atomic_fetch_add(&value, 5, __ATOMIC_SEQ_CST);
    const int32_t old_sub = __atomic_fetch_sub(&value, 3, __ATOMIC_SEQ_CST);
    const int32_t old_and = __atomic_fetch_and(&value, 0x0f, __ATOMIC_SEQ_CST);
    const int32_t old_or = __atomic_fetch_or(&value, 0x20, __ATOMIC_SEQ_CST);
    const int32_t old_xor = __atomic_fetch_xor(&value, 0x03, __ATOMIC_SEQ_CST);
    const int32_t old_exchange = __atomic_exchange_n(&value, 42, __ATOMIC_SEQ_CST);
    int32_t expected = 42;
    const int success = __atomic_compare_exchange_n(
        &value, &expected, 100, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    expected = 99;
    const int failure = __atomic_compare_exchange_n(
        &value, &expected, 200, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    const int32_t loaded = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
    __atomic_store_n(&value, 77, __ATOMIC_SEQ_CST);
    const int32_t stored = __atomic_load_n(&value, __ATOMIC_SEQ_CST);

    return old_add + old_sub + old_and + old_or + old_xor +
           old_exchange + success + failure + loaded + stored;
}

extern "C" void app_main(void)
{
    printf("--- ESPB dual-mode QEMU tests ---\n");
    espb_register_custom_index_symbol_table(qemu_fast_symbols);
    espb_register_symbol_table(0, qemu_named_symbols);

    printf("[ESPB_NATIVE] switch2=%ld switchneg=%ld switchdefault=%ld\\n",
           (long)qemu_native_switch_probe(2),
           (long)qemu_native_switch_probe(-1),
           (long)qemu_native_switch_probe(99));
    printf("[ESPB_NATIVE] conversion=0x%016llx expected=0xffffcdedf8dbbd27\\n",
           (unsigned long long)qemu_native_conversion_probe(
               0x123456789ll, (int32_t)0x89abcdefu));
    printf("[ESPB_NATIVE] atomic_i32=%ld expected=318\\n",
           (long)qemu_native_atomic_i32_probe());

    char qemu_message[] = "Hello from ESPB_WORK QEMU";
    Value args[] = {
        ESPB_I32(1112),
        ESPB_PTR(qemu_message),
        ESPB_PTR(NULL)
    };

    printf("[ESPB_TEST] module_count=%u\n", (unsigned)espb_embedded_module_count);

    // Run the JIT atomics probe before other JIT groups. This keeps CMPXCHG
    // coverage reachable even when an unrelated earlier JIT group faults.
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
            first_jit_index < jit_atomics_index) {
            if (execution_index == first_jit_index) {
                module_index = jit_atomics_index;
            } else if (execution_index > first_jit_index &&
                       execution_index <= jit_atomics_index) {
                module_index = execution_index - 1;
            }
        }
        const EspbEmbeddedModule &module = espb_embedded_modules[module_index];
        const bool jit_mode = strncmp(module.name, "jit__", 5) == 0;
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

        // This call resolves HOT functions and compiles the JIT entry before
        // either core benchmark begins. No compilation is timed.
        result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] warmup_failed=%s error=%d\n", module.name, (int)result);
            espb_unload_module(handle);
            return;
        }

        if (jit_mode) {
            // Compilation and register allocation are outside every execution timer.
            result = espb_jit_precompile_hot_functions(handle->instance, NULL);
            if (result != ESPB_OK) {
                printf("[ESPB_TEST] precompile_failed=%s error=%d\n", module.name, (int)result);
                espb_unload_module(handle);
                return;
            }
        }

        uint32_t hot_count = 0;
        uint32_t compiled_hot_count = 0;
        size_t jit_code_bytes = 0;
        for (uint32_t i = 0; i < handle->module->num_functions; ++i) {
            EspbFunctionBody *body = &handle->module->function_bodies[i];
            const uint16_t declared_regs = body->header.num_virtual_regs;
            const uint16_t max_used_regs = (uint16_t)body->header.max_reg_used + 1u;
            printf("[ESPB_FRAME] module=%s function=%u vregs=%u max_reg_used=%u max_used_regs=%u frame_size=%u\n",
                   module.name, (unsigned)i, (unsigned)declared_regs,
                   (unsigned)body->header.max_reg_used, (unsigned)max_used_regs,
                   (unsigned)body->header.frame_size);
            if (body->header.flags & ESPB_FUNC_FLAG_HOT) {
                ++hot_count;
                if (body->is_jit_compiled && body->jit_code) {
                    ++compiled_hot_count;
                    jit_code_bytes += body->jit_code_size;
                }
            }
        }

        if (jit_mode) {
            if (hot_count == 0 || compiled_hot_count != hot_count) {
                printf("[ESPB_TEST] jit_not_ready=%s hot=%u compiled=%u\n",
                       module.name, (unsigned)hot_count, (unsigned)compiled_hot_count);
                espb_unload_module(handle);
                return;
            }
            printf("[ESPB_TEST] jit_ready=%s hot=%u compiled=%u jit_bytes=%u\n",
                   module.name, (unsigned)hot_count, (unsigned)compiled_hot_count,
                   (unsigned)jit_code_bytes);
        } else {
            if (hot_count != 0 || compiled_hot_count != 0) {
                printf("[ESPB_TEST] interpreter_not_clean=%s hot=%u compiled=%u\n",
                       module.name, (unsigned)hot_count, (unsigned)compiled_hot_count);
                espb_unload_module(handle);
                return;
            }
            printf("[ESPB_TEST] interp_ready=%s hot=0 compiled=0\n", module.name);
        }

        const uint32_t heap_after_ready = esp_get_free_heap_size();
        const bool is_bounded_rtos =
            strstr(module.name, "09_freertos_task") != NULL ||
            strstr(module.name, "10_freertos_timer") != NULL;

        if (is_bounded_rtos) {
            // RTOS integration functions execute exactly three times per mode.
            for (int call = 1; call < 3; ++call) {
                result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
                if (result != ESPB_OK) {
                    printf("[ESPB_TEST] call_failed=%s call=%d error=%d\n",
                           module.name, call + 1, (int)result);
                    espb_unload_module(handle);
                    return;
                }
            }
            // Deleted FreeRTOS tasks are reclaimed by the idle task. Give it
            // time to run before taking the heap snapshot; otherwise QEMU
            // reports the task stacks as a false per-module heap leak.
            vTaskDelay(pdMS_TO_TICKS(20));
            // Deleted FreeRTOS tasks are reclaimed by the idle task. Give it
            // time to run before taking the heap snapshot; otherwise QEMU
            // reports the task stacks as a false per-module heap leak.
            vTaskDelay(pdMS_TO_TICKS(20));
            printf("[ESPB_TEST] bounded_rtos_calls=%s count=3\n", module.name);
        } else {
            const bool core_bench_ok = jit_mode
                ? qemu_benchmark_raw_jit(handle, "app_main", args, 3)
                : qemu_benchmark_interpreter_core(handle, "app_main", args, 3);
            if (!core_bench_ok) {
                printf("[ESPB_TEST] core_bench_failed=%s\n", module.name);
                espb_unload_module(handle);
                return;
            }

            // Public API timing is separate from raw interpreter/JIT execution.
            enum { SAMPLE_COUNT = 5 };
            int64_t samples_us[SAMPLE_COUNT];
            uint32_t samples_cycles[SAMPLE_COUNT];
            for (int i = 0; i < SAMPLE_COUNT; ++i) {
                const int64_t start_us = esp_timer_get_time();
                const uint32_t start_cycles = esp_cpu_get_cycle_count();
                result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
                const uint32_t end_cycles = esp_cpu_get_cycle_count();
                samples_us[i] = esp_timer_get_time() - start_us;
                samples_cycles[i] = end_cycles - start_cycles;
                printf("[ESPB_BENCH] module=%s sample_%d_us=%lld cycles_start=%u cycles_end=%u cycles_delta=%u\n",
                       module.name, i, (long long)samples_us[i],
                       (unsigned)start_cycles, (unsigned)end_cycles,
                       (unsigned)samples_cycles[i]);
                if (result != ESPB_OK) {
                    printf("[ESPB_TEST] call_failed=%s error=%d\n", module.name, (int)result);
                    espb_unload_module(handle);
                    return;
                }
            }

            qemu_sort_samples(samples_us, samples_cycles, SAMPLE_COUNT);
            printf("[ESPB_BENCH] module=%s app_main_median_us=%lld app_main_median_cycles=%u\n",
                   module.name, (long long)samples_us[SAMPLE_COUNT / 2],
                   (unsigned)samples_cycles[SAMPLE_COUNT / 2]);
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
