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

#include "espb_callback_system.h"
#include "espb_interpreter_runtime_oc.h"
#include "esp_log.h"
#include "espb_jit_dispatcher.h"
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"

// Включаем заголовочные файлы ESP-IDF для функций работы с памятью
// Эти функции определены в ESP-IDF SDK
#include "esp_attr.h"

// Включаем заголовочные файлы ESP-IDF для функций работы с памятью
#include <esp_memory_utils.h>

static const char *TAG = "espb_callback";

// Глобальная callback система
static EspbCallbackSystem g_callback_system = {0};

// Счетчик вызовов callback для отладки
static uint32_t g_callback_invocation_count = 0;
static TickType_t g_last_callback_tick = 0;

// Вспомогательная функция для преобразования ESPB типа в FFI тип
// КРИТИЧНАЯ ФУНКЦИЯ: часто вызывается, размещаем в IRAM
ESPB_CALLBACK_CRITICAL
static ffi_type* espb_type_to_ffi_type_internal(EspbValueType es_type) {
    switch (es_type) {
        case ESPB_TYPE_VOID: return &ffi_type_void;
        case ESPB_TYPE_I8:   return &ffi_type_sint8;
        case ESPB_TYPE_U8:   return &ffi_type_uint8;
        case ESPB_TYPE_I16:  return &ffi_type_sint16;
        case ESPB_TYPE_U16:  return &ffi_type_uint16;
        case ESPB_TYPE_I32:  return &ffi_type_sint32;
        case ESPB_TYPE_U32:  return &ffi_type_uint32;
        case ESPB_TYPE_I64:  return &ffi_type_sint64;
        case ESPB_TYPE_U64:  return &ffi_type_uint64;
        case ESPB_TYPE_F32:  return &ffi_type_float;
        case ESPB_TYPE_F64:  return &ffi_type_double;
        case ESPB_TYPE_PTR:  return &ffi_type_pointer;
        case ESPB_TYPE_BOOL: return &ffi_type_sint32;
        default: return NULL;
    }
}

EspbResult espb_callback_system_init(void) {
    if (g_callback_system.initialized) {
        return ESPB_OK; // Уже инициализирована
    }

    g_callback_system.mutex = xSemaphoreCreateMutex();
    if (!g_callback_system.mutex) {
        ESP_LOGE(TAG, "Failed to create callback system mutex");
        return ESPB_ERR_MEMORY_ALLOC;
    }

    g_callback_system.active_closures = NULL;
    g_callback_system.initialized = true;

    // IRAM pool уже инициализирован в app_main(), просто выводим статус
#if defined(CONFIG_LIBFFI_USE_IRAM_POOL)
   ESP_LOGD(TAG, "Using existing IRAM pool for libffi closures");
    
    // Выводим отладочную информацию о состоянии IRAM pool
    // extern void iram_pool_debug(void);
    // iram_pool_debug();
#else
   ESP_LOGD(TAG, "Using standard heap for libffi closures (IRAM pool disabled)");
#endif

   ESP_LOGD(TAG, "Universal callback system initialized successfully");
    return ESPB_OK;
}

EspbResult espb_get_active_closures(EspbClosureCtx **out_active_closures) {
    if (out_active_closures) {
        *out_active_closures = (EspbClosureCtx*)g_callback_system.active_closures;
    }

    return ESPB_OK;
}

void espb_callback_system_deinit(void) {
    if (!g_callback_system.initialized) {
        return;
    }

    // Освобождаем все активные замыкания
    if (xSemaphoreTake(g_callback_system.mutex, portMAX_DELAY) == pdTRUE) {
        EspbCallbackClosure *current = g_callback_system.active_closures;
        while (current) {
            EspbCallbackClosure *next = current->next;
            
            if (current->closure_ptr) {
                ffi_closure_free(current->closure_ptr);
            }
            
            if (current->callback_info) {
                free(current->callback_info);
            }
            
            free(current);
            current = next;
        }
        
        g_callback_system.active_closures = NULL;
        xSemaphoreGive(g_callback_system.mutex);
    }

    if (g_callback_system.mutex) {
        vSemaphoreDelete(g_callback_system.mutex);
        g_callback_system.mutex = NULL;
    }

    g_callback_system.initialized = false;
   ESP_LOGD(TAG, "Callback system deinitialized");
}

