/*
 * espb component
 * Copyright (C) 2025 Smersh
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "espb_api.h"
#include "espb_interpreter.h"
#include "espb_host_symbols.h"
#include "espb_callback_system.h"
#include "espb_jit_dispatcher.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_timer.h"

// Внешние обертки для iram_pool
extern void iram_pool_init_wrapper(void);
extern void iram_pool_debug_wrapper(void);

// Определение структуры дескриптора
struct espb_module_handle_t {
    EspbInstance *instance;
    EspbModule *module;
    uint32_t num_func_imports;
};


EspbResult espb_load_module(const uint8_t *espb_data, size_t espb_size, espb_handle_t *out_handle) {
    printf("--- ESPB Loading Module ---\n");

    int64_t load_stage_start_us = esp_timer_get_time();
    iram_pool_init_wrapper();
    const int64_t iram_pool_us = esp_timer_get_time() - load_stage_start_us;

    load_stage_start_us = esp_timer_get_time();
    if (espb_callback_system_init() != ESPB_OK) return ESPB_ERR_INVALID_STATE;
    const int64_t callback_init_us = esp_timer_get_time() - load_stage_start_us;

    load_stage_start_us = esp_timer_get_time();
    init_cpp_symbols();
    const int64_t symbol_init_us = esp_timer_get_time() - load_stage_start_us;

    EspbModule *module = NULL;
    load_stage_start_us = esp_timer_get_time();
    EspbResult result = espb_parse_module(&module, espb_data, espb_size);
    const int64_t parse_us = esp_timer_get_time() - load_stage_start_us;
    if (result != ESPB_OK) return result;

    EspbInstance *instance = NULL;
    load_stage_start_us = esp_timer_get_time();
    result = espb_instantiate(&instance, module);
    const int64_t instantiate_us = esp_timer_get_time() - load_stage_start_us;
    if (result != ESPB_OK) {
        espb_free_module(module);
        return result;
    }

    espb_handle_t handle = (espb_handle_t)malloc(sizeof(struct espb_module_handle_t));
    if (!handle) {
        espb_free_instance(instance);
        espb_free_module(module);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    handle->module = module;
    handle->instance = instance;
    handle->num_func_imports = 0;
    for (uint32_t i = 0; i < module->num_imports; ++i) {
        if (module->imports[i].kind == ESPB_IMPORT_KIND_FUNC) {
            ++handle->num_func_imports;
        }
    }
    *out_handle = handle;

    printf("[ESPB_LOAD_TIME] iram_pool_us=%lld callback_init_us=%lld symbol_init_us=%lld parse_us=%lld instantiate_us=%lld\n",
           (long long)iram_pool_us, (long long)callback_init_us,
           (long long)symbol_init_us, (long long)parse_us,
           (long long)instantiate_us);
    printf("--- ESPB Module Loaded ---\n");
    return ESPB_OK;
}

void espb_unload_module(espb_handle_t handle) {
    if (handle) {
        printf("--- ESPB Unloading Module ---\n");
        // The API handle owns both the instance and parsed module metadata.
        // Closures retain pointers to this instance/module; release before freeing either.
        espb_free_callback_closures_for_instance(handle->instance);
        espb_free_instance(handle->instance);
        espb_free_module(handle->module);
        free(handle);
        printf("--- ESPB Module Unloaded ---\n");
    }
}


EspbResult espb_call_function_sync(espb_handle_t handle, const char* function_name, const Value *args, uint32_t num_args, Value *results) {
    if (!handle) return ESPB_ERR_INVALID_STATE;

    // Поиск функции
    uint32_t func_idx_to_call = 0;
    bool found_func = false;
    for (uint32_t i = 0; i < handle->module->num_exports; ++i) {
        if (handle->module->exports[i].kind == ESPB_IMPORT_KIND_FUNC &&
            strcmp(handle->module->exports[i].name, function_name) == 0) {
            func_idx_to_call = handle->module->exports[i].index + handle->num_func_imports;
            found_func = true;
            break;
        }
    }

    if (!found_func) return ESPB_ERR_INVALID_FUNC_INDEX;

    ExecutionContext *exec_ctx = init_execution_context();
    if (!exec_ctx) {
        return ESPB_ERR_MEMORY_ALLOC;
    }

    EspbResult result = espb_execute_function(handle->instance, exec_ctx, func_idx_to_call, args, results);

    free_execution_context(exec_ctx);

    return result;
}
