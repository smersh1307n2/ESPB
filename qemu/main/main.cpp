#include <stdio.h>
#include <string.h>
#include "espb_api.h"
#include "espb_embedded_modules.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "espb_host_symbols.h"
#include "espb_fast_symbols.h"

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
    printf("QEMU custom print: %s\\n", message ? message : "(null)");
}

void native_set_magic_number(int *out_value)
{
    if (out_value) *out_value = 42;
}

static bool expect_u32(espb_handle_t handle, const char *function_name,
                       const Value *args, uint32_t num_args,
                       uint32_t expected, const char *label)
{
    Value result = ESPB_U32(0);
    EspbResult rc = espb_call_function_sync(handle, function_name, args, num_args, &result);
    const uint32_t actual = V_U32(result);
    const bool ok = rc == ESPB_OK && actual == expected;
    printf("[ESPB_ASSERT] %s=%s actual=%u expected=%u rc=%d\\n",
           label, ok ? "PASS" : "FAIL", (unsigned)actual,
           (unsigned)expected, (int)rc);
    return ok;
}

static bool expect_i32(espb_handle_t handle, const char *function_name,
                       const Value *args, uint32_t num_args,
                       int32_t expected, const char *label)
{
    Value result = ESPB_I32(0);
    EspbResult rc = espb_call_function_sync(handle, function_name, args, num_args, &result);
    const int32_t actual = V_I32(result);
    const bool ok = rc == ESPB_OK && actual == expected;
    printf("[ESPB_ASSERT] %s=%s actual=%d expected=%d rc=%d\\n",
           label, ok ? "PASS" : "FAIL", (int)actual,
           (int)expected, (int)rc);
    return ok;
}

static bool run_semantic_checks(espb_handle_t handle, const char *module_name)
{
    const char *mode = strstr(module_name, "jit__") != NULL ? "jit" : "interpreter";
    if (strstr(module_name, "test_01_integer") != NULL) {
        const Value chain_args[] = { ESPB_U32(1112) };
        const Value signed_args[] = { ESPB_I32(1112), ESPB_I32(0) };
        const bool chain_ok = expect_u32(handle, "espb_integer_chain", chain_args, 1, 1110154669u, "integer_chain");
        const bool signed_ok = expect_i32(handle, "espb_integer_signed", signed_args, 2, 7784, "integer_signed");
        printf("[ESPB_ASSERT] integer=%s mode=%s\\n", chain_ok && signed_ok ? "PASS" : "FAIL", mode);
        return chain_ok && signed_ok;
    }

    if (strstr(module_name, "test_02_control_flow") != NULL) {
        const Value loop_args[] = { ESPB_U32(24) };
        const Value nested_args[] = { ESPB_U32(1112) };
        const bool loop_ok = expect_u32(handle, "espb_branch_loop", loop_args, 1, 249u, "branch_loop");
        const bool nested_ok = expect_u32(handle, "espb_nested_branch", nested_args, 1, 165u, "nested_branch");
        printf("[ESPB_ASSERT] control_flow=%s mode=%s\\n", loop_ok && nested_ok ? "PASS" : "FAIL", mode);
        return loop_ok && nested_ok;
    }

    if (strstr(module_name, "test_03_memory") != NULL) {
        uint32_t values[8];
        for (uint32_t i = 0; i < 8; ++i) values[i] = 1112u + i * 7u;
        const Value transform_args[] = { ESPB_PTR(values), ESPB_U32(8), ESPB_U32(1112) };
        const Value checksum_args[] = { ESPB_PTR(values), ESPB_U32(8) };
        const EspbResult transform_rc = espb_call_function_sync(handle, "espb_array_transform", transform_args, 3, NULL);
        Value checksum = ESPB_U32(0);
        const EspbResult checksum_rc = espb_call_function_sync(handle, "espb_array_checksum", checksum_args, 2, &checksum);
        const uint32_t actual = V_U32(checksum);
        const bool ok = transform_rc == ESPB_OK && checksum_rc == ESPB_OK && actual == 2623u;
        printf("[ESPB_ASSERT] memory=%s mode=%s checksum=%u expected=2623 transform_rc=%d checksum_rc=%d\\n",
               ok ? "PASS" : "FAIL", mode, (unsigned)actual, (int)transform_rc, (int)checksum_rc);
        return ok;
    }

    if (strstr(module_name, "test_04_calls") != NULL) {
        const Value args[] = { ESPB_U32(1112), ESPB_U32(12) };
        const bool ok = expect_u32(handle, "espb_call_chain", args, 2, 3794755490u, "call_chain");
        printf("[ESPB_ASSERT] calls=%s mode=%s\\n", ok ? "PASS" : "FAIL", mode);
        return ok;
    }

    return true;
}

static const EspbSymbolFast qemu_fast_symbols[] = {
#include "symbols/custom_fast.sym"
};

static const EspbSymbol qemu_named_symbols[] = {
    {"printf", (const void*)&printf},
    ESP_ELFSYM_END
};

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

        if (!run_semantic_checks(handle, module.name)) {
            printf("[ESPB_TEST] semantic_failed=%s\\n", module.name);
            espb_unload_module(handle);
            return;
        }

        // Warm up once before collecting measurements.
        result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        if (result != ESPB_OK) {
            printf("[ESPB_TEST] warmup_failed=%s error=%d\n", module.name, (int)result);
            espb_unload_module(handle);
            return;
        }

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

        // Median of five, insertion sort; no heap allocation.
        for (int i = 1; i < SAMPLE_COUNT; ++i) {
            int64_t value_us = samples_us[i];
            uint32_t value_cycles = samples_cycles[i];
            int j = i;
            while (j > 0 && samples_us[j - 1] > value_us) {
                samples_us[j] = samples_us[j - 1];
                --j;
            }
            samples_us[j] = value_us;
            j = i;
            while (j > 0 && samples_cycles[j - 1] > value_cycles) {
                samples_cycles[j] = samples_cycles[j - 1];
                --j;
            }
            samples_cycles[j] = value_cycles;
        }
        printf("[ESPB_BENCH] module=%s app_main_median_us=%lld app_main_median_cycles=%u\n",
               module.name, (long long)samples_us[SAMPLE_COUNT / 2],
               (unsigned)samples_cycles[SAMPLE_COUNT / 2]);

        espb_unload_module(handle);
        printf("[ESPB_TEST] module_done=%s mode=%s\n", module.name, jit_mode ? "jit" : "interpreter");
    }

    printf("--- ESPB dual-mode QEMU tests finished ---\n");
}