bool espb_callback_get_iram_pool_stats(size_t *out_total_size, size_t *out_used_size, size_t *out_free_size) {
#if defined(CONFIG_LIBFFI_USE_IRAM_POOL)
    // Проверяем, есть ли реальная реализация IRAM pool или только заглушка
    // Если есть реальная реализация в libffi/src/common/iram_pool.c
    extern size_t iram_pool_get_total_size(void) __attribute__((weak));
    extern size_t iram_pool_get_used_size(void) __attribute__((weak));
    extern size_t iram_pool_get_free_size(void) __attribute__((weak));
    
    if (iram_pool_get_total_size && iram_pool_get_used_size && iram_pool_get_free_size) {
        // Реальная реализация IRAM pool доступна
        if (out_total_size) {
            *out_total_size = iram_pool_get_total_size();
        }
        
        if (out_used_size) {
            *out_used_size = iram_pool_get_used_size();
        }
        
        if (out_free_size) {
            *out_free_size = iram_pool_get_free_size();
        }
        
        return true;
    } else {
        // Только заглушка в main.cpp, возвращаем примерные значения
        ESP_LOGW(TAG, "Using stub IRAM pool implementation - statistics not available");
        if (out_total_size) *out_total_size = 8192;  // Примерный размер
        if (out_used_size) *out_used_size = 0;       // Неизвестно
        if (out_free_size) *out_free_size = 8192;    // Примерный размер
        
        return false; // Указываем, что статистика неточная
    }
#else
    // IRAM pool не включен
    if (out_total_size) *out_total_size = 0;
    if (out_used_size) *out_used_size = 0;
    if (out_free_size) *out_free_size = 0;
    
    return false;
#endif
}

