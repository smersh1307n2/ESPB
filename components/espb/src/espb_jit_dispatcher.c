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
#include "espb_jit_dispatcher.h"
#include "espb_interpreter.h" // Для espb_call_function

#include <stdio.h>
#include <string.h>  // Для memset

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_memory_utils.h"  // for esp_ptr_executable, esp_ptr_in_iram, esp_ptr_in_dram
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// БЫСТРОЕ ОТКЛЮЧЕНИЕ JIT: раскомментируйте следующие 2 строки
 //#undef CONFIG_ESPB_JIT_ENABLED
 //#define CONFIG_ESPB_JIT_ENABLED 0

#if CONFIG_ESPB_JIT_ENABLED
#include "espb_jit.h"         // Для espb_jit_compile_function
#endif

#if CONFIG_ESPB_JIT_ENABLED
static size_t total_jit_size = 0;
#endif

static uint8_t espb_get_declared_num_args_for_local(const EspbInstance *instance, uint32_t local_func_idx) {
    if (!instance || !instance->module) return 0;
    const EspbModule *m = instance->module;
    if (!m->function_signature_indices || !m->signatures) return 0;
    if (local_func_idx >= m->num_functions) return 0;
    uint16_t sig_idx = m->function_signature_indices[local_func_idx];
    if (sig_idx >= m->num_signatures) return 0;
    const EspbFuncSignature *sig = &m->signatures[sig_idx];
    return sig ? sig->num_params : 0;
}
EspbResult espb_execute_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results) {
#if CONFIG_ESPB_JIT_ENABLED
    if (!instance || !instance->module) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    // FAST PATH: если в модуле вообще нет HOT функций, JIT не может быть использован.
    // В этом случае важно не платить за какие-либо JIT-проверки и сразу идти в интерпретатор.
    if (__builtin_expect(instance->jit_hot_function_count == 0, 1)) {
        return espb_call_function(instance, exec_ctx, func_idx, args, results);
    }

    // Проверка, что func_idx находится в пределах внутренних функций модуля
    uint32_t num_imported_funcs = instance->module->num_imported_funcs;
    if (func_idx < num_imported_funcs || func_idx >= (num_imported_funcs + instance->module->num_functions)) {
        // TODO: Обработка вызова импортированных функций, если это необходимо
        // В данный момент диспетчер работает только для внутренних функций.
        return espb_call_function(instance, exec_ctx, func_idx, args, results);
    }

    uint32_t local_func_idx = func_idx - num_imported_funcs;
    EspbFunctionBody *body = &instance->module->function_bodies[local_func_idx];

    // Проверяем флаг ESPB_FUNC_FLAG_HOT (0x40) - должна ли функция компилироваться через JIT
    bool is_hot_function = (body->header.flags & ESPB_FUNC_FLAG_HOT) != 0;

    // Проверяем, скомпилирована ли функция
    if (body->is_jit_compiled && body->jit_code != NULL) {
        // JIT-путь
        uint8_t num_args = espb_get_declared_num_args_for_local(instance, local_func_idx);
        return execute_jit_code(instance, body->jit_code, args, num_args, results, body->header.num_virtual_regs);
    }
    
    // Функция НЕ скомпилирована
    // Компилируем ТОЛЬКО если установлен флаг ESPB_FUNC_FLAG_HOT
    if (!is_hot_function) {
        // Функция НЕ помечена как HOT - выполняем через интерпретатор (по умолчанию)
        return espb_call_function(instance, exec_ctx, func_idx, args, results);
    }

    // Функция помечена как HOT - пытаемся JIT-компиляцию
    void* jit_code = NULL;
    size_t jit_size = 0;
    EspbResult jit_res = espb_jit_compile_function(instance, func_idx, body, &jit_code, &jit_size);

    if (jit_res == ESPB_OK) {
        total_jit_size += jit_size;
        printf("[JIT] Compiled HOT function %u, code size: %zu bytes. Total JIT size: %zu bytes\n", (unsigned)func_idx, jit_size, total_jit_size);
        body->jit_code = jit_code;
        body->jit_code_size = jit_size;
        body->is_jit_compiled = true;
        
        // Добавляем в JIT cache
        if (instance->jit_cache) {
            espb_jit_cache_insert(instance->jit_cache, func_idx, jit_code, jit_size);
        }
        
        // Выполняем через JIT
        uint8_t num_args = espb_get_declared_num_args_for_local(instance, local_func_idx);
        return execute_jit_code(instance, body->jit_code, args, num_args, results, body->header.num_virtual_regs);
    } else {
        // Если JIT-компиляция не удалась, выполняем в интерпретаторе
        printf("[JIT] Failed to compile HOT function %u (error %d), using interpreter\n", (unsigned)func_idx, jit_res);
        return espb_call_function(instance, exec_ctx, func_idx, args, results);
    }
#else
    return espb_call_function(instance, exec_ctx, func_idx, args, results);
#endif
}

