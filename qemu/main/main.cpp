#include <stdio.h>
#include "espb_api.h"
#include "espb_embedded_modules.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "espb_host_symbols.h"
#include "espb_fast_symbols.h"

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