ESPB_CALLBACK_CRITICAL
void espb_universal_callback_handler(ffi_cif *cif, void *ret_value, void **ffi_args, void *user_data) {
    EspbCallbackClosure *closure = (EspbCallbackClosure*)user_data;
    if (!closure || !closure->callback_info || !closure->instance) {
        ESP_LOGE(TAG, "Invalid callback closure data");
        return;
    }

    EspbCallbackInfo *info = closure->callback_info;
    EspbInstance *instance = closure->instance;
    
    // Увеличиваем счетчик вызовов и измеряем время
    g_callback_invocation_count++;
    TickType_t current_tick = xTaskGetTickCount();
    TickType_t delta_ticks = (g_last_callback_tick > 0) ? (current_tick - g_last_callback_tick) : 0;
    uint32_t delta_ms = delta_ticks * portTICK_PERIOD_MS;
    
   ESP_LOGD(TAG, "=== TIMER CALLBACK TRIGGERED #%lu ===", (unsigned long)g_callback_invocation_count);
   ESP_LOGD(TAG, "Time since last callback: %lu ms (should be ~2000ms)", (unsigned long)delta_ms);
   ESP_LOGD(TAG, "Universal callback handler: espb_func_idx=%lu, params=%u", 
             (unsigned long)info->espb_func_idx, info->espb_signature->num_params);
   ESP_LOGD(TAG, "Native args count: %u", cif ? (uint32_t)cif->nargs : 0);
   ESP_LOGD(TAG, "Now executing ESPB test_timer_cb function...");
    
    // КРИТИЧЕСКАЯ ДИАГНОСТИКА: проверяем состояние системы
   ESP_LOGD(TAG, "Current tick: %lu, heap free: %u bytes", 
             (unsigned long)xTaskGetTickCount(), esp_get_free_heap_size());
   ESP_LOGD(TAG, "ESPB instance: %p, module: %p, memory_data: %p", 
             instance, instance ? instance->module : NULL, 
             instance ? instance->memory_data : NULL);
    
    g_last_callback_tick = current_tick;

    // СОЗДАТЬ ЛОКАЛЬНЫЙ КОНТЕКСТ
    ExecutionContext *callback_exec_ctx = init_execution_context();
    if (!callback_exec_ctx) {
        ESP_LOGE(TAG, "Failed to create execution context for callback");
        return;
    }

    // Подготавливаем аргументы для ESPB функции
    Value espb_args[ESPB_CALLBACK_MAX_PARAMS];
    uint32_t num_params = info->espb_signature->num_params;
    uint32_t native_nargs = cif ? (uint32_t)cif->nargs : 1; // Timer callback всегда имеет 1 аргумент
    
    // ИСПРАВЛЕНИЕ: Если ESPB сигнатура показывает 0 параметров, но это timer callback, передаем 1
    uint32_t actual_params_to_pass = (num_params == 0 && native_nargs == 1) ? 1 : num_params;
    uint32_t copy_limit = (native_nargs < actual_params_to_pass) ? native_nargs : actual_params_to_pass;
    if (copy_limit > ESPB_CALLBACK_MAX_PARAMS) {
        copy_limit = ESPB_CALLBACK_MAX_PARAMS;
    }
    
   ESP_LOGD(TAG, "ESPB signature expects %u params, native provides %u args, will pass %u args", 
             num_params, native_nargs, actual_params_to_pass);

    // Конвертируем аргументы из FFI в ESPB формат
    for (uint32_t i = 0; i < copy_limit; ++i) {
        EspbValueType es_type = info->espb_signature->param_types[i];
        SET_TYPE(espb_args[i], es_type);

        // Специальная обработка user_data параметра
        if ((int32_t)i == info->user_data_param_index && es_type == ESPB_TYPE_PTR) {
            V_PTR(espb_args[i]) = info->original_user_data;
           ESP_LOGD(TAG, "Callback arg[%lu] user_data: %p", (unsigned long)i, info->original_user_data);
            continue;
        }
        
        // Для timer callback: первый аргумент должен быть TimerHandle_t (как в рабочем коде)
        if (i == 0 && es_type == ESPB_TYPE_PTR && ffi_args && copy_limit > 0) {
            // ИСПРАВЛЕНИЕ: Передаем TimerHandle_t в ESPB функцию, а не user_data
            // ESPB функция должна сама вызвать pvTimerGetTimerID если нужно
            TimerHandle_t timer_handle = *(TimerHandle_t*)ffi_args[0];
            V_PTR(espb_args[i]) = timer_handle;
           ESP_LOGD(TAG, "Callback arg[%lu] FreeRTOS timer: passing TimerHandle_t=%p to ESPB", (unsigned long)i, timer_handle);
            continue;
        }

        // Стандартная конвертация типов
        switch (es_type) {
            case ESPB_TYPE_I8:   V_I32(espb_args[i]) = *(int8_t*)ffi_args[i]; break;
            case ESPB_TYPE_U8:   V_I32(espb_args[i]) = *(uint8_t*)ffi_args[i]; break;
            case ESPB_TYPE_I16:  V_I32(espb_args[i]) = *(int16_t*)ffi_args[i]; break;
            case ESPB_TYPE_U16:  V_I32(espb_args[i]) = *(uint16_t*)ffi_args[i]; break;
            case ESPB_TYPE_I32:  V_I32(espb_args[i]) = *(int32_t*)ffi_args[i]; break;
            case ESPB_TYPE_U32:  V_I32(espb_args[i]) = *(uint32_t*)ffi_args[i]; break;
            case ESPB_TYPE_I64:  V_I64(espb_args[i]) = *(int64_t*)ffi_args[i]; break;
            case ESPB_TYPE_U64:  V_I64(espb_args[i]) = *(uint64_t*)ffi_args[i]; break;
            case ESPB_TYPE_F32:  V_F32(espb_args[i]) = *(float*)ffi_args[i]; break;
            case ESPB_TYPE_F64:  V_F64(espb_args[i]) = *(double*)ffi_args[i]; break;
            case ESPB_TYPE_PTR:  V_PTR(espb_args[i]) = *(void**)ffi_args[i]; break;
            case ESPB_TYPE_BOOL: V_I32(espb_args[i]) = *(int32_t*)ffi_args[i]; break;
            default:
                V_I32(espb_args[i]) = *(int32_t*)ffi_args[i];
                SET_TYPE(espb_args[i], ESPB_TYPE_I32);
                break;
        }

        ESP_LOGD(TAG, "Callback arg[%lu] type=%d converted", (unsigned long)i, es_type);
    }

    // Синхронизация больше не нужна - используем разделяемый контекст выполнения

    // Вызываем ESPB функцию
    Value result = {0};
    Value *result_ptr = (info->espb_signature->num_returns > 0) ? &result : NULL;
    
   ESP_LOGD(TAG, "Calling ESPB function %lu with %u parameters (actual_params_to_pass=%u)", 
             (unsigned long)info->espb_func_idx, num_params, actual_params_to_pass);
    
    // Выводим все параметры для отладки
    for (uint32_t i = 0; i < num_params && i < copy_limit; ++i) {
       ESP_LOGD(TAG, "  Param[%lu]: value=0x%08lx",
                 (unsigned long)i,
                 (unsigned long)V_I32(espb_args[i]));
    }
    
  
    // КРИТИЧЕСКАЯ ПРОВЕРКА: убеждаемся что передаем правильные аргументы
   ESP_LOGD(TAG, "Calling espb_call_function(instance=%p, exec_ctx=%p, func_idx=%lu, args=%p, result=%p)",
             instance, callback_exec_ctx, (unsigned long)info->espb_func_idx, espb_args, result_ptr);
    
    // Проверяем, что у нас есть хотя бы один аргумент для передачи
    const Value *args_to_pass = (copy_limit > 0) ? espb_args : NULL;
    
    EspbResult call_result = espb_execute_function(instance, callback_exec_ctx,
                                                 info->espb_func_idx, args_to_pass, result_ptr);
    
   ESP_LOGD(TAG, "*** espb_call_function RETURNED: result=%d ***", call_result);
    
    // Синхронизация больше не нужна - состояние автоматически разделяется
    
    // КРИТИЧЕСКАЯ ДИАГНОСТИКА: детальный анализ результата
    if (call_result == ESPB_OK) {
       ESP_LOGD(TAG, "*** SUCCESS: ESPB function executed successfully ***");
    } else {
        ESP_LOGE(TAG, "*** FAILURE: ESPB function failed with code %d ***", call_result);
        ESP_LOGE(TAG, "*** This explains why printf doesn't appear ***");
    }

    if (call_result != ESPB_OK) {
        ESP_LOGE(TAG, "Callback ESPB function call failed: %d", call_result);
    } else {
       ESP_LOGD(TAG, "Callback ESPB function executed successfully");
        if (result_ptr && info->espb_signature->num_returns > 0) {
           ESP_LOGD(TAG, "  Return value: value=0x%08lx",
                     (unsigned long)V_I32(result));
        }
        
        // Симуляция больше не нужна - реальная ESPB функция должна работать с разделяемым контекстом
    }

    // Обрабатываем возвращаемое значение, если есть
    if (ret_value && result_ptr && info->espb_signature->num_returns > 0) {
        EspbValueType ret_type = info->espb_signature->return_types[0];
        switch (ret_type) {
            case ESPB_TYPE_I8:   *(int8_t*)ret_value   = (int8_t)V_I32(result); break;
            case ESPB_TYPE_U8:   *(uint8_t*)ret_value  = (uint8_t)V_I32(result); break;
            case ESPB_TYPE_I16:  *(int16_t*)ret_value  = (int16_t)V_I32(result); break;
            case ESPB_TYPE_U16:  *(uint16_t*)ret_value = (uint16_t)V_I32(result); break;
            case ESPB_TYPE_I32:  *(int32_t*)ret_value  = V_I32(result); break;
            case ESPB_TYPE_U32:  *(uint32_t*)ret_value = (uint32_t)V_I32(result); break;
            case ESPB_TYPE_I64:  *(int64_t*)ret_value  = V_I64(result); break;
            case ESPB_TYPE_U64:  *(uint64_t*)ret_value = (uint64_t)V_I64(result); break;
            case ESPB_TYPE_F32:  *(float*)ret_value    = V_F32(result); break;
            case ESPB_TYPE_F64:  *(double*)ret_value   = V_F64(result); break;
            case ESPB_TYPE_PTR:  *(void**)ret_value    = V_PTR(result); break;
            case ESPB_TYPE_BOOL: *(int32_t*)ret_value  = V_I32(result); break;
            default: break;
        }
    }

    // Принудительный вывод больше не нужен - реальная ESPB функция должна работать
    
    // УНИЧТОЖИТЬ ЛОКАЛЬНЫЙ КОНТЕКСТ
    free_execution_context(callback_exec_ctx);
    
   ESP_LOGD(TAG, "=== TIMER CALLBACK COMPLETED ===");
}