EspbResult espb_execute_function_jit_only(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results) {
#if CONFIG_ESPB_JIT_ENABLED
    if (!instance || !instance->module) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    uint32_t num_imported_funcs = instance->module->num_imported_funcs;

    // JIT-only поддерживает только внутренние (не импортированные) функции.
    if (func_idx < num_imported_funcs || func_idx >= (num_imported_funcs + instance->module->num_functions)) {
        return ESPB_ERR_INVALID_FUNC_INDEX;
    }

    uint32_t local_func_idx = func_idx - num_imported_funcs;
    EspbFunctionBody *body = &instance->module->function_bodies[local_func_idx];

    // Если уже скомпилировано — сразу выполняем.
    if (body->is_jit_compiled && body->jit_code != NULL) {
        uint8_t num_args = espb_get_declared_num_args_for_local(instance, local_func_idx);
        return execute_jit_code(instance, body->jit_code, args, num_args, results, body->header.num_virtual_regs);
    }

    // Пытаемся скомпилировать. Если не вышло — возвращаем ошибку (без interpreter fallback).
    void *jit_code = NULL;
    size_t jit_size = 0;
    EspbResult jit_res = espb_jit_compile_function(instance, func_idx, body, &jit_code, &jit_size);
    if (jit_res != ESPB_OK) {
        return jit_res;
    }

    total_jit_size += jit_size;
    printf("[JIT] Compiled function %u, code size: %zu bytes. Total JIT size: %zu bytes\n", (unsigned)func_idx, jit_size, total_jit_size);
    body->jit_code = jit_code;
    body->jit_code_size = jit_size;
    body->is_jit_compiled = true;

    uint8_t num_args = espb_get_declared_num_args_for_local(instance, local_func_idx);
    return execute_jit_code(instance, body->jit_code, args, num_args, results, body->header.num_virtual_regs);
#else
    return ESPB_ERR_UNSUPPORTED; // JIT is disabled
#endif
}

#if CONFIG_ESPB_JIT_ENABLED
EspbResult execute_jit_code(EspbInstance *instance, void *jit_code, const Value *args, uint8_t num_args, Value *results, uint16_t num_virtual_regs) {
    if (!jit_code) {
        printf("JIT: Error - jit_code is NULL\n");
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    // Валидация JIT кода перед выполнением
    if (!esp_ptr_executable(jit_code)) {
        printf("[JIT ERROR] JIT code at %p is NOT in executable memory!\n", jit_code);
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    if (esp_ptr_in_dram(jit_code)) {
        printf("[JIT ERROR] JIT code at %p is in DRAM - cannot execute from DRAM!\n", jit_code);
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    // Проверяем валидность instance
    if (!instance || !instance->module) {
        printf("[JIT ERROR] Invalid instance: %p\n", instance);
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    // Указатель на скомпилированный JIT код как функцию
    // Сигнатура: void (*jit_func)(EspbInstance *instance, Value *v_regs)
    typedef void (*JitFunc)(EspbInstance *instance, Value *v_regs);
    
    JitFunc jit_func = (JitFunc)jit_code;
    
    // ОПТИМИЗАЦИЯ: Выделяем только необходимое количество регистров из метаданных функции
    // Ограничиваем максимум 256 регистров для безопасности
    uint16_t actual_regs = num_virtual_regs > 256 ? 256 : num_virtual_regs;
    if (actual_regs < 8) actual_regs = 8; // Минимум 8 для аргументов

    // Выделяем v_regs динамически на стеке (VLA - Variable Length Array)
    Value v_regs[actual_regs] __attribute__((aligned(8)));
    memset(v_regs, 0, actual_regs * sizeof(Value));
    
    // Копируем аргументы в регистры R0-R7 (или сколько есть)
    if (args) {
        uint8_t n = num_args;
        if (n > actual_regs) n = actual_regs;
        for (uint8_t i = 0; i < n; i++) {
            v_regs[i] = args[i];
        }
    }
    
    // ОПТИМИЗАЦИЯ #2: Убраны избыточные проверки (только в debug режиме)
    #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    // Проверка выравнивания только в debug
    if (((uintptr_t)v_regs & 0x7) != 0) {
        printf("JIT ERROR: v_regs not aligned: %p\n", v_regs);
    }
    #endif
    
    // ОПТИМИЗАЦИЯ #3: fence.i убран - он нужен только ОДИН РАЗ после компиляции
    // (уже выполняется в espb_jit_compile_function)
    
    // Вызываем JIT-скомпилированный код
    ESP_LOGD("espb_jit", "Calling JIT function at %p with instance=%p, v_regs=%p", jit_func, instance, v_regs);
    
    jit_func(instance, v_regs);
    ESP_LOGD("espb_jit", "JIT function returned");
    
    // Копируем результаты из регистров обратно (R0 содержит возвращаемое значение)
    if (results) {
        *results = v_regs[0];
    }
    
    return ESPB_OK;
}
#endif
