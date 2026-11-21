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
#include "espb_jit_precompile.h"
#include "espb_jit.h"
#include "espb_interpreter.h"
#include "esp_log.h"
#include <string.h>

#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

#if ESPB_JIT_DEBUG
#define JIT_LOGI ESP_LOGI
#define JIT_LOGW ESP_LOGW
#define JIT_LOGD ESP_LOGD
#else
#define JIT_LOGI(tag, fmt, ...) ((void)0)
#define JIT_LOGW(tag, fmt, ...) ((void)0)
#define JIT_LOGD(tag, fmt, ...) ((void)0)
#endif

static const char* TAG = "espb_jit_precompile";

EspbResult espb_jit_precompile_hot_functions(EspbInstance* instance, espb_jit_precompile_progress_cb progress_cb) {
    if (!instance || !instance->module) {
        ESP_LOGE(TAG, "Invalid instance");
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    const EspbModule* module = instance->module;
    EspbResult last_error = ESPB_OK;
    uint32_t hot_count = 0;
    uint32_t compiled_count = 0;
    
    // Первый проход: подсчитываем количество HOT функций
    for (uint32_t i = 0; i < module->num_functions; i++) {
        EspbFunctionBody* body = &module->function_bodies[i];
        if (body->header.flags & ESPB_FUNC_FLAG_HOT) {
            hot_count++;
        }
    }
    
    if (hot_count == 0) {
        JIT_LOGI(TAG, "No HOT functions found in module");
        return ESPB_OK;
    }
    
    JIT_LOGI(TAG, "Precompiling %u HOT functions...", hot_count);
    
    // Второй проход: компилируем HOT функции
    uint32_t current = 0;
    for (uint32_t i = 0; i < module->num_functions; i++) {
        EspbFunctionBody* body = &module->function_bodies[i];
        
        // Проверяем флаг HOT
        if (!(body->header.flags & ESPB_FUNC_FLAG_HOT)) {
            continue; // Пропускаем не-HOT функции
        }
        
        current++;
        
        // Проверяем, не скомпилирована ли уже
        if (body->is_jit_compiled && body->jit_code != NULL) {
            compiled_count++;
            if (progress_cb) {
                progress_cb(current, hot_count, NULL);
            }
            continue;
        }
        
        // Компилируем
        void* jit_code = NULL;
        size_t jit_size = 0;
        uint32_t global_func_idx = i + module->num_imported_funcs;
        
        EspbResult res = espb_jit_compile_function(instance, global_func_idx, body, &jit_code, &jit_size);
        
        if (res == ESPB_OK) {
            body->jit_code = jit_code;
            body->jit_code_size = jit_size;
            body->is_jit_compiled = true;
            compiled_count++;
            
            // Добавляем в cache
            if (instance->jit_cache) {
                espb_jit_cache_insert(instance->jit_cache, global_func_idx, jit_code, jit_size);
            }
            
            ESP_LOGI(TAG, "[%u/%u] Precompiled HOT function #%u (%zu bytes)", 
                    current, hot_count, i, jit_size);
        } else {
            JIT_LOGW(TAG, "[%u/%u] Failed to precompile HOT function #%u (error %d)", 
                    current, hot_count, i, res);
            last_error = res;
        }
        
        if (progress_cb) {
            progress_cb(current, hot_count, NULL);
        }
    }
    
    JIT_LOGI(TAG, "Precompilation complete: %u/%u HOT functions compiled successfully", 
            compiled_count, hot_count);
    
    return last_error;
}

EspbResult espb_jit_precompile_function(EspbInstance* instance, uint32_t func_idx) {
    if (!instance || !instance->module) {
        ESP_LOGE(TAG, "Invalid instance");
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    const EspbModule* module = instance->module;
    uint32_t num_imported_funcs = module->num_imported_funcs;
    
    // Проверяем, что это внутренняя функция
    if (func_idx < num_imported_funcs || func_idx >= num_imported_funcs + module->num_functions) {
        ESP_LOGE(TAG, "Invalid function index %u", func_idx);
        return ESPB_ERR_INVALID_FUNC_INDEX;
    }
    
    uint32_t local_func_idx = func_idx - num_imported_funcs;
    EspbFunctionBody* body = &module->function_bodies[local_func_idx];
    
    // Проверяем, не скомпилирована ли уже
    if (body->is_jit_compiled && body->jit_code != NULL) {
        JIT_LOGD(TAG, "Function #%u already compiled", func_idx);
        return ESPB_OK;
    }
    
    // Компилируем
    void* jit_code = NULL;
    size_t jit_size = 0;
    
    EspbResult res = espb_jit_compile_function(instance, func_idx, body, &jit_code, &jit_size);
    
    if (res == ESPB_OK) {
        body->jit_code = jit_code;
        body->jit_code_size = jit_size;
        body->is_jit_compiled = true;
        
        // Добавляем в cache
        if (instance->jit_cache) {
            espb_jit_cache_insert(instance->jit_cache, func_idx, jit_code, jit_size);
        }
        
        JIT_LOGI(TAG, "Precompiled function #%u (%zu bytes)", func_idx, jit_size);
    } else {
        ESP_LOGE(TAG, "Failed to precompile function #%u (error %d)", func_idx, res);
    }
    
    return res;
}

EspbResult espb_jit_precompile_by_name(EspbInstance* instance, const char* func_name) {
    if (!instance || !instance->module || !func_name) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    const EspbModule* module = instance->module;
    
    // Ищем функцию по имени в экспортах
    for (uint32_t i = 0; i < module->num_exports; i++) {
        if (module->exports[i].kind == ESPB_IMPORT_KIND_FUNC) {
            if (strcmp(module->exports[i].name, func_name) == 0) {
                uint32_t func_idx = module->exports[i].index;
                JIT_LOGI(TAG, "Found function '%s' at index %u", func_name, func_idx);
                return espb_jit_precompile_function(instance, func_idx);
            }
        }
    }
    
    ESP_LOGE(TAG, "Function '%s' not found in exports", func_name);
    return ESPB_ERR_UNKNOWN_OPCODE; // Используем существующую константу вместо ESPB_ERR_NOT_FOUND
}

EspbResult espb_jit_get_stats(EspbInstance* instance, uint32_t* out_total, uint32_t* out_hot, 
                               uint32_t* out_compiled, size_t* out_jit_size) {
    if (!instance || !instance->module) {
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    const EspbModule* module = instance->module;
    uint32_t total = module->num_functions;
    uint32_t hot = 0;
    uint32_t compiled = 0;
    size_t jit_size = 0;
    
    for (uint32_t i = 0; i < module->num_functions; i++) {
        EspbFunctionBody* body = &module->function_bodies[i];
        
        if (body->header.flags & ESPB_FUNC_FLAG_HOT) {
            hot++;
        }
        
        if (body->is_jit_compiled && body->jit_code != NULL) {
            compiled++;
            jit_size += body->jit_code_size;
        }
    }
    
    if (out_total) *out_total = total;
    if (out_hot) *out_hot = hot;
    if (out_compiled) *out_compiled = compiled;
    if (out_jit_size) *out_jit_size = jit_size;
    
    return ESPB_OK;
}