EspbResult espb_create_callback_closure(
    EspbInstance *instance,
    uint16_t import_idx,
    uint8_t callback_param_idx,
    uint32_t espb_func_idx,
    int32_t user_data_param_idx,
    void *original_user_data,
    void **out_closure_ptr
) {
    if (!g_callback_system.initialized) {
        EspbResult init_result = espb_callback_system_init();
        if (init_result != ESPB_OK) {
            return init_result;
        }
    }

    if (!instance || !out_closure_ptr) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    const EspbModule *module = instance->module;
    if (import_idx >= module->num_imports) {
        ESP_LOGE(TAG, "Invalid import index: %u", import_idx);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // Получаем сигнатуру импортируемой функции
    EspbImportDesc *import_desc = &module->imports[import_idx];
    if (import_desc->kind != ESPB_IMPORT_KIND_FUNC) {
        ESP_LOGE(TAG, "Import is not a function");
        return ESPB_ERR_INVALID_OPERAND;
    }

    uint16_t sig_idx = import_desc->desc.func.type_idx;
    if (sig_idx >= module->num_signatures) {
        ESP_LOGE(TAG, "Invalid signature index: %u", sig_idx);
        return ESPB_ERR_INVALID_OPERAND;
    }

    EspbFuncSignature *import_sig = &module->signatures[sig_idx];

    // Проверяем, что callback_param_idx корректен
    if (callback_param_idx >= import_sig->num_params) {
        ESP_LOGE(TAG, "Invalid callback parameter index: %u", callback_param_idx);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // Получаем сигнатуру ESPB функции для callback
    // FIX: The espb_func_idx passed for a callback is the LOCAL function index, not the global one.
    // The validation must be done against num_functions directly.
    uint32_t local_func_idx = espb_func_idx;
    if (local_func_idx >= module->num_functions) {
        ESP_LOGE(TAG, "Invalid ESPB local function index for callback: %lu (num_functions: %u)", 
                 (unsigned long)local_func_idx, module->num_functions);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // For later operations (like calling espb_call_function), we need the global index.
    uint32_t num_imported_funcs = 0;
    for (uint32_t i = 0; i < module->num_imports; ++i) {
        if (module->imports[i].kind == ESPB_IMPORT_KIND_FUNC) {
            num_imported_funcs++;
        }
    }
    uint32_t global_func_idx = local_func_idx + num_imported_funcs;

    uint16_t espb_sig_idx = module->function_signature_indices[local_func_idx];
    EspbFuncSignature *espb_sig = &module->signatures[espb_sig_idx];
    
   ESP_LOGD(TAG, "ESPB callback function: global_idx=%lu, local_idx=%lu, sig_idx=%u, params=%u, returns=%u",
             (unsigned long)global_func_idx, (unsigned long)local_func_idx, espb_sig_idx, 
             espb_sig->num_params, espb_sig->num_returns);

    // Создаем структуру информации о callback
    EspbCallbackInfo *callback_info = (EspbCallbackInfo*)calloc(1, sizeof(EspbCallbackInfo));
    if (!callback_info) {
        ESP_LOGE(TAG, "Failed to allocate callback info");
        return ESPB_ERR_MEMORY_ALLOC;
    }

    callback_info->espb_func_idx = global_func_idx;
    callback_info->espb_signature = espb_sig;
    callback_info->user_data_param_index = user_data_param_idx;
    callback_info->original_user_data = original_user_data;

    // ИСПРАВЛЕНИЕ: ESPB сигнатура неправильная (0 параметров), но timer callback должен иметь 1 параметр
    uint32_t num_params = espb_sig->num_params;
    if (num_params == 0) {
        ESP_LOGW(TAG, "ESPB signature has 0 params, but timer callback needs 1 param - fixing");
        num_params = 1; // Принудительно устанавливаем 1 параметр для timer callback
    }
    
    if (num_params > ESPB_CALLBACK_MAX_PARAMS) {
        ESP_LOGE(TAG, "Too many callback parameters: %lu", (unsigned long)num_params);
        free(callback_info);
        return ESPB_ERR_INVALID_OPERAND;
    }

    for (uint32_t i = 0; i < num_params; ++i) {
        if (i < espb_sig->num_params) {
            callback_info->native_arg_types[i] = espb_type_to_ffi_type_internal(espb_sig->param_types[i]);
        } else {
            // Для дополнительных параметров (timer callback) используем PTR
            callback_info->native_arg_types[i] = espb_type_to_ffi_type_internal(ESPB_TYPE_PTR);
        }
        
        if (!callback_info->native_arg_types[i]) {
            ESP_LOGE(TAG, "Unsupported parameter type for param %lu", (unsigned long)i);
            free(callback_info);
            return ESPB_ERR_INVALID_OPERAND;
        }
    }

    // Определяем тип возвращаемого значения
    ffi_type *ret_type = &ffi_type_void;
    if (espb_sig->num_returns > 0) {
        ret_type = espb_type_to_ffi_type_internal(espb_sig->return_types[0]);
        if (!ret_type) {
            ESP_LOGE(TAG, "Unsupported return type: %d", espb_sig->return_types[0]);
            free(callback_info);
            return ESPB_ERR_INVALID_OPERAND;
        }
    }

    // Подготавливаем FFI CIF
    ffi_status status = ffi_prep_cif(&callback_info->native_cif, FFI_DEFAULT_ABI,
                                     num_params, ret_type, callback_info->native_arg_types);
    if (status != FFI_OK) {
        ESP_LOGE(TAG, "ffi_prep_cif failed: %d", status);
        free(callback_info);
        return ESPB_ERR_RUNTIME_ERROR;
    }

    // Создаем замыкание
    EspbCallbackClosure *closure = (EspbCallbackClosure*)calloc(1, sizeof(EspbCallbackClosure));
    if (!closure) {
        ESP_LOGE(TAG, "Failed to allocate callback closure");
        free(callback_info);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    closure->callback_info = callback_info;
    closure->instance = instance;
    // closure->exec_ctx = exec_ctx; // БОЛЬШЕ НЕ СОХРАНЯЕМ КОНТЕКСТ
    
   ESP_LOGD(TAG, "Callback closure created (will use its own exec_ctx)");

    // Выделяем память для FFI замыкания
    closure->closure_ptr = ffi_closure_alloc(sizeof(ffi_closure), &closure->executable_code);
    
    if (!closure->closure_ptr || !closure->executable_code) {
        ESP_LOGE(TAG, "ffi_closure_alloc failed - likely out of executable memory");
        
#if defined(CONFIG_LIBFFI_USE_IRAM_POOL)
        // Выводим диагностику IRAM pool при ошибке выделения
        // extern void iram_pool_debug(void);
        // ESP_LOGW(TAG, "IRAM pool status after allocation failure:");
        // iram_pool_debug();
#endif
        // Проверяем, не вернул ли аллокатор неисполняемую память (критично для C3)
        if (closure->executable_code && !esp_ptr_executable(closure->executable_code)) {
            ESP_LOGE(TAG, "FATAL: ffi_closure_alloc returned non-executable memory (%p)!", closure->executable_code);
        }
        
        free(callback_info);
        free(closure);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    // Диагностика успешного выделения с проверкой на исполняемость
    if (esp_ptr_in_iram(closure->executable_code)) {
       ESP_LOGD(TAG, "Successfully allocated closure from IRAM pool: closure=%p, exec=%p", closure->closure_ptr, closure->executable_code);
    } else if (esp_ptr_executable(closure->executable_code)) {
        ESP_LOGW(TAG, "Allocated closure from executable DRAM (fallback): closure=%p, exec=%p", closure->closure_ptr, closure->executable_code);
    } else {
        ESP_LOGE(TAG, "FATAL: Allocated closure in NON-EXECUTABLE memory: closure=%p, exec=%p", closure->closure_ptr, closure->executable_code);
        // Здесь можно добавить обработку критической ошибки, например, панику
    }

    // Подготавливаем замыкание
    status = ffi_prep_closure_loc(closure->closure_ptr, &callback_info->native_cif,
                                  espb_universal_callback_handler, closure, closure->executable_code);
    if (status != FFI_OK) {
        ESP_LOGE(TAG, "ffi_prep_closure_loc failed: %d", status);
        ffi_closure_free(closure->closure_ptr);
        free(callback_info);
        free(closure);
        return ESPB_ERR_RUNTIME_ERROR;
    }

    // Добавляем замыкание в список активных (потокобезопасно)
    if (xSemaphoreTake(g_callback_system.mutex, portMAX_DELAY) == pdTRUE) {
        closure->next = g_callback_system.active_closures;
        g_callback_system.active_closures = closure;
        xSemaphoreGive(g_callback_system.mutex);
    }

    *out_closure_ptr = closure->executable_code;
    
   ESP_LOGD(TAG, "Created callback closure: espb_func=%lu, closure=%p, exec=%p",
             (unsigned long)espb_func_idx, closure->closure_ptr, closure->executable_code);

    return ESPB_OK;
}

EspbResult espb_free_callback_closure(void *closure_ptr) {
    if (!closure_ptr || !g_callback_system.initialized) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    EspbCallbackClosure *found_closure = NULL;
    EspbCallbackClosure *prev_closure = NULL;

    // Ищем замыкание в списке активных (потокобезопасно)
    if (xSemaphoreTake(g_callback_system.mutex, portMAX_DELAY) == pdTRUE) {
        EspbCallbackClosure *current = g_callback_system.active_closures;
        
        while (current) {
            if (current->executable_code == closure_ptr) {
                found_closure = current;
                
                // Удаляем из списка
                if (prev_closure) {
                    prev_closure->next = current->next;
                } else {
                    g_callback_system.active_closures = current->next;
                }
                break;
            }
            prev_closure = current;
            current = current->next;
        }
        
        xSemaphoreGive(g_callback_system.mutex);
    }

    if (!found_closure) {
        ESP_LOGW(TAG, "Callback closure not found: %p", closure_ptr);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // Освобождаем ресурсы
    if (found_closure->closure_ptr) {
       ESP_LOGD(TAG, "Freeing closure: closure=%p, exec=%p", 
                 found_closure->closure_ptr, found_closure->executable_code);
        ffi_closure_free(found_closure->closure_ptr);
        
        // Диагностика состояния IRAM pool после освобождения
#if defined(CONFIG_LIBFFI_USE_IRAM_POOL)
        extern void iram_pool_debug(void);
        // ESP_LOGD(TAG, "IRAM pool status after freeing closure:");
        // iram_pool_debug();
#endif
    }
    
    if (found_closure->callback_info) {
        free(found_closure->callback_info);
    }
    
    free(found_closure);

   ESP_LOGD(TAG, "Freed callback closure: %p", closure_ptr);
    return ESPB_OK;
}

bool espb_find_callback_metadata(
    const EspbModule *module,
    uint16_t import_idx,
    EspbCbmetaImportEntry **out_cbmeta_entry
) {
    if (!module || !out_cbmeta_entry) {
        return false;
    }

    ESP_LOGD(TAG, "Searching callback metadata for import %u in %u cbmeta entries", 
             import_idx, module->cbmeta.num_imports_with_cb);

    // Ищем в cbmeta секции информацию об импорте
    for (uint16_t i = 0; i < module->cbmeta.num_imports_with_cb; ++i) {
        EspbCbmetaImportEntry *entry = &module->cbmeta.imports[i];
        ESP_LOGD(TAG, "Cbmeta entry %u: import_index=%u, num_callbacks=%u", 
                 i, entry->import_index, entry->num_callbacks);
        
        if (entry->import_index == import_idx) {
            ESP_LOGD(TAG, "Found callback metadata for import %u: %u callbacks", 
                     import_idx, entry->num_callbacks);
            *out_cbmeta_entry = entry;
            return true;
        }
    }

    ESP_LOGD(TAG, "No callback metadata found for import %u", import_idx);
    return false;
}

// КРИТИЧНАЯ ФУНКЦИЯ: декодирование callback метаданных
ESPB_CALLBACK_CRITICAL
void espb_decode_callback_entry(
    const uint8_t *entry_data,
    uint8_t *out_param_idx,
    uint32_t *out_espb_func_idx,
    int8_t *out_user_data_idx
) {
    if (!entry_data || !out_param_idx || !out_espb_func_idx || !out_user_data_idx) {
        return;
    }

    // Декодируем cbHeader (Байт 0)
    uint8_t cbHeader = entry_data[0];
    *out_param_idx = cbHeader & 0x0F; // младшие 4 бита
    uint8_t user_data_idx_raw = (cbHeader >> 4) & 0x0F; // старшие 4 бита

    if (user_data_idx_raw == 0x0F) {
        *out_user_data_idx = -1; // 0xF означает "не указано"
    } else {
        *out_user_data_idx = user_data_idx_raw;
    }

    // Декодируем espb_func_idx из Байт 1 и 2
    // Эта часть основана на существующем коде, так как спецификация cbmeta неполная
    *out_espb_func_idx = entry_data[1] | ((uint32_t)(entry_data[2] & 0x7F) << 8);

    ESP_LOGD(TAG, "Decoded callback: param_idx=%u, espb_func_idx=%lu, user_data_idx=%d, raw_bytes=[0x%02x, 0x%02x, 0x%02x]",
             *out_param_idx, (unsigned long)*out_espb_func_idx, *out_user_data_idx,
             entry_data[0], entry_data[1], entry_data[2]);
}

EspbResult espb_auto_create_callbacks_for_import(
    EspbInstance *instance,
    uint16_t import_idx,
    void **ffi_args,
    uint32_t num_args
) {
    if (!instance || !ffi_args) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    const EspbModule *module = instance->module;
    EspbCbmetaImportEntry *cbmeta_entry = NULL;

    // Ищем callback метаданные для данного импорта
    if (!espb_find_callback_metadata(module, import_idx, &cbmeta_entry)) {
        // Нет callback метаданных для этого импорта
        return ESPB_OK;
    }

   ESP_LOGD(TAG, "Found callback metadata for import %u: %u callbacks", 
             import_idx, cbmeta_entry->num_callbacks);

    // Обрабатываем каждый callback в метаданных
    for (uint8_t cb_idx = 0; cb_idx < cbmeta_entry->num_callbacks; ++cb_idx) {
        const uint8_t *entry_data = cbmeta_entry->entries + (cb_idx * 3);
        
        uint8_t param_idx;
        uint32_t espb_func_idx;
        int8_t user_data_idx;
        
        espb_decode_callback_entry(entry_data, &param_idx, &espb_func_idx, &user_data_idx);

        if (param_idx >= num_args) {
            ESP_LOGW(TAG, "Callback parameter index %u out of bounds (num_args=%lu) - skipping callback %u", 
                     param_idx, (unsigned long)num_args, cb_idx);
            ESP_LOGW(TAG, "Raw callback entry: [0x%02x, 0x%02x, 0x%02x]", 
                     entry_data[0], entry_data[1], entry_data[2]);
            continue;
        }
        
        // Дополнительная проверка user_data индекса
        if (user_data_idx >= 0 && (uint32_t)user_data_idx >= num_args) {
            ESP_LOGW(TAG, "User data parameter index %d out of bounds (num_args=%lu) - setting to -1", 
                     user_data_idx, (unsigned long)num_args);
            user_data_idx = -1; // Отключаем user_data если индекс неправильный
        }

       ESP_LOGD(TAG, "Creating callback %u: param_idx=%u, espb_func=%lu, user_data_idx=%d",
                 cb_idx, param_idx, (unsigned long)espb_func_idx, user_data_idx);
        
       ESP_LOGD(TAG, "This callback will be triggered when timer fires (every 2 seconds)");

        // Получаем оригинальные пользовательские данные
        void *original_user_data = NULL;
        if (user_data_idx >= 0 && (uint32_t)user_data_idx < num_args) {
            original_user_data = *(void**)ffi_args[user_data_idx];
        }

        // Создаем callback замыкание
        void *closure_ptr = NULL;
        EspbResult result = espb_create_callback_closure(
            instance, import_idx, param_idx, espb_func_idx,
            user_data_idx, original_user_data, &closure_ptr
        );

        if (result != ESPB_OK) {
            ESP_LOGE(TAG, "Failed to create callback closure: %d", result);
            continue;
        }

        // Заменяем аргумент на указатель на замыкание
        *(void**)ffi_args[param_idx] = closure_ptr;

       ESP_LOGD(TAG, "Replaced callback argument %u with closure: %p", param_idx, closure_ptr);
    }

    return ESPB_OK;
}
