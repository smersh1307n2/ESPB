#include <stdio.h>
#include <string.h>
#include "espb_api.h"
#include "espb_interpreter_runtime.h"
#include "espb_embedded_modules.h"
#include "esp_timer.h"
#include "esp_cpu.h"
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

static const EspbSymbolFast qemu_fast_symbols[] = {
#include "symbols/custom_fast.sym"
};

static const EspbSymbol qemu_named_symbols[] = {
    {"printf", (const void*)&printf},
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
    if (!qemu_find_exported_function(handle, function_name, &global_func_idx, NULL, NULL)) {
        printf("[ESPB_BENCH_INTERP_CORE] function=%s unavailable\n", function_name);
        return false;
    }

    enum { SAMPLE_COUNT = 5 };
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
        !body || !body->is_jit_compiled || !body->jit_code) {
        printf("[ESPB_BENCH_RAW_JIT] function=%s jit_entry_unavailable\n", function_name);
        return false;
    }

    uint16_t actual_regs = body->header.num_virtual_regs;
    if (actual_regs > 256) actual_regs = 256;
    if (actual_regs < 8) actual_regs = 8;

    uint32_t copied_args = signature->num_params;
    if (copied_args > args_count) copied_args = args_count;
    if (copied_args > actual_regs) copied_args = actual_regs;

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

extern "C" void app_main(void)
{
    printf("--- ESPB dual-mode QEMU tests ---\n");
    espb_register_custom_index_symbol_table(qemu_fast_symbols);
    espb_register_symbol_table(0, qemu_named_symbols);

    char qemu_message[] = "Hello from ESPB_WORK QEMU";
    Value args[] = {
        ESPB_I32(1112),
        ESPB_PTR(qemu_message),
        ESPB_PTR(NULL)
    };

    printf("[ESPB_TEST] module_count=%u\n", (unsigned)espb_embedded_module_count);
    for (size_t module_index = 0; module_index < espb_embedded_module_count; ++module_index) {
        const EspbEmbeddedModule &module = espb_embedded_modules[module_index];
        const bool jit_mode = module.name[0] == 'j' && module.name[1] == 'i' && module.name[2] == 't' && module.name[3] == '_';
        printf("[ESPB_TEST] module=%s size=%u\n", module.name, (unsigned)module.size);

        espb_handle_t handle = NULL;
        EspbResult result = espb_load_module(module.data, module.size, &handle);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] load_failed=%s error=%d\n", module.name, (int)result);
            return;
        }

        // This call resolves HOT functions and compiles the JIT entry before
        // either core benchmark begins. No compilation is timed.
        result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] warmup_failed=%s error=%d\n", module.name, (int)result);
            espb_unload_module(handle);
            return;
        }

        const bool core_bench_ok = jit_mode
            ? qemu_benchmark_raw_jit(handle, "app_main", args, 3)
            : qemu_benchmark_interpreter_core(handle, "app_main", args, 3);
        if (!core_bench_ok) {
            printf("[ESPB_TEST] core_bench_failed=%s\n", module.name);
            espb_unload_module(handle);
            return;
        }

        // Public API/end-to-end measurement is retained separately. It includes
        // name lookup, context allocation/free, dispatcher and engine setup.
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

        espb_unload_module(handle);
        printf("[ESPB_TEST] module_done=%s mode=%s\n", module.name, jit_mode ? "jit" : "interpreter");
    }

    printf("--- ESPB dual-mode QEMU tests finished ---\n");
}
