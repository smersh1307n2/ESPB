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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> // Для uint32_t и т.д.
#include <stdbool.h>
#include <inttypes.h> // Для PRIx32 и т.д.
#include <limits.h> // Для INT32_MIN, INT64_MIN
#include "ffi.h" // libffi for dynamic host function calls

#include "espb_interpreter_common_types.h" // Определяет EspbInstance, Value, EspbModule и т.д.
#include "espb_interpreter_reader.h" // Для функций read_*
#include "espb_interpreter_runtime_oc.h" // Заголовок для этой функции
#include "espb_host_symbols.h"
#include "espb_interpreter.h"
#include "espb_heap_manager.h"
#include "esp_log.h"
#include "sdkconfig.h"  // Для доступа к Kconfig-опциям
#include <math.h>
#include "espb_callback_system.h" // Добавлена система callback'ов
#include "espb_runtime_oc_debug.h"

// If OC debug is disabled, compile-out ALL ESP_LOGD in this translation unit.
// This guarantees zero benchmark impact even if LOG_LOCAL_LEVEL is DEBUG.
#if !ESPB_RUNTIME_OC_DEBUG
#undef ESP_LOGD
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
#endif

// JIT должен давать 0 overhead, когда отключен в Kconfig
#if CONFIG_ESPB_JIT_ENABLED
#include "espb_jit_dispatcher.h" // Для espb_execute_function

// (JIT cold-path helpers находятся ниже, после объявления TAG)
#endif

#include "esp_system.h"


// Локальный TAG для сообщений от этого модуля
static const char *TAG = "espb_runtime_oc";

#if CONFIG_ESPB_JIT_ENABLED
// Локальный лимит аргументов для helper'ов (не хотим тянуть лишние зависимости в заголовки).
// Должен быть >= реального максимума, который поддерживает ваш ABI. 16 достаточно для большинства случаев.
#ifndef ESPB_JIT_HELPER_ARGS_MAX
#define ESPB_JIT_HELPER_ARGS_MAX 16
#endif

// --- Cold-path helpers для JIT, чтобы не раздувать hot-loop интерпретатора ---
__attribute__((noinline, cold))
static bool espb_try_call_jit_for_call(
    EspbInstance *instance,
    ExecutionContext *exec_ctx,
    uint32_t num_imported_funcs,
    uint16_t num_virtual_regs,
    uint16_t local_func_idx_to_call,
    const EspbFuncSignature *callee_sig,
    Value *locals
) {
    Value temp_args[ESPB_JIT_HELPER_ARGS_MAX];
    uint32_t num_args = MIN(callee_sig->num_params, ESPB_JIT_HELPER_ARGS_MAX);
    for (uint32_t i = 0; i < num_args; i++) {
        if (i < num_virtual_regs) {
            temp_args[i] = locals[i];
        } else {
            memset(&temp_args[i], 0, sizeof(Value));
        }
    }

    uint32_t global_func_idx = local_func_idx_to_call + num_imported_funcs;
    Value jit_result;
    memset(&jit_result, 0, sizeof(Value));

    EspbResult call_res = espb_execute_function(instance, exec_ctx, global_func_idx, temp_args, &jit_result);
    if (call_res != ESPB_OK) {
        ESP_LOGE(TAG, "JIT call failed for function #%u: %d", local_func_idx_to_call, call_res);
        return false;
    }

    if (callee_sig->num_returns > 0 && num_virtual_regs > 0) {
        locals[0] = jit_result;
    }
    return true;
}

__attribute__((noinline, cold))
static bool espb_try_call_jit_for_call_indirect(
    EspbInstance *instance,
    ExecutionContext *exec_ctx,
    uint32_t num_imported_funcs,
    uint16_t num_virtual_regs,
    uint32_t local_func_idx_to_call,
    const EspbFuncSignature *callee_sig,
    Value *locals
) {
    Value temp_args[ESPB_JIT_HELPER_ARGS_MAX];
    uint32_t num_args = MIN(callee_sig->num_params, ESPB_JIT_HELPER_ARGS_MAX);
    for (uint32_t i = 0; i < num_args; i++) {
        if (i < num_virtual_regs) {
            temp_args[i] = locals[i];
        } else {
            memset(&temp_args[i], 0, sizeof(Value));
        }
    }

    uint32_t global_func_idx = local_func_idx_to_call + num_imported_funcs;
    Value jit_result;
    memset(&jit_result, 0, sizeof(Value));

    EspbResult call_res = espb_execute_function(instance, exec_ctx, global_func_idx, temp_args, &jit_result);
    if (call_res != ESPB_OK) {
        ESP_LOGE(TAG, "JIT CALL_INDIRECT failed for function #%u: %d", local_func_idx_to_call, call_res);
        return false;
    }

    if (callee_sig->num_returns > 0 && num_virtual_regs > 0) {
        locals[0] = jit_result;
    }
    return true;
}
#endif

// ============================================================================
// DEBUG CHECKS: Runtime validation macros
// ============================================================================
// Эти проверки убивают производительность и раздувают код.
// В релизной сборке они отключены, валидация происходит один раз при загрузке модуля.
// ============================================================================

#ifdef CONFIG_ESPB_DEBUG_CHECKS
    #define DEBUG_CHECK_REG(reg, max_reg, msg) \
        do { \
            if ((reg) > (max_reg)) { \
                ESP_LOGE(TAG, "%s - Register R%u out of bounds (max: %u)", msg, (reg), (max_reg)); \
                return ESPB_ERR_INVALID_REGISTER_INDEX; \
            } \
        } while(0)
        
    #define DEBUG_CHECK_REGS_2(r1, r2, max_reg, msg) \
        do { \
            if ((r1) > (max_reg) || (r2) > (max_reg)) { \
                ESP_LOGE(TAG, "%s - Register out of bounds. R%u, R%u (max: %u)", msg, (r1), (r2), (max_reg)); \
                return ESPB_ERR_INVALID_REGISTER_INDEX; \
            } \
        } while(0)
        
    #define DEBUG_CHECK_REGS_3(r1, r2, r3, max_reg, msg) \
        do { \
            if ((r1) > (max_reg) || (r2) > (max_reg) || (r3) > (max_reg)) { \
                ESP_LOGE(TAG, "%s - Register out of bounds. R%u, R%u, R%u (max: %u)", msg, (r1), (r2), (r3), (max_reg)); \
                return ESPB_ERR_INVALID_REGISTER_INDEX; \
            } \
        } while(0)
#else
    #define DEBUG_CHECK_REG(reg, max_reg, msg) ((void)0)
    #define DEBUG_CHECK_REGS_2(r1, r2, max_reg, msg) ((void)0)
    #define DEBUG_CHECK_REGS_3(r1, r2, r3, max_reg, msg) ((void)0)
#endif

const uint8_t value_size_map[] = {
    [ESPB_TYPE_UNKNOWN] = 0,
    [ESPB_TYPE_I8]      = sizeof(int8_t),
    [ESPB_TYPE_U8]      = sizeof(uint8_t),
    [ESPB_TYPE_I16]     = sizeof(int16_t),
    [ESPB_TYPE_U16]     = sizeof(uint16_t),
    [ESPB_TYPE_I32]     = sizeof(int32_t),
    [ESPB_TYPE_U32]     = sizeof(uint32_t),
    [ESPB_TYPE_I64]     = sizeof(int64_t),
    [ESPB_TYPE_U64]     = sizeof(uint64_t),
    [ESPB_TYPE_F32]     = sizeof(float),
    [ESPB_TYPE_F64]     = sizeof(double),
    [ESPB_TYPE_PTR]     = sizeof(void*),
    [ESPB_TYPE_BOOL]    = sizeof(uint8_t), // Stored as byte
    [ESPB_TYPE_V128]    = 16,
    [ESPB_TYPE_INTERNAL_FUNC_IDX] = 0,
    [ESPB_TYPE_VOID]    = 0,
};

// Helper for comparing function signatures
static bool signatures_are_compatible(const EspbFuncSignature* sig1, const EspbFuncSignature* sig2) {
    if (!sig1 || !sig2) return false;
    if (sig1->num_params != sig2->num_params || sig1->num_returns != sig2->num_returns) {
        return false;
    }
    if (sig1->num_params > 0 && memcmp(sig1->param_types, sig2->param_types, sig1->num_params * sizeof(EspbValueType)) != 0) {
        return false;
    }
    if (sig1->num_returns > 0 && memcmp(sig1->return_types, sig2->return_types, sig1->num_returns * sizeof(EspbValueType)) != 0) {
        return false;
    }
    return true;
}


// Comparator for bsearch on EspbFuncPtrMapEntry
static int compare_func_ptr_map_entry_for_search(const void *key, const void *element) {
    const uint32_t *data_offset_key = (const uint32_t *)key;
    const EspbFuncPtrMapEntry *entry = (const EspbFuncPtrMapEntry *)element;
    if (*data_offset_key < entry->data_offset) return -1;
    if (*data_offset_key > entry->data_offset) return 1;
    return 0;
}

// Helper for debugging memory
static void print_memory(const char *title, const uint8_t *mem, size_t len) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "--- %s (len=%zu, addr=%p) ---", title, len, mem);
    char line_buf[128];
    int line_pos = 0;
    if (len > 128) len = 128; // Print max 128 bytes
    for (size_t i = 0; i < len; ++i) {
        line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos, "%02x ", mem[i]);
        if ((i + 1) % 16 == 0 || i == len - 1) {
            ESP_LOGD(TAG, "%s", line_buf);
            line_pos = 0;
            memset(line_buf, 0, sizeof(line_buf));
        }
    }
    ESP_LOGD(TAG, "-------------------------");
#endif
}

// === ASYNC WRAPPER SYSTEM ===

// Структура для планирования аргументов при маршалинге
typedef struct {
    uint8_t has_meta;         // 1 if immeta entry exists for this arg
    uint8_t direction;        // IN/OUT/INOUT flags
    uint8_t handler_idx;      // 0=standard, 1=async
    uint32_t buffer_size;     // computed size if needed
    void *temp_buffer;        // allocated buffer for std marshalling
    void *original_ptr;       // original destination for copy-back
} ArgPlan;

// Universal async wrapper handler function
static void universal_async_wrapper_handler(ffi_cif *cif, void *ret_value, 
                                           void **ffi_args, void *user_data) {
    AsyncWrapperContext *ctx = (AsyncWrapperContext*)user_data;
    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD("espb_debug", "ASYNC WRAPPER HANDLER CALLED - THIS MEANS IT WORKS!");
    ESP_LOGD("espb_async", "=== ASYNC WRAPPER CALLED ===");
    ESP_LOGD("espb_async", "Original function: %p", ctx->original_func_ptr);
    ESP_LOGD("espb_async", "OUT parameters to handle: %u", ctx->num_out_params);
#endif
    
    // 1. Вызываем оригинальную функцию
    ffi_call(&ctx->original_cif, FFI_FN(ctx->original_func_ptr), ret_value, ffi_args);
    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD("espb_async", "Original function call completed");
#endif
    
    // 2. Атомарно копируем все OUT параметры обратно в память ESPB
    for (uint8_t i = 0; i < ctx->num_out_params; ++i) {
        uint8_t arg_idx = ctx->out_params[i].arg_index;
        void *espb_ptr = ctx->out_params[i].espb_memory_ptr;
        uint32_t size = ctx->out_params[i].buffer_size;
        void *native_ptr = ffi_args[arg_idx];
        
        if (espb_ptr && native_ptr && size > 0) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD("espb_async", "Copying OUT param #%u: %u bytes from %p to %p",
                     arg_idx, size, native_ptr, espb_ptr);
#endif
            
            // Для указателей копируем значение указателя
            if (size == sizeof(void*)) {
                memcpy(espb_ptr, native_ptr, size);
            } else {
                // Для буферов копируем содержимое
                memcpy(espb_ptr, *(void**)native_ptr, size);
            }
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD("espb_async", "OUT param #%u copied successfully", arg_idx);
#endif
        } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD("espb_async", "OUT param #%u skipped: invalid pointers or size", arg_idx);
#endif
        }
    }
    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD("espb_async", "=== ASYNC WRAPPER COMPLETED ===");
#endif
}

// Создание async wrapper для импорта
static AsyncWrapper* create_async_wrapper_for_import(EspbInstance *instance,
                                                     uint16_t import_idx,
                                                     const EspbImmetaImportEntry *immeta_entry,
                                                     const ArgPlan *arg_plans,
                                                     uint8_t num_args,
                                                     ffi_cif* original_cif) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD("espb_async", "Creating async wrapper for import #%u", import_idx);
#endif
    
    AsyncWrapper *wrapper = (AsyncWrapper*)malloc(sizeof(AsyncWrapper));
    if (!wrapper) {
        ESP_LOGE("espb_async", "Failed to allocate async wrapper");
        return NULL;
    }
    
    memset(wrapper, 0, sizeof(AsyncWrapper));
    
    // Инициализируем контекст
    wrapper->context.original_func_ptr = instance->resolved_import_funcs[import_idx];
    wrapper->context.num_out_params = 0;
    
    // Копируем CIF оригинальной функции
    memcpy(&wrapper->context.original_cif, original_cif, sizeof(ffi_cif));
    
    // Первый проход: подсчитываем количество OUT параметров
    uint8_t out_param_count = 0;
    for (uint8_t i = 0; i < num_args; ++i) {
        if (arg_plans[i].has_meta && 
            (arg_plans[i].direction & ESPB_IMMETA_DIRECTION_OUT) &&
            arg_plans[i].handler_idx == 1) {
            out_param_count++;
        }
    }
    
    if (out_param_count == 0) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGD("espb_async", "No OUT parameters found, this shouldn't happen");
#endif
        free(wrapper);
        return NULL;
    }
    
    // Выделяем память для OUT параметров
    wrapper->context.out_params = (AsyncOutParam*)malloc(out_param_count * sizeof(AsyncOutParam));
    if (!wrapper->context.out_params) {
        ESP_LOGE("espb_async", "Failed to allocate memory for OUT parameters");
        free(wrapper);
        return NULL;
    }
    
    // Второй проход: заполняем информацию об OUT параметрах
    wrapper->context.num_out_params = 0;
    for (uint8_t i = 0; i < num_args; ++i) {
        if (arg_plans[i].has_meta && 
            (arg_plans[i].direction & ESPB_IMMETA_DIRECTION_OUT) &&
            arg_plans[i].handler_idx == 1) {
            
            uint8_t out_idx = wrapper->context.num_out_params++;
            wrapper->context.out_params[out_idx].arg_index = i;
            wrapper->context.out_params[out_idx].espb_memory_ptr = arg_plans[i].original_ptr;
            wrapper->context.out_params[out_idx].buffer_size = arg_plans[i].buffer_size;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD("espb_async", "Registered OUT param #%u: arg_idx=%u, size=%u",
                     out_idx, i, arg_plans[i].buffer_size);
#endif
        }
    }
    
    // Создаем FFI closure
    wrapper->closure_ptr = ffi_closure_alloc(sizeof(ffi_closure), &wrapper->executable_code);
    if (!wrapper->closure_ptr) {
        ESP_LOGE("espb_async", "Failed to allocate FFI closure");
        free(wrapper->context.out_params);
        free(wrapper);
        return NULL;
    }
    
    // Подготавливаем closure
    if (ffi_prep_closure_loc(wrapper->closure_ptr, &wrapper->context.original_cif,
                            universal_async_wrapper_handler, &wrapper->context,
                            wrapper->executable_code) != FFI_OK) {
        ESP_LOGE("espb_async", "Failed to prepare FFI closure");
        free(wrapper->closure_ptr);
        free(wrapper->context.out_params);
        free(wrapper);
        return NULL;
    }
    
    wrapper->is_initialized = true;
    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD("espb_async", "Async wrapper created successfully: exec_code=%p",
             wrapper->executable_code);
#endif
    
    return wrapper;
}

// --- Функции для поддержки маршалинга (immeta), перенесены из espb_marshalling.c ---

static bool espb_find_marshalling_metadata(
    const EspbModule *module,
    uint16_t import_idx,
    EspbImmetaImportEntry **out_immeta_entry
) {
    if (!module || !out_immeta_entry) {
        return false;
    }
    *out_immeta_entry = NULL;
    if (module->immeta.num_imports_with_meta == 0 || !module->immeta.imports) {
        return false;
    }
    for (uint16_t i = 0; i < module->immeta.num_imports_with_meta; ++i) {
        if (module->immeta.imports[i].import_index == import_idx) {
            *out_immeta_entry = &module->immeta.imports[i];
            return true;
        }
    }
    return false;
}

static bool espb_get_arg_marshalling_info(
    const EspbImmetaImportEntry *entry,
    uint8_t arg_index,
    const EspbImmetaArgEntry **out_arg_entry
) {
    if (!entry || !out_arg_entry) {
        return false;
    }
    *out_arg_entry = NULL;
    for (uint8_t i = 0; i < entry->num_marshalled_args; ++i) {
        if (entry->args[i].arg_index == arg_index) {
            *out_arg_entry = &entry->args[i];
            return true;
        }
    }
    return false;
}

static uint32_t espb_calculate_buffer_size(
    const EspbImmetaArgEntry *arg_entry,
    const Value *args,
    uint32_t num_args
) {
    if (!arg_entry) {
        return 0;
    }
    switch (arg_entry->size_kind) {
        case ESPB_IMMETA_SIZE_KIND_CONST:
            return (uint32_t)arg_entry->size_value;
        case ESPB_IMMETA_SIZE_KIND_FROM_ARG:
            if (arg_entry->size_value < num_args && args) {
                return (uint32_t)V_I32(args[arg_entry->size_value]);
            }
            return 32; // Fallback
        case 2: // ESPB_IMMETA_SIZE_KIND_NULL_TERMINATED - strlen(src_arg) + 1
            // size_value contains the index of the source string argument
            if (arg_entry->size_value < num_args && args) {
                const char *src_str = (const char *)V_PTR(args[arg_entry->size_value]);
                if (src_str) {
                    return (uint32_t)(strlen(src_str) + 1);
                }
            }
            return 32; // Fallback if source is invalid
        default:
            return 32; // Fallback
    }
}

static bool espb_arg_needs_copyback(const EspbImmetaArgEntry *arg_entry) {
    if (!arg_entry) {
        return false;
    }
    return (arg_entry->direction_flags == ESPB_IMMETA_DIRECTION_OUT ||
            arg_entry->direction_flags == ESPB_IMMETA_DIRECTION_INOUT);
}

static bool espb_arg_needs_copyin(const EspbImmetaArgEntry *arg_entry) {
    if (!arg_entry) {
        return false;
    }
    return (arg_entry->direction_flags == ESPB_IMMETA_DIRECTION_IN ||
            arg_entry->direction_flags == ESPB_IMMETA_DIRECTION_INOUT);
}
// --- Конец функций для поддержки маршалинга ---

// Предварительные объявления
// РЕФАКТОРИНГ: Упрощенные функции для работы с единым виртуальным стеком
static EspbResult push_call_frame(ExecutionContext *ctx, int return_pc, size_t saved_fp, uint32_t caller_local_func_idx, Value* frame_to_save, size_t num_regs_to_save);
static EspbResult pop_call_frame(ExecutionContext *ctx, int* return_pc, size_t* saved_fp, uint32_t* caller_local_func_idx, Value** saved_frame_ptr, size_t* num_regs_saved_ptr);

#define FFI_ARGS_MAX 16 // Максимальное количество аргументов для FFI вызовов (включая замыкания)
// Глобальный буфер для переопределённых целочисленных аргументов (например, xCoreID)
static int32_t override_int_args[FFI_ARGS_MAX] __attribute__((unused)) = {0};

// Вспомогательная функция для преобразования ESPB типа в FFI тип
static ffi_type* espb_type_to_ffi_type(EspbValueType es_type) {
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
        // ESPB_TYPE_BOOL обычно обрабатывается как I32
        case ESPB_TYPE_BOOL: return &ffi_type_sint32; 
        // ESPB_TYPE_INTERNAL_FUNC_IDX не должен напрямую мапиться в FFI для вызова,
        // он преобразуется в I32, а затем используется для поиска ESPB функции.
        // ESPB_TYPE_V128 пока не поддерживается.
        default: return NULL; // Неизвестный или неподдерживаемый тип
    }
}



// Используем реализацию espb_lookup_host_symbol из espb_host_symbols.c
#pragma GCC diagnostic ignored "-Wunused-variable"

// --- Константы для ядра интерпретатора ---
#define CALL_STACK_SIZE 64   // Максимальная глубина стека вызовов (CallFrame)

// Используем значения из Kconfig или значения по умолчанию, если Kconfig не определен
#ifdef CONFIG_ESPB_SHADOW_STACK_INITIAL_SIZE
#define INITIAL_SHADOW_STACK_CAPACITY CONFIG_ESPB_SHADOW_STACK_INITIAL_SIZE
#else
#define INITIAL_SHADOW_STACK_CAPACITY (4 * 1024)  // 4 КБ по умолчанию
#endif

#ifdef CONFIG_ESPB_SHADOW_STACK_INCREMENT
#define SHADOW_STACK_INCREMENT CONFIG_ESPB_SHADOW_STACK_INCREMENT
#else
#define SHADOW_STACK_INCREMENT (4 * 1024)  // 4 КБ по умолчанию
#endif

// --- Функции для управления ExecutionContext ---

ExecutionContext* init_execution_context(void) {
    ExecutionContext *ctx = (ExecutionContext*)calloc(1, sizeof(ExecutionContext));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate memory for ExecutionContext");
        return NULL;
    }

    ctx->call_stack = (RuntimeFrame*)malloc(CALL_STACK_SIZE * sizeof(RuntimeFrame));
    if (!ctx->call_stack) {
        ESP_LOGE(TAG, "Failed to allocate memory for call stack");
        free(ctx);
        return NULL;
    }
    memset(ctx->call_stack, 0, CALL_STACK_SIZE * sizeof(RuntimeFrame));

    ctx->shadow_stack_buffer = (uint8_t*)malloc(INITIAL_SHADOW_STACK_CAPACITY);
    if (!ctx->shadow_stack_buffer) {
        ESP_LOGE(TAG, "Failed to allocate initial shadow stack of %d bytes", INITIAL_SHADOW_STACK_CAPACITY);
        free(ctx->call_stack);
        free(ctx);
        return NULL;
    }
    ctx->shadow_stack_capacity = INITIAL_SHADOW_STACK_CAPACITY;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Initialized shadow stack with capacity: %d bytes", INITIAL_SHADOW_STACK_CAPACITY);
#endif

    ctx->call_stack_top = 0;
    ctx->sp = 0;
    ctx->fp = 0; // Инициализируем указатель кадра
    
    // ОПТИМИЗАЦИЯ: Инициализируем флаги системы колбэков
    ctx->callback_system_initialized = false;
    ctx->feature_callback_auto_active = false;
    
    return ctx;
}

void free_execution_context(ExecutionContext *ctx) {
    if (ctx) {
        if (ctx->call_stack) {
            free(ctx->call_stack);
        }
        if (ctx->shadow_stack_buffer) {
            free(ctx->shadow_stack_buffer);
        }
        // ИСПРАВЛЕНО: Убрано освобождение ctx->registers (устраняет double free)
        // if (ctx->registers) {
        //     free(ctx->registers);
        // }
        free(ctx);
    }
}

// ОПТИМИЗАЦИЯ: Инициализация системы колбэков для ExecutionContext
static void init_callback_system_for_context(ExecutionContext *ctx, const EspbModule *module) {
    if (!ctx->callback_system_initialized && module) {
        ctx->feature_callback_auto_active = (module->header.features & FEATURE_CALLBACK_AUTO) != 0;
        ctx->callback_system_initialized = true;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        #if ESPB_RUNTIME_OC_DEBUG
        ESP_LOGD(TAG, "ESPB DEBUG: Callback system initialized. FEATURE_CALLBACK_AUTO: %s", 
                 ctx->feature_callback_auto_active ? "yes" : "no");
#endif
#endif
    }
}

// Коды ошибок времени выполнения (могут дублироваться с common_types.h, проверить)
#define ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO      (ESPB_ERR_RUNTIME_ERROR - 1)
#define ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW (ESPB_ERR_RUNTIME_ERROR - 2)
#define ESPB_ERR_RUNTIME_TRAP_BAD_BRANCH_TARGET (ESPB_ERR_RUNTIME_ERROR - 3)
#define ESPB_ERR_RUNTIME_TRAP                 (ESPB_ERR_RUNTIME_ERROR - 5) // Generic trap



// Функции для работы со стеком вызовов (новая, упрощенная реализация)
static EspbResult push_call_frame(ExecutionContext *ctx, int return_pc, size_t saved_fp, uint32_t caller_local_func_idx, Value* frame_to_save, size_t num_regs_to_save) {
    if (ctx->call_stack_top >= CALL_STACK_SIZE) {
        ESP_LOGE(TAG, "Call stack overflow");
        return ESPB_ERR_STACK_OVERFLOW;
    }
    RuntimeFrame* frame = &ctx->call_stack[ctx->call_stack_top++];
    frame->ReturnPC = return_pc;
    frame->SavedFP = saved_fp;
    frame->caller_local_func_idx = caller_local_func_idx;
    frame->saved_frame = frame_to_save;
    frame->saved_num_virtual_regs = num_regs_to_save;
    
    // Инициализация ALLOCA трекера для нового кадра
    frame->alloca_count = 0;
    frame->has_custom_aligned = false;
    memset(frame->alloca_ptrs, 0, sizeof(frame->alloca_ptrs));
    
    return ESPB_OK;
}

static EspbResult pop_call_frame(ExecutionContext *ctx, int* return_pc, size_t* saved_fp, uint32_t* caller_local_func_idx, Value** saved_frame_ptr, size_t* num_regs_saved_ptr) {
    if (ctx->call_stack_top <= 0) {
        ESP_LOGE(TAG, "Call stack underflow");
        return ESPB_ERR_STACK_UNDERFLOW;
    }
    RuntimeFrame* frame = &ctx->call_stack[--ctx->call_stack_top];
    *return_pc = frame->ReturnPC;
    *saved_fp = frame->SavedFP;
    *caller_local_func_idx = frame->caller_local_func_idx;
    *saved_frame_ptr = frame->saved_frame;
    *num_regs_saved_ptr = frame->saved_num_virtual_regs;
    return ESPB_OK;
}

// --- ОПТИМИЗИРОВАННАЯ ФУНКЦИЯ УПРАВЛЕНИЯ ТЕНЕВЫМ СТЕКОМ (V3) ---
// "Медленный" путь: вызывается только когда места действительно не хватает.
// Помечена как noinline и cold, чтобы компилятор не встраивал ее в горячий код.
// Возвращает: 1 если буфер был перемещен, 0 если не перемещен, -1 в случае ошибки.
__attribute__((noinline, cold))
static int _espb_grow_shadow_stack(ExecutionContext *ctx, size_t required_size) {
    size_t new_capacity = ctx->shadow_stack_capacity;
    while (ctx->sp + required_size > new_capacity) {
        new_capacity += SHADOW_STACK_INCREMENT;
    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Shadow stack overflow detected. Current capacity: %zu, required: %zu. Attempting to resize to %zu",
             ctx->shadow_stack_capacity, ctx->sp + required_size, new_capacity);
#endif

    uint8_t* old_buffer = ctx->shadow_stack_buffer;
    uint8_t* new_buffer = (uint8_t*)realloc(old_buffer, new_capacity);

    if (!new_buffer) {
        ESP_LOGE(TAG, "Failed to reallocate shadow stack to %zu bytes", new_capacity);
        return -1; // Ошибка выделения памяти
    }

    ctx->shadow_stack_buffer = new_buffer;
    ctx->shadow_stack_capacity = new_capacity;

    if (new_buffer != old_buffer) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGD(TAG, "Shadow stack buffer reallocated. Old: %p, New: %p. Relocating pointers...", (void*)old_buffer, (void*)new_buffer);
#endif
        // Исправляем указатели на сохраненные кадры в стеке вызовов
        // Рассчитываем разницу в адресах и применяем её ко всем указателям в стеке вызовов,
        // чтобы избежать использования `old_buffer` после `realloc`.
        uintptr_t diff = (uintptr_t)new_buffer - (uintptr_t)old_buffer;
        for (int i = 0; i < ctx->call_stack_top; ++i) {
            if (ctx->call_stack[i].saved_frame) {
                ctx->call_stack[i].saved_frame = (Value*)((uintptr_t)ctx->call_stack[i].saved_frame + diff);
            }
        }
        return 1; // Успешно, буфер был перемещен
    }

    return 0; // Успешно, буфер не был перемещен
}

// --- Начало тела функции espb_call_function ---
// ... existing code ... // Это комментарий, представляющий тело функции, которое уже есть в файле

// Универсальный диспетчер: вызывная точка для всех обратных вызовов из ESPB-модуля
__attribute__((noinline, optimize("O0")))
static void espb_callback_dispatch(void *pvParameter) {
    CallbackCtx *ctx = (CallbackCtx*)pvParameter;
    Value arg;
    SET_TYPE(arg, ESPB_TYPE_PTR);
    V_PTR(arg) = ctx->user_arg;
    ExecutionContext *callback_exec_ctx = init_execution_context();
    if (!callback_exec_ctx) {
        ESP_LOGE(TAG, "Failed to create execution context for callback dispatch");
        return;
    }
#if CONFIG_ESPB_JIT_ENABLED
    espb_execute_function(ctx->instance, callback_exec_ctx, ctx->func_idx, &arg, NULL);
#else
    // JIT выключен => вызываем напрямую интерпретаторный путь, без espb_execute_function
    espb_call_function(ctx->instance, callback_exec_ctx, ctx->func_idx, &arg, NULL);
#endif
    free_execution_context(callback_exec_ctx);
}
//__attribute__((noinline, optimize("O0")))
//IRAM_ATTR 
EspbResult espb_call_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results) {
    // Проверка входных параметров
    
    if (!instance) {
        return ESPB_ERR_INVALID_OPERAND;
    }
    if (!exec_ctx) {
        ESP_LOGE(TAG, "ExecutionContext is NULL");
        return ESPB_ERR_INVALID_STATE;
    }
    const EspbModule *module = instance->module;
    if (!module) {
        ESP_LOGE(TAG, "Module is NULL");
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    // ОПТИМИЗАЦИЯ: Инициализация системы колбэков вынесена в отдельную функцию
    // Вызывается только один раз для каждого ExecutionContext
    init_callback_system_for_context(exec_ctx, module);

    // ОПТИМИЗАЦИЯ: Используем кэшированное значение вместо цикла подсчёта
    uint32_t num_imported_funcs = module->num_imported_funcs;
    
    // ОПТИМИЗАЦИЯ: Ранняя проверка func_idx перед вычислением local_func_idx
    if (func_idx >= (num_imported_funcs + module->num_functions)) {
        ESP_LOGE(TAG, "espb_call_function invalid func_idx=%" PRIu32, func_idx);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // Поддержка вызова любой локальной функции: func_idx после импортов
    if (func_idx >= num_imported_funcs && func_idx < num_imported_funcs + module->num_functions) {
        // If this is the initial entry point, push a base frame so ALLOCA can work.
        if (exec_ctx->call_stack_top == 0) {
            uint32_t entry_local_idx = func_idx - num_imported_funcs;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Initial call, pushing base frame for local_func_idx %u", (unsigned)entry_local_idx);
#endif
            if (push_call_frame(exec_ctx, -1, 0, entry_local_idx, NULL, 0) != ESPB_OK) {
                return ESPB_ERR_STACK_OVERFLOW;
            }
        }
        // ОПТИМИЗАЦИЯ: Импорты уже разрешены в espb_instantiate() -> resolve_imports()
        // Повторное разрешение не требуется и вызывает лишние логи "Looking up symbol"
        // for (uint32_t imp = 0; imp < module->num_imports; ++imp) {
        //     if (module->imports[imp].kind == ESPB_IMPORT_KIND_FUNC) {
        //         instance->resolved_import_funcs[imp] = (void*)espb_lookup_host_symbol(
        //             module->imports[imp].module_name,
        //             module->imports[imp].entity_name);
        //     }
        // }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        #if ESPB_RUNTIME_OC_DEBUG
        ESP_LOGD(TAG, "ESPB DEBUG: Using pre-resolved imports from instantiation");
#endif
#endif
        
        uint32_t local_func_idx = func_idx - num_imported_funcs;
        if (local_func_idx >= module->num_functions) {
            ESP_LOGE(TAG, "Function index %" PRIu32 " out of bounds", func_idx);
            return ESPB_ERR_INVALID_FUNC_INDEX;
        }

        // Используем EspbFunctionBody из common_types.h, который должен быть корректно заполнен парсером
        const EspbFunctionBody *func_body_ptr = &module->function_bodies[local_func_idx];
        uint16_t num_virtual_regs = func_body_ptr->header.num_virtual_regs;
        uint8_t max_reg_used = func_body_ptr->header.max_reg_used; // Максимальный индекс регистра (для проверок)
        const uint8_t *instructions_ptr = func_body_ptr->code;
        size_t instructions_size = func_body_ptr->code_size;
        const uint8_t *instructions_end_ptr = instructions_ptr + instructions_size;
        
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        #if ESPB_RUNTIME_OC_DEBUG
        ESP_LOGD(TAG, "ESPB DEBUG: Дамп байт-кода функции (size=%zu):", instructions_size);
#endif
        for (size_t i = 0; i < instructions_size; i++) {
            ESP_LOGD(TAG, "ESPB DEBUG: %02X ", instructions_ptr[i]);
            if ((i + 1) % 16 == 0 || i == instructions_size - 1) {
                ESP_LOGD(TAG, "");
            }
        }
#endif
        
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        if (num_virtual_regs == 0 && instructions_size > 0) {
             ESP_LOGW(TAG, "num_virtual_regs is 0, but function has code. func_idx=%" PRIu32, local_func_idx);
        }
#endif
        
        // РЕФАКТОРИНГ: КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ - используем новую систему стека вместо calloc
        // СТАРЫЙ КОД (ВЫЗЫВАЛ DOUBLE FREE):
        // Value *locals = (Value *)calloc(num_virtual_regs > 0 ? num_virtual_regs : 1, sizeof(Value));
        
        // НОВЫЙ КОД: Используем единый виртуальный стек
        size_t frame_size_bytes = num_virtual_regs * sizeof(Value);
        // "Быстрый путь" - проверка стека встроена inline
        if (__builtin_expect(exec_ctx->sp + frame_size_bytes > exec_ctx->shadow_stack_capacity, 0)) {
            if (_espb_grow_shadow_stack(exec_ctx, frame_size_bytes) < 0) {
                return ESPB_ERR_OUT_OF_MEMORY;
            }
        }
        
        // `locals` теперь просто указатель на текущую позицию в `shadow_stack_buffer`
        Value *locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->sp);
        
        // Обнуляем выделенный кадр
        memset(locals, 0, frame_size_bytes);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        ESP_LOGD(TAG, "Allocated function frame: %u regs at %p", num_virtual_regs, locals);
#endif
#endif
        // Массив для хранения контекстов колбэков, индексирован по виртуальным регистрам
        // CallbackCtx **reg_contexts = (CallbackCtx **)calloc(num_virtual_regs > 0 ? num_virtual_regs : 1, sizeof(CallbackCtx*));
        // if (reg_contexts == NULL) {
        //     // REFACTOR_REMOVED: // REMOVED_free_locals;
        //     return ESPB_ERR_MEMORY_ALLOC;
        // }
        // memset не нужен, так как calloc инициализирует нулями
        
        if (args) {
            // Копируем аргументы в регистры R0..RN
            EspbFuncSignature* main_sig = &module->signatures[module->function_signature_indices[local_func_idx]];
            uint8_t num_params_to_copy = MIN(main_sig->num_params, num_virtual_regs);
            
            for(uint8_t i=0; i < num_params_to_copy; ++i) {
                locals[i] = args[i];
            }
        }
        
        // Инициализация R7 (в релизе без логов/ветвлений)
        // Если функция реально использует R7, то валидатор при загрузке гарантирует num_virtual_regs >= 8.
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        if (num_virtual_regs > 7) {
            SET_TYPE(locals[7], ESPB_TYPE_PTR);
            V_PTR(locals[7]) = NULL;
        }
#endif
        
        const uint8_t *pc = instructions_ptr;
        bool end_reached = false;
        int return_register = 0; 
        
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        ESP_LOGD(TAG, "ESPB DEBUG: Opcode analysis (num_virtual_regs: %u)", num_virtual_regs);
#endif
#endif
        
      //  uint32_t instruction_count = 0;
      //  TickType_t last_yield_time = xTaskGetTickCount();
        
#if defined(__GNUC__) || defined(__clang__)
    // --- Инфраструктура для Computed Goto ---
    static void* dispatch_table[256];
    static bool table_initialized = false;
    if (!table_initialized) {
        for (int i = 0; i < 256; i++) {
            dispatch_table[i] = &&op_unhandled;
        }
        dispatch_table[0x00] = &&op_0x00;
        dispatch_table[0x01] = &&op_0x01;
        dispatch_table[0x02] = &&op_0x02;
        dispatch_table[0x03] = &&op_0x03;
        dispatch_table[0x04] = &&op_0x04;
        dispatch_table[0x05] = &&op_0x05;
        // JIT_REGION_START (0x06) отключён: региональный JIT не реализуем и не планируем.
        dispatch_table[0x06] = &&op_unhandled;
        dispatch_table[0x07] = &&op_unhandled; // RESERVED as per spec v1.7 (LOOP removed)
        dispatch_table[0x0A] = &&op_0x0A;
        dispatch_table[0x0B] = &&op_0x0B;
        dispatch_table[0x0D] = &&op_0x0D;
        dispatch_table[0x11] = &&op_0x11;
        dispatch_table[0x12] = &&op_0x12;
        dispatch_table[0x13] = &&op_0x13;
        dispatch_table[0x14] = &&op_unhandled; // RESERVED
        dispatch_table[0x15] = &&op_unhandled; // RESERVED
        dispatch_table[0x16] = &&op_unhandled; // RESERVED
        dispatch_table[0x18] = &&op_0x18;
        dispatch_table[0x19] = &&op_0x19;
        dispatch_table[0x1A] = &&op_0x1A;
        dispatch_table[0x1B] = &&op_0x1B;
        dispatch_table[0x1C] = &&op_0x1C;
        dispatch_table[0x20] = &&op_0x20;
        dispatch_table[0x21] = &&op_0x21;
        dispatch_table[0x22] = &&op_0x22;
        dispatch_table[0x23] = &&op_0x23;
        dispatch_table[0x24] = &&op_0x24;
        dispatch_table[0x26] = &&op_0x26;
        dispatch_table[0x27] = &&op_0x27;
        dispatch_table[0x28] = &&op_0x28;
        dispatch_table[0x29] = &&op_0x29;
        dispatch_table[0x2A] = &&op_0x2A;
        dispatch_table[0x2B] = &&op_0x2B;
        dispatch_table[0x2C] = &&op_0x2C;
        dispatch_table[0x2D] = &&op_0x2D;
        dispatch_table[0x2E] = &&op_0x2E;
        dispatch_table[0x30] = &&op_0x30;
        dispatch_table[0x31] = &&op_0x31;
        dispatch_table[0x32] = &&op_0x32;
        dispatch_table[0x33] = &&op_0x33;
        dispatch_table[0x34] = &&op_0x34;
        dispatch_table[0x36] = &&op_0x36;
        dispatch_table[0x37] = &&op_0x37;
        dispatch_table[0x38] = &&op_0x38;
        dispatch_table[0x39] = &&op_0x39;
        dispatch_table[0x3A] = &&op_0x3A;
        dispatch_table[0x3B] = &&op_0x3B;
        dispatch_table[0x3C] = &&op_0x3C;
        dispatch_table[0x3D] = &&op_0x3D;
        dispatch_table[0x3E] = &&op_0x3E;
        dispatch_table[0x40] = &&op_0x40;
        dispatch_table[0x41] = &&op_0x41;
        dispatch_table[0x42] = &&op_0x42;
        dispatch_table[0x43] = &&op_0x43;
        dispatch_table[0x44] = &&op_0x44;
        dispatch_table[0x45] = &&op_0x45;
        dispatch_table[0x46] = &&op_0x46;
        dispatch_table[0x47] = &&op_0x47;
        dispatch_table[0x48] = &&op_0x48;
        dispatch_table[0x49] = &&op_0x49;
        dispatch_table[0x4A] = &&op_0x4A;
        dispatch_table[0x4B] = &&op_0x4B;
        dispatch_table[0x50] = &&op_0x50;
        dispatch_table[0x51] = &&op_0x51;
        dispatch_table[0x52] = &&op_0x52;
        dispatch_table[0x53] = &&op_0x53;
        dispatch_table[0x54] = &&op_0x54;
        dispatch_table[0x55] = &&op_0x55;
        dispatch_table[0x60] = &&op_0x60;
        dispatch_table[0x61] = &&op_0x61;
        dispatch_table[0x62] = &&op_0x62;
        dispatch_table[0x63] = &&op_0x63;
        dispatch_table[0x64] = &&op_0x64;
        dispatch_table[0x65] = &&op_0x65;
        dispatch_table[0x66] = &&op_0x66;
        dispatch_table[0x67] = &&op_0x67;
        dispatch_table[0x68] = &&op_0x68;
        dispatch_table[0x69] = &&op_0x69;
        dispatch_table[0x6A] = &&op_0x6A;
        dispatch_table[0x6B] = &&op_0x6B;
        dispatch_table[0x6C] = &&op_0x6C;
        dispatch_table[0x6D] = &&op_0x6D;
        dispatch_table[0x6E] = &&op_0x6E;
        dispatch_table[0x6F] = &&op_0x6F;
        dispatch_table[0x70] = &&op_0x70;
        dispatch_table[0x71] = &&op_0x71;
        dispatch_table[0x72] = &&op_0x72;
        dispatch_table[0x73] = &&op_0x73;
        dispatch_table[0x74] = &&op_0x74;
        dispatch_table[0x75] = &&op_unhandled; // RESERVED as per spec v1.7
        dispatch_table[0x76] = &&op_0x76;
        dispatch_table[0x77] = &&op_unhandled; // RESERVED as per spec v1.7
        dispatch_table[0x78] = &&op_0x78;
        dispatch_table[0x79] = &&op_0x79;
        dispatch_table[0x7A] = &&op_0x7A;
        dispatch_table[0x7B] = &&op_0x7B;
        dispatch_table[0x80] = &&op_0x80;
        dispatch_table[0x81] = &&op_0x81;
        dispatch_table[0x82] = &&op_0x82;
        dispatch_table[0x83] = &&op_0x83;
        dispatch_table[0x84] = &&op_0x84;
        dispatch_table[0x85] = &&op_0x85;
        dispatch_table[0x86] = &&op_0x86;
        dispatch_table[0x87] = &&op_0x87;
        dispatch_table[0x88] = &&op_0x88;
        dispatch_table[0x89] = &&op_0x89;
        dispatch_table[0x8E] = &&op_0x8E;
        dispatch_table[0x8F] = &&op_0x8F;
        dispatch_table[0x90] = &&op_0x90;
        dispatch_table[0x92] = &&op_0x92;
        dispatch_table[0x93] = &&op_0x93;
        dispatch_table[0x94] = &&op_0x94;
        dispatch_table[0x95] = &&op_0x95;
        dispatch_table[0x96] = &&op_0x96;
        dispatch_table[0x97] = &&op_0x97;
        dispatch_table[0x98] = &&op_0x98;
        dispatch_table[0x99] = &&op_0x99;
        dispatch_table[0x9B] = &&op_0x9B;
        dispatch_table[0x9C] = &&op_0x9C;
        dispatch_table[0x9D] = &&op_0x9D;
        dispatch_table[0x9E] = &&op_0x9E;
        dispatch_table[0x9F] = &&op_0x9F;
        dispatch_table[0xA0] = &&op_0xA0;
        dispatch_table[0xA1] = &&op_0xA1;
        dispatch_table[0xA4] = &&op_0xA4;
        dispatch_table[0xA5] = &&op_0xA5;
        dispatch_table[0xA6] = &&op_0xA6;
        dispatch_table[0xA7] = &&op_0xA7;
        dispatch_table[0xA8] = &&op_0xA8;
        dispatch_table[0xA9] = &&op_0xA9;
        dispatch_table[0xAA] = &&op_0xAA;
        dispatch_table[0xAB] = &&op_0xAB;
        dispatch_table[0xAC] = &&op_0xAC;
        dispatch_table[0xAD] = &&op_0xAD;
        dispatch_table[0xAE] = &&op_0xAE;
        dispatch_table[0xAF] = &&op_0xAF;
        dispatch_table[0xB0] = &&op_0xB0;
        dispatch_table[0xB1] = &&op_0xB1;
        dispatch_table[0xB2] = &&op_0xB2;
        dispatch_table[0xB3] = &&op_0xB3;
        dispatch_table[0xB4] = &&op_0xB4;
        dispatch_table[0xB5] = &&op_0xB5;
        dispatch_table[0xB6] = &&op_unhandled; // RESERVED
        dispatch_table[0xB7] = &&op_unhandled; // RESERVED
        dispatch_table[0xB8] = &&op_unhandled; // BITCAST not implemented
        dispatch_table[0xB9] = &&op_unhandled; // BITCAST not implemented
        dispatch_table[0xBA] = &&op_unhandled; // BITCAST not implemented
        dispatch_table[0xBB] = &&op_unhandled; // BITCAST not implemented
        dispatch_table[0xBD] = &&op_0xBD;
        dispatch_table[0xBE] = &&op_0xBE;
        dispatch_table[0xBF] = &&op_0xBF;
        dispatch_table[0xC0] = &&op_0xC0;
        dispatch_table[0xC1] = &&op_0xC1;
        dispatch_table[0xC2] = &&op_0xC2;
        dispatch_table[0xC3] = &&op_0xC3;
        dispatch_table[0xC4] = &&op_0xC4;
        dispatch_table[0xC5] = &&op_0xC5;
        dispatch_table[0xC6] = &&op_0xC6;
        dispatch_table[0xC7] = &&op_0xC7;
        dispatch_table[0xC8] = &&op_0xC8;
        dispatch_table[0xC9] = &&op_0xC9;
        dispatch_table[0xCA] = &&op_0xCA;
        dispatch_table[0xCB] = &&op_0xCB;
        dispatch_table[0xCC] = &&op_0xCC;
        dispatch_table[0xCD] = &&op_0xCD;
        dispatch_table[0xCE] = &&op_0xCE;
        dispatch_table[0xCF] = &&op_0xCF;
        dispatch_table[0xD0] = &&op_0xD0;
        dispatch_table[0xD1] = &&op_0xD1;
        dispatch_table[0xD2] = &&op_0xD2;
        dispatch_table[0xD3] = &&op_0xD3;
        dispatch_table[0xD4] = &&op_0xD4;
        dispatch_table[0xD5] = &&op_0xD5;
        dispatch_table[0xD6] = &&op_0xD6;
        dispatch_table[0xD7] = &&op_0xD7;
        dispatch_table[0xD8] = &&op_0xD7;
        dispatch_table[0xD9] = &&op_0xD7;
        dispatch_table[0xDA] = &&op_0xD7;
        dispatch_table[0xDB] = &&op_0xD7;
        dispatch_table[0xDC] = &&op_0xD7;
        dispatch_table[0xDD] = &&op_0xDD;
        dispatch_table[0xDE] = &&op_0xDE;
        dispatch_table[0xDF] = &&op_0xDF;
        dispatch_table[0xE0] = &&op_0xE0;
        dispatch_table[0xE1] = &&op_0xE1;
        dispatch_table[0xE2] = &&op_0xE2;
        dispatch_table[0xE3] = &&op_0xE3;
        dispatch_table[0xE4] = &&op_0xE4;
        dispatch_table[0xE5] = &&op_0xE5;
        dispatch_table[0xE6] = &&op_0xE6;
        dispatch_table[0xE7] = &&op_0xE7;
        dispatch_table[0xE8] = &&op_0xE8;
        dispatch_table[0xE9] = &&op_0xE9;
        dispatch_table[0xEA] = &&op_0xEA;
        dispatch_table[0xEB] = &&op_0xEB;
        dispatch_table[0xEC] = &&op_0xEC;
        dispatch_table[0xED] = &&op_0xED;
        dispatch_table[0xEE] = &&op_0xEE;
        dispatch_table[0xEF] = &&op_unhandled; // RESERVED
        dispatch_table[0xF0] = &&op_0xF0;
        dispatch_table[0xF1] = &&op_0xF0;
        dispatch_table[0xF2] = &&op_0xF0;
        dispatch_table[0xF3] = &&op_0xF0;
        dispatch_table[0xF4] = &&op_0xF0;
        dispatch_table[0xF5] = &&op_0xF0;
        dispatch_table[0xF6] = &&op_0xF6;
        dispatch_table[0xF7] = &&op_unhandled; // RESERVED
        dispatch_table[0xF8] = &&op_unhandled; // TRY not implemented
        dispatch_table[0xF9] = &&op_unhandled; // CATCH not implemented
        dispatch_table[0xFA] = &&op_unhandled; // CATCH_ALL not implemented
        dispatch_table[0xFB] = &&op_unhandled; // THROW not implemented
        dispatch_table[0xFD] = &&op_unhandled; // PLATFORM.OP not implemented
        dispatch_table[0xFE] = &&op_unhandled; // PLATFORM.OP not implemented
        dispatch_table[0xFF] = &&op_unhandled; // RESERVED
        dispatch_table[0x09] = &&op_0x09;
        dispatch_table[0x0F] = &&op_0x0F;
        dispatch_table[0x10] = &&op_0x10;
        dispatch_table[0x1D] = &&op_0x1D;
        dispatch_table[0x1E] = &&op_0x1E;
        dispatch_table[0x1F] = &&op_0x1F;
        dispatch_table[0x56] = &&op_0x56;
        dispatch_table[0x57] = &&op_unhandled; // RESERVED as per spec v1.7
        dispatch_table[0x58] = &&op_0x58;
        dispatch_table[0xBC] = &&op_0xBC;
        dispatch_table[0xFC] = &&op_0xFC;
        table_initialized = true;
    }
#endif

        interpreter_loop_start:
                if (__builtin_expect(!(pc < instructions_end_ptr && !end_reached), 0)) {
                    goto interpreter_loop_end;
                }
            const long pos = (long)(pc - instructions_ptr);
            uint8_t opcode = *pc++;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            #if ESPB_RUNTIME_OC_DEBUG
            ESP_LOGD(TAG, "ESPB DEBUG: exec pc=%ld opcode=0x%02X", pos, opcode);
#endif
#endif
            /*
            // ДИАГНОСТИКА: Периодически отдаем управление планировщику
            instruction_count++;
            if (instruction_count % 50000 == 0) {
                TickType_t current_time = xTaskGetTickCount();
                ESP_LOGD(TAG, "ESPB DEBUG: Executed %u instructions, time elapsed: %u ticks", 
                        instruction_count, (unsigned)(current_time - last_yield_time));
                
                // КРИТИЧЕСКАЯ ДИАГНОСТИКА: Проверяем состояние замыканий
                // Проверяем только если система колбэков уже была инициализирована
                static bool timer_callbacks_expected = false;
                static TickType_t last_timer_check = 0;
                TickType_t now = xTaskGetTickCount();
                
                EspbClosureCtx *active_closures = NULL;
                if (espb_get_active_closures(&active_closures) == ESPB_OK && active_closures) {
                    EspbCallbackClosure *closure = (EspbCallbackClosure*)active_closures;
                    ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: closure_ctx=%p, executable_code=%p, closure_ptr=%p",
                            closure, closure->executable_code, closure->closure_ptr);

                    if (closure->callback_info) {
                        ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: espb_func_idx=%u, original_user_data=%p",
                                closure->callback_info->espb_func_idx, closure->callback_info->original_user_data);
                    }

                    // ПРОВЕРЯЕМ ВАЛИДНОСТЬ ИСПОЛНЯЕМОЙ ПАМЯТИ
                    if (closure->executable_code) {
                        // Пытаемся прочитать первые 4 байта исполняемого кода
                        uint32_t *exec_ptr = (uint32_t*)closure->executable_code;
                        uint32_t first_word = *exec_ptr;
                        ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: executable_code first_word=0x%08x", first_word);

                        // Проверяем, что это не мусор
                        if (first_word == 0x00000000 || first_word == 0xFFFFFFFF || first_word == 0xDEADBEEF) {
                            ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: EXECUTABLE CODE LOOKS CORRUPTED!");
                        }
                    } else {
                        ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: EXECUTABLE CODE IS NULL!");
                    }
                    
                    // Отмечаем, что таймерные колбэки ожидаются
                    timer_callbacks_expected = true;
                    last_timer_check = now;
                } else {
                    // Выводим предупреждение только если:
                    // 1. Таймерные колбэки уже создавались ранее И
                    // 2. Прошло достаточно времени с последней проверки (избегаем спама)
                    if (timer_callbacks_expected && (now - last_timer_check) > pdMS_TO_TICKS(5000)) {
                        ESP_LOGD(TAG, "ESPB CLOSURE HEALTH: NO ACTIVE CLOSURES - TIMER MAY HAVE STOPPED!");
                        last_timer_check = now;
                    }
                    // В остальных случаях (во время обычных вычислений) не выводим предупреждение
                }
                
                // Отдаем управление планировщику каждые 50 инструкций
                ESP_LOGD(TAG, "ESPB DEBUG: Yielding to scheduler...");
                taskYIELD();
                last_yield_time = current_time;
            }
*/
             goto *dispatch_table[opcode];
        op_unhandled:;
                // --- Устаревшие блочные инструкции - теперь не должны генерироваться ---
                // op_0x06 (BLOCK) and op_0x07 (LOOP) are obsolete and have been removed.
                op_0x0B: { // CALL_INDIRECT Rfunc(u8), type_idx(u16)
                    uint8_t r_func_idx = READ_U8();
                    uint16_t expected_type_idx = READ_U16();

                    DEBUG_CHECK_REG(r_func_idx, max_reg_used, "CALL_INDIRECT");
                    if (0) { // Unreachable after debug check
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    uint32_t local_func_idx_to_call = V_I32(locals[r_func_idx]);
                    bool resolved_from_ptr = false;
                    
                    if (local_func_idx_to_call >= module->num_functions) {
                        // Возможно в регистре хранится указатель на функцию в data сегменте
                        // или смещение в data сегменте (если загружено из массива указателей)
                        uintptr_t mem_base = (uintptr_t)instance->memory_data;
                        uintptr_t mem_end = mem_base + instance->memory_size_bytes;
                        uintptr_t ptr_val = (uintptr_t)V_PTR(locals[r_func_idx]);
                        uint32_t data_offset = 0;
                        bool found_offset = false;
                        
                        if (ptr_val >= mem_base && ptr_val < mem_end) {
                            // Случай 1: ptr_val - это реальный указатель в memory_data
                            data_offset = (uint32_t)(ptr_val - mem_base);
                            found_offset = true;
                        } else if (ptr_val > 0 && ptr_val < instance->memory_size_bytes) {
                            // Случай 2: ptr_val - это смещение в data сегменте (загружено из массива)
                            // Это происходит когда массив функциональных указателей хранит смещения
                            data_offset = (uint32_t)ptr_val;
                            found_offset = true;
                        }
                        
                        if (found_offset && module->func_ptr_map && module->num_func_ptr_map_entries > 0) {
                            EspbFuncPtrMapEntry *found_entry = (EspbFuncPtrMapEntry *)bsearch(
                                &data_offset,
                                module->func_ptr_map,
                                module->num_func_ptr_map_entries,
                                sizeof(EspbFuncPtrMapEntry),
                                compare_func_ptr_map_entry_for_search
                            );
                            if (found_entry) {
                                local_func_idx_to_call = found_entry->function_index;
                                resolved_from_ptr = true;
                            } else {
                                ESP_LOGE(TAG, "CALL_INDIRECT: data_offset %u not found in func_ptr_map", data_offset);
                                return ESPB_ERR_INVALID_FUNC_INDEX;
                            }
                        } else {
                            ESP_LOGE(TAG, "CALL_INDIRECT: Invalid ptr_val 0x%lx (mem_base=0x%lx, mem_end=0x%lx, mem_size=%u)", 
                                     (unsigned long)ptr_val, (unsigned long)mem_base, (unsigned long)mem_end, 
                                     (unsigned)instance->memory_size_bytes);
                            return ESPB_ERR_INVALID_FUNC_INDEX;
                        }
                    }
                    uint32_t actual_sig_idx = module->function_signature_indices[local_func_idx_to_call];
                    if (actual_sig_idx != expected_type_idx) {
                        // Если вызов разрешён через указатель, разрешаем несовпадение индекса при совместимых сигнатурах
                        if (expected_type_idx < module->num_signatures && actual_sig_idx < module->num_signatures) {
                            const EspbFuncSignature* expected_sig = &module->signatures[expected_type_idx];
                            const EspbFuncSignature* actual_sig = &module->signatures[actual_sig_idx];
                            if (!signatures_are_compatible(expected_sig, actual_sig)) {
                                return ESPB_ERR_TYPE_MISMATCH;
                            }
                        } else if (!resolved_from_ptr) {
                            return ESPB_ERR_TYPE_MISMATCH;
                        }
                    }

                    EspbFunctionBody* callee_body = &module->function_bodies[local_func_idx_to_call];

#if CONFIG_ESPB_JIT_ENABLED
                    // Минимальный hot-path: если для вызываемой функции есть скомпилированный JIT-код — выполняем его.
                    // Вся "тяжёлая" логика вынесена в cold helper, чтобы не замедлять интерпретатор.
                    if (__builtin_expect(callee_body->is_jit_compiled && callee_body->jit_code != NULL, 0)) {
                        uint32_t sig_idx_indirect = module->function_signature_indices[local_func_idx_to_call];
                        const EspbFuncSignature* callee_sig_indirect = &module->signatures[sig_idx_indirect];
                        if (espb_try_call_jit_for_call_indirect(instance, exec_ctx, num_imported_funcs,
                                                               num_virtual_regs, local_func_idx_to_call,
                                                               callee_sig_indirect, locals)) {
                            goto interpreter_loop_start;
                        }
                    }
#endif

                    // JIT выключен или не применим - стандартный путь через интерпретатор
                    const EspbFuncSignature* callee_sig = &module->signatures[actual_sig_idx];

                    size_t saved_frame_size = num_virtual_regs * sizeof(Value);
                    size_t callee_frame_size = callee_body->header.num_virtual_regs * sizeof(Value);

                    // 1. Проверяем, хватит ли места (быстрый путь inline)
                    if (__builtin_expect(exec_ctx->sp + saved_frame_size + callee_frame_size > exec_ctx->shadow_stack_capacity, 0)) {
                        int stack_status = _espb_grow_shadow_stack(exec_ctx, saved_frame_size + callee_frame_size);
                        if (stack_status < 0) { return ESPB_ERR_OUT_OF_MEMORY; }
                        if (stack_status > 0) { // Буфер перемещен, обновляем указатель на locals
                            locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                        }
                    }

                    // 2. Копируем текущий кадр (locals) в теневой стек
                    Value* saved_frame_location = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->sp);
                    memcpy(saved_frame_location, locals, saved_frame_size);

                    // 3. Сохраняем контекст вызывающей стороны
                    int return_pc = (int)(pc - instructions_ptr);
                    if (push_call_frame(exec_ctx, return_pc, exec_ctx->fp, local_func_idx, saved_frame_location, num_virtual_regs) != ESPB_OK) {
                        return ESPB_ERR_STACK_OVERFLOW; // Не должно произойти, так как стек вызовов не растет
                    }

                    // 4. Изолируем аргументы во временном буфере
                    Value temp_args[FFI_ARGS_MAX];
                    uint32_t num_args_to_copy = MIN(callee_sig->num_params, FFI_ARGS_MAX);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if(i < num_virtual_regs) temp_args[i] = locals[i];
                    }

                    // 5. Выделяем новый кадр
                    exec_ctx->fp = exec_ctx->sp + saved_frame_size; // Новый кадр начинается ПОСЛЕ сохраненного
                    exec_ctx->sp = exec_ctx->fp + callee_frame_size;   // Вершина стека сдвигается на размер нового кадра

                    // 6. Копируем аргументы в новый кадр
                    Value* callee_locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                    memset(callee_locals, 0, callee_frame_size);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if (i < callee_body->header.num_virtual_regs) callee_locals[i] = temp_args[i];
                    }
                    
                    // 7. Обновляем контекст интерпретатора для вызываемой функции
                    local_func_idx = local_func_idx_to_call;
                    pc = callee_body->code;
                    instructions_ptr = callee_body->code;
                    instructions_end_ptr = callee_body->code + callee_body->code_size;
                    locals = callee_locals;
                    num_virtual_regs = callee_body->header.num_virtual_regs;
                    
                    goto interpreter_loop_start;
                }

                
op_0x0D: { // CALL_INDIRECT_PTR Rdst, Rptr, type_idx
    // 1. Чтение операндов
    uint8_t func_ptr_reg = READ_U8();
    uint16_t type_idx = READ_U16();

    if (!CHECK_TYPE(locals[func_ptr_reg], ESPB_TYPE_PTR)) {
        ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Register R%u does not contain a pointer.", func_ptr_reg);
        return ESPB_ERR_TYPE_MISMATCH;
    }
    void* target_ptr = V_PTR(locals[func_ptr_reg]);
    if (!target_ptr) {
        ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Pointer in R%u is NULL.", func_ptr_reg);
        return ESPB_ERR_INVALID_OPERAND;
    }

    // 2. Определяем, является ли указатель смещением в памяти данных или нативным указателем
    uintptr_t mem_base = (uintptr_t)instance->memory_data;
    uintptr_t mem_end = mem_base + instance->memory_size_bytes;
    uint32_t data_offset = 0;
    bool is_in_data_segment = ((uintptr_t)target_ptr >= mem_base && (uintptr_t)target_ptr < mem_end);
    
    if (is_in_data_segment) {
        data_offset = (uint32_t)((uintptr_t)target_ptr - mem_base);
        ESP_LOGD(TAG, "CALL_INDIRECT_PTR: Pointer %p is in data segment at offset %u.", target_ptr, data_offset);
    } else {
        ESP_LOGD(TAG, "CALL_INDIRECT_PTR: Pointer %p is a native pointer.", target_ptr);
    }
    
    EspbFuncPtrMapEntry *found_entry = NULL;
    if (is_in_data_segment && module->func_ptr_map && module->num_func_ptr_map_entries > 0) {
        found_entry = (EspbFuncPtrMapEntry *)bsearch(
            &data_offset,
            module->func_ptr_map,
            module->num_func_ptr_map_entries,
            sizeof(EspbFuncPtrMapEntry),
            compare_func_ptr_map_entry_for_search
        );
    }

    if (found_entry) {
        /************************************************************************
         * ПУТЬ А: Указатель найден в карте. Это ESPB-функция.
         ************************************************************************/
        uint32_t callee_local_func_idx = found_entry->function_index;
        ESP_LOGD(TAG, "CALL_INDIRECT_PTR: Found ESPB function index %u via map for data offset %u.", callee_local_func_idx, data_offset);

        // Проверяем сигнатуру (обязательно!)
        if (callee_local_func_idx >= module->num_functions) {
            ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Mapped function index %u is out of bounds.", callee_local_func_idx);
            return ESPB_ERR_INVALID_FUNC_INDEX;
        }
        uint32_t actual_sig_idx = module->function_signature_indices[callee_local_func_idx];
        if (actual_sig_idx != type_idx) {
            if (type_idx < module->num_signatures && actual_sig_idx < module->num_signatures) {
                const EspbFuncSignature* expected_sig = &module->signatures[type_idx];
                const EspbFuncSignature* actual_sig = &module->signatures[actual_sig_idx];
                if (signatures_are_compatible(expected_sig, actual_sig)) {
                    ESP_LOGW(TAG, "CALL_INDIRECT_PTR: Signature index mismatch (expected %u, got %u), but signatures are compatible. Proceeding.", type_idx, actual_sig_idx);
                } else {
                    ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Type mismatch. Expected sig %u, found %u for func %u. Signatures are incompatible.", type_idx, actual_sig_idx, callee_local_func_idx);
                    return ESPB_ERR_TYPE_MISMATCH;
                }
            } else {
                ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Type mismatch and one of the signature indices is out of bounds. Expected %u, found %u.", type_idx, actual_sig_idx);
                return ESPB_ERR_TYPE_MISMATCH;
            }
        }

        // --- Код для "хвостового" вызова (адаптирован из op_0x0A) ---
        const EspbFunctionBody* callee_body = &module->function_bodies[callee_local_func_idx];
        const EspbFuncSignature* callee_sig = &module->signatures[actual_sig_idx];
        size_t saved_frame_size = num_virtual_regs * sizeof(Value);
        size_t callee_frame_size = callee_body->header.num_virtual_regs * sizeof(Value);

        if (__builtin_expect(exec_ctx->sp + saved_frame_size + callee_frame_size > exec_ctx->shadow_stack_capacity, 0)) {
            int stack_status = _espb_grow_shadow_stack(exec_ctx, saved_frame_size + callee_frame_size);
            if (stack_status < 0) { return ESPB_ERR_OUT_OF_MEMORY; }
            if (stack_status > 0) { // Буфер перемещен, обновляем указатель на locals
                locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
            }
        }
        Value* saved_frame_location = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->sp);
        memcpy(saved_frame_location, locals, saved_frame_size);

        int return_pc = (int)(pc - instructions_ptr);
        if (push_call_frame(exec_ctx, return_pc, exec_ctx->fp, local_func_idx, saved_frame_location, num_virtual_regs) != ESPB_OK) {
            return ESPB_ERR_STACK_OVERFLOW;
        }
        
        Value temp_args[FFI_ARGS_MAX];
        uint32_t num_args_to_copy = MIN(callee_sig->num_params, FFI_ARGS_MAX);
        for (uint32_t i = 0; i < num_args_to_copy; i++) {
            // Аргументы расположены в R0, R1, R2, ...
            // Если func_ptr_reg находится среди аргументов, пропускаем его (сдвигаем индексы)
            uint32_t src_reg = i;
            if (func_ptr_reg <= src_reg) {
                src_reg++; // Skip func_ptr_reg
            }
            if (src_reg < num_virtual_regs) {
                temp_args[i] = locals[src_reg];
            } else {
                // Если не хватает аргументов, заполняем нулями (или можно вернуть ошибку)
                memset(&temp_args[i], 0, sizeof(Value));
            }
        }

        exec_ctx->fp = exec_ctx->sp + saved_frame_size;
        exec_ctx->sp = exec_ctx->fp + callee_frame_size;
        
        Value* callee_locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
        memset(callee_locals, 0, callee_frame_size);
        for (uint32_t i = 0; i < num_args_to_copy; i++) {
            if(i < callee_body->header.num_virtual_regs) callee_locals[i] = temp_args[i];
        }
        
        local_func_idx = callee_local_func_idx;
        pc = callee_body->code;
        instructions_ptr = callee_body->code;
        instructions_end_ptr = callee_body->code + callee_body->code_size;
        locals = callee_locals;
        num_virtual_regs = callee_body->header.num_virtual_regs;
        
        goto interpreter_loop_start;

    } else if (is_in_data_segment) {
        /************************************************************************
         * ПУТЬ Б: Указатель в сегменте данных, но не в карте. Это ошибка.
         ************************************************************************/
        ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Pointer %p is in data segment but not found in func_ptr_map. This is an invalid function pointer.", target_ptr);
        return ESPB_ERR_INVALID_FUNC_INDEX;
    } else {
        /************************************************************************
         * ПУТЬ В: Указатель не в карте и не в сегменте данных. Это нативный код.
         ************************************************************************/
        ESP_LOGD(TAG, "CALL_INDIRECT_PTR: Pointer %p not in ESPB data segment, assuming native call via FFI.", target_ptr);
        
        EspbFuncSignature* func_sig = &module->signatures[type_idx];

        ffi_cif cif;
        ffi_type* ffi_arg_types[FFI_ARGS_MAX];
        void* ffi_arg_values[FFI_ARGS_MAX];

        if (func_sig->num_params > FFI_ARGS_MAX) {
            return ESPB_ERR_INVALID_OPERAND;
        }

        for (uint32_t i = 0; i < func_sig->num_params; i++) {
            ffi_arg_types[i] = espb_type_to_ffi_type(func_sig->param_types[i]);
            if (!ffi_arg_types[i]) return ESPB_ERR_TYPE_MISMATCH;
            
            // Аргументы расположены в R0, R1, R2, ...
            // Если func_ptr_reg находится среди аргументов, пропускаем его (сдвигаем индексы)
            uint32_t src_reg_idx = i;
            if (func_ptr_reg <= src_reg_idx) {
                src_reg_idx++; // Skip func_ptr_reg
            }
            DEBUG_CHECK_REG(src_reg_idx, max_reg_used, "CALL_INDIRECT_PTR");
            (void)src_reg_idx; // Избегаем warning об unused переменной

            switch(func_sig->param_types[i]) {
                case ESPB_TYPE_I32: ffi_arg_values[i] = &V_I32(locals[src_reg_idx]); break;
                case ESPB_TYPE_U32: ffi_arg_values[i] = &V_I32(locals[src_reg_idx]); break;
                case ESPB_TYPE_PTR: ffi_arg_values[i] = &V_PTR(locals[src_reg_idx]); break;
                case ESPB_TYPE_I64: ffi_arg_values[i] = &V_I64(locals[src_reg_idx]); break;
                case ESPB_TYPE_U64: ffi_arg_values[i] = &V_I64(locals[src_reg_idx]); break;
                case ESPB_TYPE_F32: ffi_arg_values[i] = &V_F32(locals[src_reg_idx]); break;
                case ESPB_TYPE_F64: ffi_arg_values[i] = &V_F64(locals[src_reg_idx]); break;
                default:
                    return ESPB_ERR_TYPE_MISMATCH;
            }
        }

        ffi_type* ffi_ret_type = &ffi_type_void;
        if (func_sig->num_returns > 0) {
            ffi_ret_type = espb_type_to_ffi_type(func_sig->return_types[0]);
        }
        
        if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, func_sig->num_params, ffi_ret_type, ffi_arg_types) != FFI_OK) {
            return ESPB_ERR_RUNTIME_ERROR;
        }
        
        union { int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; float f32; double f64; void* p; } ret_val;
        ffi_call(&cif, FFI_FN(target_ptr), &ret_val, ffi_arg_values);

        if (func_sig->num_returns > 0) {
            // Store result in R0
            switch(func_sig->return_types[0]) {
                case ESPB_TYPE_I32: SET_TYPE(locals[0], ESPB_TYPE_I32); V_I32(locals[0]) = ret_val.i32; break;
                case ESPB_TYPE_U32: SET_TYPE(locals[0], ESPB_TYPE_U32); V_I32(locals[0]) = ret_val.u32; break;
                case ESPB_TYPE_I64: SET_TYPE(locals[0], ESPB_TYPE_I64); V_I64(locals[0]) = ret_val.i64; break;
                case ESPB_TYPE_U64: SET_TYPE(locals[0], ESPB_TYPE_U64); V_I64(locals[0]) = ret_val.u64; break;
                case ESPB_TYPE_F32: SET_TYPE(locals[0], ESPB_TYPE_F32); V_F32(locals[0]) = ret_val.f32; break;
                case ESPB_TYPE_F64: SET_TYPE(locals[0], ESPB_TYPE_F64); V_F64(locals[0]) = ret_val.f64; break;
                case ESPB_TYPE_PTR: SET_TYPE(locals[0], ESPB_TYPE_PTR); V_PTR(locals[0]) = ret_val.p; break;
                default: break; // Should not happen if ffi_prep_cif succeeded
            }
        }
        goto interpreter_loop_start;
    }
}

                // --- Новые инструкции перехода ---
                op_0x02: { // BR offset(i16)
                    int16_t offset = READ_I16();
                    
                    ESP_LOGD(TAG, "BR by %d, current_pc_offset=%ld", offset, (long)((pc - 3) - instructions_ptr));
                    
                    // ДИАГНОСТИКА: Проверяем на подозрительный offset=0 в цикле
                    #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (offset == 0) {
                        ESP_LOGD(TAG, "ESPB WARNING: BR with offset=0 detected - this may be a translator bug!");
                        ESP_LOGD(TAG, "ESPB WARNING: This will create infinite loop on same instruction");
                        // Пока что выполняем как есть, но логируем проблему
                    }
                    #endif
                    // Смещение отсчитывается от начала текущей инструкции.
                    // pc был инкрементирован на 1 (opcode) + 2 (offset) = 3 байта.
                    // Возвращаемся к началу инструкции и прибавляем смещение.
                    pc = (pc - 3) + offset;
                    #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "BR jump to pc_offset=%ld", (long)(pc - instructions_ptr));
                    #endif
                    goto interpreter_loop_start; // Начинаем цикл с новым pc
                }
         op_0x03: { // BR_IF reg(u8), offset(i16) - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
             uint8_t cond_reg = READ_U8();
             int16_t offset = READ_I16();
         
             // Проверка границ с branch prediction hint (ошибка крайне редка)
             DEBUG_CHECK_REG(cond_reg, max_reg_used, "BR_IF");
             if (__builtin_expect(0, 0)) { // Unreachable after debug check
                 #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                 ESP_LOGE(TAG, "BR_IF - cond register R%u out of bounds.", cond_reg);
                 #endif
                 return ESPB_ERR_INVALID_REGISTER_INDEX;
             }
         
             // ОПТИМИЗАЦИЯ: Прямой доступ к регистру вместо копирования Value
             Value *reg_ptr = &locals[cond_reg];
             bool condition_true = (V_I32(*reg_ptr) != 0);

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "BR_IF R%u (%s), offset %d", 
             cond_reg, condition_true ? "true" : "false", offset);
#endif

    // PC калькуляция (критично для корректности!):
    // pc сейчас указывает на offset (после opcode и cond_reg)
    // offset считается от начала инструкции (opcode)
    if (__builtin_expect(condition_true, 0)) {
        // BRANCH TAKEN: инструкция занимает 4 байта (1+1+2). pc сейчас на start+4.
        // Нужно вернуться к началу инструкции и прибавить смещение.
        pc = (pc - 4) + offset;
    } else {
        // BRANCH NOT TAKEN: pc уже указывает на следующую инструкцию. Ничего делать не нужно.
    }
    goto interpreter_loop_start;
}
                op_0x18: { // LDC.I32.IMM Rd(u8), imm32
                    uint8_t rd = READ_U8();
                    int32_t imm = READ_I32();
                    DEBUG_CHECK_REG(rd, max_reg_used, "LDC.I32.IMM");
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.I32.IMM R%d, %" PRId32, rd, imm);
#endif
                goto interpreter_loop_start;
            }
                
                op_0x8F: { // ALLOCA Rd(u8), Rs(u8), align(u8) - NEW HEAP-BASED IMPLEMENTATION
                    uint8_t rd_alloc = READ_U8();
                    uint8_t rs_alloc_size_reg = READ_U8();
                    uint8_t align = READ_U8();

                    // Проверки валидности регистров
                    DEBUG_CHECK_REG(rs_alloc_size_reg, max_reg_used, "HEAP_ALLOC");
                    if (0) { // Unreachable after debug check
                        ESP_LOGE(TAG, "ALLOCA - Size register R%u out of bounds", rs_alloc_size_reg);
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    DEBUG_CHECK_REG(rd_alloc, max_reg_used, "HEAP_ALLOC");
                    if (0) { // Unreachable after debug check
                        ESP_LOGE(TAG, "ALLOCA - Dest register R%u out of bounds", rd_alloc);
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    // Корректировка выравнивания
                    if (align == 0 || (align & (align - 1)) != 0) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "ESPB WARNING: ALLOCA - Invalid alignment %u, using 4", align);
#endif
                        align = 4;
                    }

                    // Получение размера
                    uint32_t size_to_alloc = V_I32(locals[rs_alloc_size_reg]);
                    if (size_to_alloc == 0 || size_to_alloc > 65536) { // Максимум 64KB
                        ESP_LOGE(TAG, "ALLOCA - Invalid size %" PRIu32, size_to_alloc);
                        return ESPB_ERR_INVALID_OPERAND;
                    }

                    // Use the frame of the currently executing function.
                    // This is now safe because a base frame is pushed for the entry point function.
                    RuntimeFrame *frame = &exec_ctx->call_stack[exec_ctx->call_stack_top - 1];
                    if (frame->alloca_count >= 32) {
                        ESP_LOGE(TAG, "ALLOCA - Too many allocations per frame (max 32)");
                        return ESPB_ERR_OUT_OF_MEMORY;
                    }

                    // Выделение памяти через heap manager с обязательным выравниванием по 8 байт
                    // для совместимости с i64 операциями
                    size_t required_alignment = (align > 8) ? align : 8; // Минимум 8 байт для i64
                    void *allocated_ptr = espb_heap_malloc_aligned(instance, size_to_alloc, required_alignment);
                    
                    // Отмечаем что используется aligned allocation если alignment > 4
                    if (required_alignment > 4) {
                        frame->has_custom_aligned = true;
                    }
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ALLOCA heap allocation: size=%u, requested_align=%u, used_align=%zu, ptr=%p",
                             size_to_alloc, align, required_alignment, allocated_ptr);
#endif

                    if (!allocated_ptr) {
                        ESP_LOGE(TAG, "ALLOCA - heap allocation failed for %" PRIu32 " bytes", size_to_alloc);
                        return ESPB_ERR_OUT_OF_MEMORY;
                    }

                    // Дополнительная проверка безопасности
                    uintptr_t ptr_addr = (uintptr_t)allocated_ptr;
                    uintptr_t mem_base = (uintptr_t)instance->memory_data;
                    uintptr_t mem_end = mem_base + instance->memory_size_bytes;
                    
                    if (ptr_addr < mem_base || ptr_addr >= mem_end) {
                        ESP_LOGE(TAG, "ALLOCA ptr %p outside memory bounds [%p,%p)",
                                allocated_ptr, (void*)mem_base, (void*)mem_end);
                        // Освобождаем и возвращаем ошибку
                        espb_heap_free(instance, allocated_ptr);
                        return ESPB_ERR_OUT_OF_MEMORY;
                    }

                    // Отслеживаем выделение
                    frame->alloca_ptrs[frame->alloca_count] = allocated_ptr;
                    frame->alloca_count++;

                    // Устанавливаем результат
                    SET_TYPE(locals[rd_alloc], ESPB_TYPE_PTR);
                    V_PTR(locals[rd_alloc]) = allocated_ptr;

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ALLOCA SUCCESS: R%" PRIu8 "=%p size=%" PRIu32 " align=%u heap_managed",
                             rd_alloc, allocated_ptr, size_to_alloc, align);
#endif
                    goto interpreter_loop_start;
                }
                
                op_0x04: { // BR_TABLE Ridx(u8), num_targets(u16), [target_offsets(i16)...], default_offset(i16)
                    uint8_t ridx = READ_U8();
                    uint16_t num_targets = READ_U16();
                    
                    DEBUG_CHECK_REG(ridx, max_reg_used, "LD_GLOBAL_ADDR");
                    if (0) { // Unreachable after debug check
                        ESP_LOGE(TAG, "BR_TABLE - Index register R%u out of bounds", ridx);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    uint32_t index = (uint32_t)V_I32(locals[ridx]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "BR_TABLE R%u = %u, num_targets = %u", ridx, index, num_targets);
#endif
                    
                    // Читаем таблицу смещений
                    const uint8_t *table_start = pc;
                    pc += num_targets * sizeof(int16_t); // Пропускаем таблицу
                    
                    int16_t target_offset;
                    if (index < num_targets) {
                        // Используем смещение из таблицы
                        memcpy(&target_offset, table_start + index * sizeof(int16_t), sizeof(int16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "BR_TABLE: Using table entry %u, offset = %d", index, target_offset);
#endif
                    } else {
                        // Используем default смещение
                        memcpy(&target_offset, pc, sizeof(int16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "BR_TABLE: Using default offset = %d", target_offset);
#endif
                    }
                    pc += sizeof(int16_t); // Пропускаем default_offset
                    
                    // Выполняем переход
                    pc += target_offset;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "BR_TABLE: Jumping to PC += %d", target_offset);
#endif
                    goto interpreter_loop_start;
                }

                op_0x05: { // UNREACHABLE
                    ESP_LOGE(TAG, "TRAP: Reached an UNREACHABLE instruction at pc_offset=%ld. Halting execution.",
                           (long)((pc - 1) - instructions_ptr));
                    return ESPB_ERR_RUNTIME_TRAP;
                }

                // JIT_REGION_START (0x06) отключён: региональный JIT не реализуем и не планируем.
                // Опкод считается невалидным и уходит в op_unhandled через dispatch_table.


                op_0x0A: { // CALL local_func_idx(u16)
                    uint16_t local_func_idx_to_call = READ_U16();

                    if (local_func_idx_to_call >= module->num_functions) {
                        return ESPB_ERR_INVALID_FUNC_INDEX;
                    }

                    uint32_t sig_idx = module->function_signature_indices[local_func_idx_to_call];
                    EspbFunctionBody* callee_body = &module->function_bodies[local_func_idx_to_call];
                    const EspbFuncSignature* callee_sig = &module->signatures[sig_idx];

#if CONFIG_ESPB_JIT_ENABLED
                    // Минимальный hot-path: если для вызываемой функции есть скомпилированный JIT-код — выполняем его.
                    // Вся "тяжёлая" логика вынесена в cold helper, чтобы не замедлять интерпретатор.
                    if (__builtin_expect(callee_body->is_jit_compiled && callee_body->jit_code != NULL, 0)) {
                        if (espb_try_call_jit_for_call(instance, exec_ctx, num_imported_funcs,
                                                      num_virtual_regs, local_func_idx_to_call,
                                                      callee_sig, locals)) {
                            goto interpreter_loop_start;
                        }
                    }
#endif

                    // JIT выключен или не применим - стандартный путь через интерпретатор
                    bool is_leaf_function = (callee_body->header.flags & ESPB_FUNC_FLAG_IS_LEAF) != 0;

                    size_t saved_frame_size = num_virtual_regs * sizeof(Value);
                    size_t callee_frame_size = callee_body->header.num_virtual_regs * sizeof(Value);
                    
                    // --- Общая логика подготовки аргументов ---
                    Value temp_args[FFI_ARGS_MAX];
                    uint32_t num_args_to_copy = MIN(callee_sig->num_params, FFI_ARGS_MAX);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if (i < num_virtual_regs) temp_args[i] = locals[i];
                    }
                    
                    int return_pc = (int)(pc - instructions_ptr);

                    if (is_leaf_function) {
                        // ✅ БЫСТРЫЙ ПУТЬ (для листовых функций)
                        // Проверяем место только для нового кадра
                        if (__builtin_expect(exec_ctx->sp + callee_frame_size > exec_ctx->shadow_stack_capacity, 0)) {
                            if (_espb_grow_shadow_stack(exec_ctx, callee_frame_size) < 0) return ESPB_ERR_OUT_OF_MEMORY;
                        }

                        // 1. Сохраняем только адрес возврата, без регистров
                        if (push_call_frame(exec_ctx, return_pc, exec_ctx->fp, local_func_idx, NULL, 0) != ESPB_OK) {
                            return ESPB_ERR_STACK_OVERFLOW;
                        }

                        // 2. Новый кадр начинается с текущей позиции fp (на самом деле sp, но они сейчас равны)
                        exec_ctx->fp = exec_ctx->sp;
                        exec_ctx->sp = exec_ctx->fp + callee_frame_size;
                        
                    } else {
                        // 🐢 МЕДЛЕННЫЙ ПУТЬ (существующая логика)
                        // 1. Проверяем, хватит ли места для сохранения и для нового кадра
                        if (__builtin_expect(exec_ctx->sp + saved_frame_size + callee_frame_size > exec_ctx->shadow_stack_capacity, 0)) {
                            int stack_status = _espb_grow_shadow_stack(exec_ctx, saved_frame_size + callee_frame_size);
                            if (stack_status < 0) { return ESPB_ERR_OUT_OF_MEMORY; }
                            if (stack_status > 0) { // Буфер перемещен, обновляем указатель на locals
                                locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                            }
                        }

                        // 2. Копируем текущий кадр (locals) в теневой стек
                        Value* saved_frame_location = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->sp);
                        memcpy(saved_frame_location, locals, saved_frame_size);

                        // 3. Сохраняем контекст вызывающей стороны
                        if (push_call_frame(exec_ctx, return_pc, exec_ctx->fp, local_func_idx, saved_frame_location, num_virtual_regs) != ESPB_OK) {
                            return ESPB_ERR_STACK_OVERFLOW;
                        }

                        // 4. Выделяем новый кадр
                        exec_ctx->fp = exec_ctx->sp + saved_frame_size;
                        exec_ctx->sp = exec_ctx->fp + callee_frame_size;
                    }
                    
                    // --- Общая логика для обоих путей ---
                    
                    // Копируем аргументы в новый кадр
                    Value* callee_locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                    memset(callee_locals, 0, callee_frame_size);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if(i < callee_body->header.num_virtual_regs) callee_locals[i] = temp_args[i];
                    }

                    // Обновляем контекст интерпретатора
                    local_func_idx = local_func_idx_to_call;
                    pc = callee_body->code;
                    instructions_ptr = callee_body->code;
                    instructions_end_ptr = callee_body->code + callee_body->code_size;
                    locals = callee_locals;
                    num_virtual_regs = callee_body->header.num_virtual_regs;
                    
                    goto interpreter_loop_start;
                }
                
           op_0x19: { // LDC.I64.IMM Rd(u8), imm64
                    uint8_t rd = READ_U8();
                    int64_t imm64 = READ_I64();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = imm64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.I64.IMM R%d, %" PRId64, rd, imm64);
#endif
                    goto interpreter_loop_start;
                }
                
            op_0x1A: { // LDC.F32.IMM Rd(u8), imm32
                uint8_t rd = READ_U8();
                float immf32 = READ_F32();
                SET_TYPE(locals[rd], ESPB_TYPE_F32);
                V_F32(locals[rd]) = immf32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "LDC.F32.IMM R%u, %f", rd, immf32);
#endif
                goto interpreter_loop_start;
            }
            op_0x1B: { // LDC.F64.IMM Rd(u8), imm64
                uint8_t rd = READ_U8();
                double immf64 = READ_F64();
                SET_TYPE(locals[rd], ESPB_TYPE_F64);
                V_F64(locals[rd]) = immf64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "LDC.F64.IMM R%u, %f", rd, immf64);
#endif
                goto interpreter_loop_start;
            }

           op_0x1C: { // LDC.PTR.IMM Rd(u8), ptr32
                    uint8_t rd = READ_U8();
                    int32_t imm = READ_I32();
                    
                    // Вычисляем целевой адрес
                    uintptr_t target_addr = (uintptr_t)instance->memory_data + imm;
                    uintptr_t mem_base = (uintptr_t)instance->memory_data;
                    uintptr_t mem_end = mem_base + instance->memory_size_bytes;
                    uintptr_t heap_start = mem_base + instance->static_data_end_offset;
                    
                    // Проверяем границы
                    if (target_addr < mem_base || target_addr >= mem_end) {
                        ESP_LOGE(TAG, "LDC.PTR.IMM - pointer outside memory bounds");
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    
                    // Предупреждение если указывает в heap область
                    if (target_addr >= heap_start) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "LDC.PTR.IMM WARNING: pointer %p may conflict with heap area", (void*)target_addr);
#endif
                    }
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                    V_PTR(locals[rd]) = (void*)target_addr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.PTR.IMM R%u = %p (offset %" PRId32 ")", rd, (void*)target_addr, imm);
#endif
                    goto interpreter_loop_start;
                }
                
           op_0x12: { // MOV.I32 / MOV (generic)
                 uint8_t rd = READ_U8();
                 uint8_t rs = READ_U8();

                    DEBUG_CHECK_REGS_2(rd, rs, max_reg_used, "MOV.I32");

                    V_RAW(locals[rd]) = V_RAW(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MOV.I32 R%d, R%d (raw=0x%016" PRIx64 ")", rd, rs, V_RAW(locals[rs]));
#endif
                    goto interpreter_loop_start;
                }
                     op_0x90: { // TRUNC.I64.I32 Rd(u8), Rs(u8)
                   uint8_t rd = READ_U8();
                  uint8_t rs = READ_U8();
                   SET_TYPE(locals[rd], ESPB_TYPE_I32);
                   V_I32(locals[rd]) = (int32_t)V_I64(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I64.I32 R%u, R%u = %" PRId32, rd, rs, V_I32(locals[rd]));
#endif
                   goto interpreter_loop_start;
               }

               op_0xA4: { // FPROUND Rd(u8), Rs(u8) - F64 → F32
                   uint8_t rd = READ_U8();
                   uint8_t rs = READ_U8();
                   DEBUG_CHECK_REG(rs, max_reg_used, "F64_OP");
                   if (!CHECK_TYPE(locals[rs], ESPB_TYPE_F64)) {
                       ESP_LOGE(TAG, "FPROUND - Invalid source R%u (type %d)", rs, -1);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                   }
                   DEBUG_CHECK_REG(rd, max_reg_used, "FPROUND");
                   SET_TYPE(locals[rd], ESPB_TYPE_F32);
                   V_F32(locals[rd]) = (float)V_F64(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "FPROUND R%u, R%u: %f → %f", rd, rs, V_F64(locals[rs]), V_F32(locals[rd]));
#endif
                   goto interpreter_loop_start;
               }

               op_0xA5: { // FPROMOTE Rd(u8), Rs(u8) - F32 → F64
                   uint8_t rd = READ_U8();
                   uint8_t rs = READ_U8();
                   DEBUG_CHECK_REG(rs, max_reg_used, "F32_OP");
                   if (!CHECK_TYPE(locals[rs], ESPB_TYPE_F32)) {
                       ESP_LOGE(TAG, "FPROMOTE - Invalid source R%u (type %d)", rs, -1);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                   }
                   DEBUG_CHECK_REG(rd, max_reg_used, "FPROMOTE");
                   SET_TYPE(locals[rd], ESPB_TYPE_F64);
                   V_F64(locals[rd]) = (double)V_F32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "FPROMOTE R%u, R%u: %f → %f", rd, rs, V_F32(locals[rs]), V_F64(locals[rd]));
#endif
                   goto interpreter_loop_start;
               }
               op_0x13: { // MOV.I64 / MOV (generic)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    V_RAW(locals[rd]) = V_RAW(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MOV.I64 R%u, R%u = (raw=0x%016" PRIx64 ")", rd, rs, V_RAW(locals[rs]));
#endif
                 goto interpreter_loop_start;
            }
            

op_0x20: { // ADD.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = READ_U8();
    uint8_t r1 = READ_U8();
    uint8_t r2 = READ_U8();

    const int32_t val1 = V_I32(locals[r1]);
    const int32_t val2 = V_I32(locals[r2]);

    SET_TYPE(locals[rd], ESPB_TYPE_I32);
    V_I32(locals[rd]) = val1 + val2;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "ADD.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}
                
                op_0x21: { // SUB.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = READ_U8();
    uint8_t r1 = READ_U8();
    uint8_t r2 = READ_U8();

    const int32_t val1 = V_I32(locals[r1]);
    const int32_t val2 = V_I32(locals[r2]);

    SET_TYPE(locals[rd], ESPB_TYPE_I32);
    V_I32(locals[rd]) = val1 - val2;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SUB.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}
                
                op_0x22: { // MUL.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = READ_U8();
    uint8_t r1 = READ_U8();
    uint8_t r2 = READ_U8();

    const int32_t val1 = V_I32(locals[r1]);
    const int32_t val2 = V_I32(locals[r2]);
    const int64_t prod = (int64_t)val1 * val2;
    if (prod > INT32_MAX || prod < INT32_MIN) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    SET_TYPE(locals[rd], ESPB_TYPE_I32);
    V_I32(locals[rd]) = (int32_t)prod;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "MUL.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}

                op_0x23: { // DIV.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = READ_U8();
    uint8_t r1 = READ_U8();
    uint8_t r2 = READ_U8();

    const int32_t dividend = V_I32(locals[r1]);
    const int32_t divisor = V_I32(locals[r2]);
    if (divisor == 0) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
    }
    if (dividend == INT32_MIN && divisor == -1) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    SET_TYPE(locals[rd], ESPB_TYPE_I32);
    V_I32(locals[rd]) = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "DIV.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}

                op_0x24: { // REM.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = READ_U8();
    uint8_t r1 = READ_U8();
    uint8_t r2 = READ_U8();

    const int32_t dividend_r = V_I32(locals[r1]);
    const int32_t divisor_r = V_I32(locals[r2]);
    if (divisor_r == 0) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
    }
    if (dividend_r == INT32_MIN && divisor_r == -1) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    SET_TYPE(locals[rd], ESPB_TYPE_I32);
    V_I32(locals[rd]) = dividend_r % divisor_r;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "REM.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}

                op_0x26: { // DIV.U32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint32_t dividend_u = V_I32(locals[r1]);
                    uint32_t divisor_u = V_I32(locals[r2]);
                    if (divisor_u == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_U32);
                    V_I32(locals[rd]) = dividend_u / divisor_u;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.U32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                // I32 arithmetic with 8-bit immediate (per spec v1.7 0x40..0x48)
                op_0x40: { // ADD.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) + (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x41: { // SUB.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) - (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x42: { // MUL.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    int64_t prod = (int64_t)V_I32(locals[r1]) * (int64_t)imm;
                    if (prod > INT32_MAX || prod < INT32_MIN) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)prod;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x43: { // DIVS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    int32_t dividend = V_I32(locals[r1]);
                    int32_t divisor = (int32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT32_MIN && divisor == -1) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIVS.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x44: { // DIVU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8(); // treat as unsigned
                    uint32_t dividend = (uint32_t)V_I32(locals[r1]);
                    uint32_t divisor = (uint32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)(dividend / divisor);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIVU.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x45: { // REMS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    int32_t dividend = V_I32(locals[r1]);
                    int32_t divisor = (int32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT32_MIN && divisor == -1) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = dividend % divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REMS.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x46: { // REMU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint32_t dividend = (uint32_t)V_I32(locals[r1]);
                    uint32_t divisor = (uint32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)(dividend % divisor);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REMU.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x47: { // SHRS.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint32_t shift = (uint32_t)imm & 31u;
                    int32_t sval = V_I32(locals[r1]);
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = sval >> shift;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHRS.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x48: { // SHRU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint32_t shift = (uint32_t)imm & 31u;
                    uint32_t uval = (uint32_t)V_I32(locals[r1]);
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)(uval >> shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHRU.I32.IMM8 R%u, R%u, %u = %" PRIu32, rd, r1, (unsigned)imm, (uint32_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x49: { // AND.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) & (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "AND.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x4A: { // OR.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) | (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "OR.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x4B: { // XOR.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) ^ (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "XOR.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x60: { // ADD.F32 Rd, R1, R2
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = V_F32(locals[r1]) + V_F32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x61: { // SUB.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8(); uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = V_F32(locals[r1]) - V_F32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x62: { // MUL.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8(); uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = V_F32(locals[r1]) * V_F32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x63: { // DIV.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8(); uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = V_F32(locals[r1]) / V_F32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x64: { // MIN.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8(); uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = fminf(V_F32(locals[r1]), V_F32(locals[r2]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MIN.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x65: { // MAX.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8(); uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = fmaxf(V_F32(locals[r1]), V_F32(locals[r2]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MAX.F32 R%u, R%u, R%u = %f", rd, r1, r2, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x66: { // ABS.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = fabsf(V_F32(locals[r1]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ABS.F32 R%u, R%u = %f", rd, r1, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x67: { // SQRT.F32
                    uint8_t rd = READ_U8(); uint8_t r1 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = sqrtf(V_F32(locals[r1]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SQRT.F32 R%u, R%u = %f", rd, r1, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                // и для double:
                op_0x68: { // ADD.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = V_F64(locals[r1]) + V_F64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x69: { // SUB.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = V_F64(locals[r1]) - V_F64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6A: { // MUL.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = V_F64(locals[r1]) * V_F64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6B: { // DIV.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = V_F64(locals[r1]) / V_F64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6C: { // MIN.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = fmin(V_F64(locals[r1]), V_F64(locals[r2]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MIN.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6D: { // MAX.F64
                    uint8_t rd=READ_U8(), r1=READ_U8(), r2=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = fmax(V_F64(locals[r1]), V_F64(locals[r2]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MAX.F64 R%u, R%u, R%u = %f", rd, r1, r2, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6E: { // ABS.F64
                    uint8_t rd=READ_U8(), r1=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = fabs(V_F64(locals[r1]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ABS.F64 R%u, R%u = %f", rd, r1, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x6F: { // SQRT.F64
                    uint8_t rd=READ_U8(), r1=READ_U8();
                    SET_TYPE(locals[rd],ESPB_TYPE_F64);
                    V_F64(locals[rd]) = sqrt(V_F64(locals[r1]));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SQRT.F64 R%u, R%u = %f", rd, r1, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0xBC: { // PTRTOINT Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    DEBUG_CHECK_REG(rs, max_reg_used, "PTR_OP");
                    if (!CHECK_TYPE(locals[rs], ESPB_TYPE_PTR)) {
                        // Error handling as before
                        ESP_LOGE(TAG, "PTRTOINT - Invalid source R%u. PC_offset: %ld",
                                rs, (long)((pc - 3) - instructions_ptr));
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                    }
                    DEBUG_CHECK_REG(rd, max_reg_used, "PTRTOINT");
                    
                    void* ptr_value = V_PTR(locals[rs]);
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    
                    // Проверяем, указывает ли этот указатель на ESPB-функцию
                    if (exec_ctx->feature_callback_auto_active) {
                        uint8_t *base_ptr = instance->memory_data;
                        uint8_t *value_ptr = (uint8_t*)ptr_value;
                        
                        // Проверяем, находится ли указатель в диапазоне памяти ESPB-модуля
                        if (value_ptr >= base_ptr && value_ptr < base_ptr + instance->memory_size_bytes) {
                            // Если указатель в пределах ESPB-памяти, это возможно ESPB-функция
                            // uint32_t offset = (uint32_t)(value_ptr - base_ptr); // Unused variable
                            bool found_function = false;
                            
                            // Проверяем, указывает ли этот адрес на начало какой-либо ESPB-функции
                            for (uint32_t i = 0; i < module->num_functions; i++) {
                                const EspbFunctionBody *func = &module->function_bodies[i];
                                const uint8_t *func_start = func->code;
                                
                                if (value_ptr == func_start) {
                                    // Нашли точное совпадение с началом функции! Устанавливаем CALLBACK_FLAG_BIT
                                    V_I32(locals[rd]) = (int32_t)i | CALLBACK_FLAG_BIT;
                                    ESP_LOGD(TAG, "PTRTOINT R%d, R%d (exact ESPB func %" PRIu32 ") -> val=0x%08" PRIx32 " (with CALLBACK_FLAG_BIT)",
                                           rd, rs, i, (uint32_t)V_I32(locals[rd]));
                                    found_function = true;
                                    break;
                                }
                                
                                // Также проверим, попадает ли указатель в тело функции
                                const uint8_t *func_end = func_start + func->code_size;
                                if (value_ptr >= func_start && value_ptr < func_end) {
                                    // Указатель внутри тела функции, это тоже может быть колбэк
                                    V_I32(locals[rd]) = (int32_t)i | CALLBACK_FLAG_BIT;
                                    ESP_LOGD(TAG, "PTRTOINT R%d, R%d (inside ESPB func %" PRIu32 ") -> val=0x%08" PRIx32 " (with CALLBACK_FLAG_BIT)",
                                           rd, rs, i, (uint32_t)V_I32(locals[rd]));
                                    found_function = true;
                                    break;
                                }
                            }
                            
                            if (!found_function) {
                                // Если не нашли функцию, то это просто указатель в памяти ESPB
                                V_I32(locals[rd]) = (int32_t)(uintptr_t)ptr_value;
                                ESP_LOGD(TAG, "PTRTOINT R%d, R%d (mem addr in ESPB) -> val=0x%08" PRIx32,
                                       rd, rs, (uint32_t)V_I32(locals[rd]));
                            }
                        } else {
                            // Это указатель вне ESPB-памяти, просто преобразуем
                            V_I32(locals[rd]) = (int32_t)(uintptr_t)ptr_value;
                           ESP_LOGD(TAG, "PTRTOINT R%d, R%d (external ptr) -> val=0x%08" PRIx32, rd, rs, (uint32_t)V_I32(locals[rd]));
                        }
                    } else {
                        // Если FEATURE_CALLBACK_AUTO не активен, просто выполняем обычное преобразование
                        V_I32(locals[rd]) = (int32_t)(uintptr_t)ptr_value;
                       ESP_LOGD(TAG, "PTRTOINT R%d, R%d (val=0x%08" PRIx32 ")", rd, rs, (uint32_t)V_I32(locals[rd]));
                    }
                    goto interpreter_loop_start;
                }

                op_0xBD: { // INTTOPTR Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    DEBUG_CHECK_REGS_2(rd, rs, max_reg_used, "INTTOPTR");
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                    V_PTR(locals[rd]) = (void*)(uintptr_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "INTTOPTR R%u, R%u -> %p", rd, rs, V_PTR(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x27: { // REM.U32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint32_t dividend_u = V_I32(locals[r1]);
                    uint32_t divisor_u = V_I32(locals[r2]);
                    if (divisor_u == 0) {
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_U32);
                    V_I32(locals[rd]) = dividend_u % divisor_u;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REM.U32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x28: { // AND.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) & V_I32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "AND.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
                goto interpreter_loop_start;
            }

            op_0x29: { // OR.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) | V_I32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "OR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

            op_0x2A: { // XOR.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = V_I32(locals[r1]) ^ V_I32(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "XOR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

            op_0x2B: { // SHL.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint32_t val1 = (uint32_t)V_I32(locals[r1]);
                    uint32_t shift = V_I32(locals[r2]) & 31;
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)(val1 << shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SHL.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

            op_0x2C: { // SHR.I32 Rd(u8), R1(u8), R2(u8) (arithmetic)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    int32_t sval = V_I32(locals[r1]);
                    uint32_t shift_a = V_I32(locals[r2]) & 31;
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = sval >> shift_a;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SHR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

            op_0x2D: { // USHR.I32 Rd(u8), R1(u8), R2(u8) (logical)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint32_t uval = V_I32(locals[r1]);
                    uint32_t shift = V_I32(locals[r2]) & 31;
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)(uval >> shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "USHR.I32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, (uint32_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x2E: { // NOT.I32 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = ~V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "NOT.I32 R%d, R%d = %" PRId32, rd, rs, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x30: { // ADD.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) + V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x31: { // SUB.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) - V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x32: { // MUL.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) * V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x33: { // DIV.I64 Rd(u8), R1(u8), R2(u8) (signed)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    int64_t dividend = V_I64(locals[r1]);
                    int64_t divisor = V_I64(locals[r2]);
                    if (divisor == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    if (dividend == INT64_MIN && divisor == -1) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "DIV.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x36: { // DIV.U64 Rd(u8), R1(u8), R2(u8) (unsigned)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint64_t udividend = (uint64_t)V_I64(locals[r1]);
                    uint64_t udivisor = (uint64_t)V_I64(locals[r2]);
                    if (udivisor == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = udividend / udivisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "DIVU.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, (uint64_t)V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x34: { // REM.I64 Rd(u8), R1(u8), R2(u8) (signed)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    int64_t dividend_r = V_I64(locals[r1]);
                    int64_t divisor_r = V_I64(locals[r2]);
                    if (divisor_r == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    if (dividend_r == INT64_MIN && divisor_r == -1) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = dividend_r % divisor_r;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "REM.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x37: { // REM.U64 Rd(u8), R1(u8), R2(u8) (unsigned)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    uint64_t udividend_r = (uint64_t)V_I64(locals[r1]);
                    uint64_t udivisor_r = (uint64_t)V_I64(locals[r2]);
                    if (udivisor_r == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = udividend_r % udivisor_r;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "REMU.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, (uint64_t)V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x38: { // AND.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) & V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "AND.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x39: { // OR.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) | V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "OR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x3A: { // XOR.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) ^ V_I64(locals[r2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "XOR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x3B: { // SHL.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) << (V_I32(locals[r2]) & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHL.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x3C: { // SHR.I64 Rd(u8), R1(u8), R2(u8) (arithmetic)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) >> (V_I32(locals[r2]) & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x3D: { // USHR.I64 Rd(u8), R1(u8), R2(u8) (logical)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = (uint64_t)V_I64(locals[r1]) >> (V_I32(locals[r2]) & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "USHR.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, (uint64_t)V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x3E: { // NOT.I64 Rd(u8), R1(u8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = ~V_I64(locals[r1]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "NOT.I64 R%d, R%d = %" PRId64, rd, r1, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }

                op_0x70: { // STORE.I8 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    int8_t val_i8 = (int8_t)V_I32(locals[rs]);
                    
                    if (ra_addr >= base_addr && ra_addr < base_addr + mem_size) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                        int64_t tgt = (int64_t)ra_off + offset;
                        if (tgt < 0 || (uint64_t)tgt + sizeof(int8_t) > mem_size) {
                            ESP_LOGE(TAG, "STORE.I8 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_off, (unsigned int)offset);
                            return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target = (uint32_t)tgt;
                        *((int8_t*)(base + target)) = val_i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I8 R%hhu(%hhd) -> mem[%" PRIu32 "]", rs, val_i8, target);
#endif
                    } else {
                        // Ra holds an absolute native address (e.g., from ALLOCA heap allocation).
                        uintptr_t target_addr = ra_addr + offset;
                        *((int8_t*)target_addr) = val_i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I8 R%hhu(%hhd) -> abs[%p]", rs, val_i8, (void*)target_addr);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0x71: { // STORE.U8 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    uint8_t val8 = (uint8_t)(V_I32(locals[rs]) & 0xFF);
                    
                    if (ra_addr >= base_addr && ra_addr < base_addr + mem_size) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                        int64_t tgt = (int64_t)ra_off + offset;
                        if (tgt < 0 || (uint64_t)tgt + sizeof(uint8_t) > mem_size) {
                            ESP_LOGE(TAG, "STORE.U8 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_off, (unsigned int)offset);
                            return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target = (uint32_t)tgt;
                        *((uint8_t *)(base + target)) = val8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.U8 R%u(%u) -> mem[%u]", (unsigned)rs, (unsigned)val8, (unsigned)target);
#endif
                    } else {
                        // Ra holds an absolute native address (e.g., from ALLOCA heap allocation).
                        uintptr_t target_addr = ra_addr + offset;
                        *((uint8_t*)target_addr) = val8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.U8 R%u(%u) -> abs[%p]", (unsigned)rs, (unsigned)val8, (void*)target_addr);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0x72: { // STORE.I16 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    int16_t val_i16 = (int16_t)V_I32(locals[rs]);
                    
                    if (ra_addr >= base_addr && ra_addr < base_addr + mem_size) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                        int64_t tgt = (int64_t)ra_off + offset;
                        if (tgt < 0 || (uint64_t)tgt + sizeof(int16_t) > mem_size) {
                            ESP_LOGE(TAG, "STORE.I16 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_off, (unsigned int)offset);
                            return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target = (uint32_t)tgt;
                        memcpy(base + target, &val_i16, sizeof(int16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I16 R%hhu(%hd) -> mem[%" PRIu32 "]", rs, val_i16, target);
#endif
                    } else {
                        // Ra holds an absolute native address (e.g., from ALLOCA heap allocation).
                        uintptr_t target_addr = ra_addr + offset;
                        memcpy((void*)target_addr, &val_i16, sizeof(int16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I16 R%hhu(%hd) -> abs[%p]", rs, val_i16, (void*)target_addr);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0x73: { // STORE.U16 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base16 = instance->memory_data;
                    uint32_t mem_size16 = instance->memory_size_bytes;
                    uintptr_t ra_addr16 = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr16 = (uintptr_t)base16;
                    uint16_t val16 = (uint16_t)V_I32(locals[rs]);
                    
                    if (ra_addr16 >= base_addr16 && ra_addr16 < base_addr16 + mem_size16) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_off16 = (uint32_t)(ra_addr16 - base_addr16);
                        int64_t tgt16 = (int64_t)ra_off16 + offset;
                        if (tgt16 < 0 || (uint64_t)tgt16 + sizeof(uint16_t) > mem_size16) {
                            ESP_LOGE(TAG, "STORE.U16 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_off16, (unsigned int)offset);
                            return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target16 = (uint32_t)tgt16;
                        memcpy(base16 + target16, &val16, sizeof(uint16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.U16 R%u(%u) -> mem[%u]", (unsigned)rs, (unsigned)val16, (unsigned)target16);
#endif
                    } else {
                        // Ra holds an absolute native address (e.g., from ALLOCA heap allocation).
                        uintptr_t target_addr = ra_addr16 + offset;
                        memcpy((void*)target_addr, &val16, sizeof(uint16_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.U16 R%u(%u) -> abs[%p]", (unsigned)rs, (unsigned)val16, (void*)target_addr);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0x74: { // STORE.I32 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    int32_t val32 = V_I32(locals[rs]);
                    
                    if (ra_addr >= base_addr && ra_addr < base_addr + mem_size) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                        int64_t target_signed = (int64_t)ra_offset + offset;
                        if (target_signed < 0 || (uint64_t)target_signed + sizeof(uint32_t) > mem_size) {
                            ESP_LOGE(TAG, "STORE.I32 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_offset, (unsigned int)offset);
                            return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target = (uint32_t)target_signed;
                        memcpy(base + target, &val32, sizeof(int32_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I32 R%u(%ld) -> mem[%" PRIu32 "]", rs, (long)val32, target);
#endif
                    } else {
                        // Ra holds an absolute native address (e.g., from ALLOCA heap allocation).
                        uintptr_t target_addr = ra_addr + offset;
                        memcpy((void*)target_addr, &val32, sizeof(int32_t));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "STORE.I32 R%u(%ld) -> abs[%p]", rs, (long)val32, (void*)target_addr);
#endif
                    }
                    goto interpreter_loop_start;
                }
                // op_0x75 has been removed and is now unhandled
                op_0x7A: { // STORE.PTR Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) {
                       ESP_LOGE(TAG, "STORE.PTR ra_addr<base_addr: ra_addr=%p base_addr=%p", (void*)ra_addr, (void*)base_addr);
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(void*) > mem_size) {
                       ESP_LOGE(TAG, "STORE.PTR OOB: ra_offset=%" PRIu32 " offset=%" PRId16 " sizeof(void*)=%zu mem_size=%" PRIu32, ra_offset, offset, sizeof(void*), mem_size);
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    uint32_t target = (uint32_t)target_signed;
                    // Use memcpy to handle potentially unaligned access safely
                    void* ptr_val = V_PTR(locals[rs]);
                    memcpy(base + target, &ptr_val, sizeof(void*));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.PTR R%u(%p) -> mem[%u]", (unsigned)rs, ptr_val, (unsigned)target);
#endif
                    goto interpreter_loop_start;
                }

                // Добавляем поддержку 8-битного булевого типа
                op_0x7B: { // STORE.BOOL Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs_bool = READ_U8();
                    uint8_t ra_bool = READ_U8();
                    int16_t offset_bool = READ_I16();
                    uint8_t *base_bool = instance->memory_data;
                    uint32_t mem_size_bool = instance->memory_size_bytes;
                    uintptr_t ra_addr_bool = (uintptr_t)V_PTR(locals[ra_bool]);
                    uintptr_t base_addr_bool = (uintptr_t)base_bool;
                    if (ra_addr_bool < base_addr_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset_bool = (uint32_t)(ra_addr_bool - base_addr_bool);
                    int64_t target_signed_bool = (int64_t)ra_offset_bool + offset_bool;
                    if (target_signed_bool < 0 || (uint64_t)target_signed_bool + sizeof(uint8_t) > mem_size_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target_bool = (uint32_t)target_signed_bool;
                    uint8_t bool_val = (V_I32(locals[rs_bool]) != 0) ? 1 : 0;
                    *((uint8_t *)(base_bool + target_bool)) = bool_val;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.BOOL [R%u + %d] <- R%u(%u)", ra_bool, offset_bool, rs_bool, (unsigned)bool_val);
#endif
                    goto interpreter_loop_start;
                }

                op_0x76: { // STORE.I64 Rs(u8), Ra(u8), offset(i16)
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(int64_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)target_signed;
                    
                    // ДИАГНОСТИКА: Выводим информацию о выравнивании
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "STORE.I64 ALIGNMENT CHECK: Ra=R%u, ptr=%p, offset=%d, target_addr=0x%x, alignment_check=%u",
                             ra, V_PTR(locals[ra]), offset, target, target % sizeof(int64_t));
#endif
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (target % sizeof(int64_t) != 0) {
                        ESP_LOGD(TAG, "STORE.I64 UNALIGNED ACCESS: Ra=R%u contains ptr=%p, offset=%d, final_addr=0x%x (mod 8 = %u) - using unaligned write",
                                ra, V_PTR(locals[ra]), offset, target, target % 8);
                        // Используем unaligned доступ вместо ошибки
                    }
#endif
                    int64_t val64 = V_I64(locals[rs]);
                    *((int64_t *)(base + target)) = val64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.I64 R%u(%" PRId64 ") -> mem[%" PRIu32 "]", rs, val64, target);
#endif
                    goto interpreter_loop_start;
                }
                // op_0x77 (STORE.U64) is now handled by op_0x76 (STORE.I64)
                op_0x78: { // STORE.F32 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rs=READ_U8(), ra=READ_U8();
                    int16_t off = READ_I16();
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)V_PTR(locals[ra]);
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(float)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    *(float*)(base+tgt) = V_F32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"STORE.F32 R%u(%f)->mem[%u]", rs, V_F32(locals[rs]), tgt);
#endif
                    goto interpreter_loop_start;
                }
                op_0x79: { // STORE.F64 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rs=READ_U8(), ra=READ_U8();
                    int16_t off = READ_I16();
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)V_PTR(locals[ra]);
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(double)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    *(double*)(base+tgt) = V_F64(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"STORE.F64 R%u(%f)->mem[%u]", rs, V_F64(locals[rs]), tgt);
#endif
                    goto interpreter_loop_start;
                }
                op_0x85: { // LOAD.I64 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt64 = (int64_t)ra_off + offset;
                    if (tgt64 < 0 || (uint64_t)tgt64 + sizeof(int64_t) > mem_size) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt64;
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (target % sizeof(int64_t) != 0) {
                        ESP_LOGD(TAG, "LOAD.I64 UNALIGNED ACCESS: Ra=R%u contains ptr=%p, offset=%d, final_addr=0x%x (mod 8 = %u) - using unaligned read",
                                ra, V_PTR(locals[ra]), offset, target, target % 8);
                    }
#endif
                    int64_t loadv;
                    memcpy(&loadv, base + target, sizeof(int64_t));
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = loadv;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I64 R%u <- mem[%" PRIu32 "] = %" PRId64, rd, target, loadv);
#endif
                    goto interpreter_loop_start;
                }
                op_0x86: { // LOAD.F32 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd=READ_U8(), ra=READ_U8();
                    int16_t off = READ_I16();
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)V_PTR(locals[ra]);
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(float)>sz)){ return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    memcpy(&V_F32(locals[rd]), base + tgt, sizeof(float));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"LOAD.F32 R%u<-mem[%u]=%f", rd, tgt, V_F32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x87: { // LOAD.F64 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd=READ_U8(), ra=READ_U8();
                    int16_t off = READ_I16();
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)V_PTR(locals[ra]);
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(double)>sz)){ return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    SET_TYPE(locals[rd], ESPB_TYPE_F64);
                    memcpy(&V_F64(locals[rd]), base + tgt, sizeof(double));
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"LOAD.F64 R%u<-mem[%u]=%f", rd, tgt, V_F64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x88: { // LOAD.PTR Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt_ptr = (int64_t)ra_off + offset;
                    if (tgt_ptr < 0 || (uint64_t)tgt_ptr + sizeof(void*) > mem_size) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target_ptr = (uint32_t)tgt_ptr;
                    // Use memcpy to handle potentially unaligned access safely (no alignment check needed)
                    void* loaded_ptr;
                    memcpy(&loaded_ptr, base + target_ptr, sizeof(void*));
                    SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                    V_PTR(locals[rd]) = loaded_ptr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.PTR R%u <- mem[%" PRIu32 "] = %p", rd, target_ptr, loaded_ptr);
#endif
                    goto interpreter_loop_start;
                }
                
                op_0x89: { // LOAD.BOOL Rd(u8), Ra(u8), offset(i16) - loads and normalizes to 0 or 1
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(uint8_t) > mem_size) { return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    uint8_t raw_val = *(base + target);
                    // Normalize: any non-zero value becomes 1
                    int32_t bool_val = (raw_val != 0) ? 1 : 0;
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // BOOL is stored as I32
                    V_I32(locals[rd]) = bool_val;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.BOOL R%u <- mem[%" PRIu32 "] = %d (raw: %u)", rd, target, bool_val, raw_val);
#endif
                    goto interpreter_loop_start;
                }
                    

                op_0x0F: { // END
                    // 1. Save return value from current frame's R0
                    Value return_val = {0};
                    uint32_t callee_sig_idx = module->function_signature_indices[local_func_idx];
                    const EspbFuncSignature* callee_sig = &module->signatures[callee_sig_idx];
                    if (callee_sig->num_returns > 0 && num_virtual_regs > 0) {
                        return_val = locals[0];
                    }

                    // 2. Free ALLOCA allocations for the current frame
                    if (exec_ctx->call_stack_top > 0) {
                        // The frame to be cleaned is the one we are about to pop.
                        RuntimeFrame *frame = &exec_ctx->call_stack[exec_ctx->call_stack_top - 1];
                        if (frame->alloca_count > 0) {
                            for (uint8_t i = 0; i < frame->alloca_count; i++) {
                                if (frame->alloca_ptrs[i]) {
                                    espb_heap_free(instance, frame->alloca_ptrs[i]);
                                    frame->alloca_ptrs[i] = NULL;
                                }
                            }
                            frame->alloca_count = 0;
                            frame->has_custom_aligned = false;
                        }
                    }

                    // 3. Pop the call frame to get caller's info
                    int restored_pc;
                    size_t restored_fp;
                    uint32_t restored_caller_idx;
                    Value* saved_frame_ptr = NULL;
                    size_t num_regs_saved = 0;

                    if (pop_call_frame(exec_ctx, &restored_pc, &restored_fp, &restored_caller_idx, &saved_frame_ptr, &num_regs_saved) != ESPB_OK) {
                        return ESPB_ERR_STACK_UNDERFLOW;
                    }

                    // 4. Check if that was the last frame (exit execution)
                    if (restored_pc == -1 || exec_ctx->call_stack_top == 0) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "END: Popped last frame. Exiting execution.");
#endif
                        end_reached = true;
                        if (callee_sig->num_returns > 0 && num_virtual_regs > 0) {
                            locals[0] = return_val;
                        }
                        goto function_epilogue;
                    }

                    // 5. Restore caller's full register frame if it was saved on the stack
                    if (saved_frame_ptr != NULL && num_regs_saved > 0) { // Check if there's anything to restore
                        Value* caller_locals_ptr = (Value*)(exec_ctx->shadow_stack_buffer + restored_fp);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "END (Slow Path): Restoring %zu registers from shadow stack at %p to caller frame %p", num_regs_saved, saved_frame_ptr, caller_locals_ptr);
#endif
                        
                        if (num_regs_saved == module->function_bodies[restored_caller_idx].header.num_virtual_regs) {
                            memcpy(caller_locals_ptr, saved_frame_ptr, num_regs_saved * sizeof(Value));
                        } else {
                            ESP_LOGW(TAG, "END: Mismatch in saved regs (%zu) vs caller regs (%u). Not restoring frame.", num_regs_saved, module->function_bodies[restored_caller_idx].header.num_virtual_regs);
                        }
                    } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "END (Fast Path): Skipped register restore for leaf function call.");
#endif
                    }

                    // 6. Unwind the stack and restore context
                    local_func_idx = restored_caller_idx;
                    const EspbFunctionBody* caller_body = &module->function_bodies[local_func_idx];
                    num_virtual_regs = caller_body->header.num_virtual_regs;
                    
                    exec_ctx->fp = restored_fp;
                    exec_ctx->sp = exec_ctx->fp + (num_virtual_regs * sizeof(Value)); // Reset SP to the top of the restored caller's frame

                    instructions_ptr = caller_body->code;
                    instructions_end_ptr = instructions_ptr + caller_body->code_size;
                    pc = instructions_ptr + restored_pc;
                    locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);

                    // 7. Copy return value to R0 of the (now restored) caller's frame
                    if (callee_sig->num_returns > 0 && num_virtual_regs > 0) {
                        locals[0] = return_val;
                    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "END: Returned to function %" PRIu32 ". pc_offset=%d, fp=%zu, sp=%zu", local_func_idx, restored_pc, exec_ctx->fp, exec_ctx->sp);
#endif
                    goto interpreter_loop_start;
                }
                // --- Поддержка i8 ---
                op_0x10: { // MOV.I8 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I8);
                    V_I32(locals[rd]) = V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "MOV.I8 R%u, R%u = %" PRId8, (unsigned)rd, (unsigned)rs, (int8_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x80: { // LOAD.I8S Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(int8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    int8_t val_i8 = *((int8_t*)(base + target));
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // Promote to I32
                    V_I32(locals[rd]) = val_i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I8S R%u <- mem[%" PRIu32 "] = %" PRId8, (unsigned)rd, target, val_i8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x81: { // LOAD.I8U Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(uint8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    uint8_t val_u8 = *((uint8_t*)(base + target));
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // Promote to I32
                    V_I32(locals[rd]) = val_u8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.I8U R%u <- mem[%" PRIu32 "] = %" PRIu8, rd, target, val_u8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x82: { // LOAD.I16S Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) {
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(int16_t) > mem_size) {
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    // Use memcpy to handle potentially unaligned access safely (packed structs)
                    int16_t val_i16;
                    memcpy(&val_i16, base + target, sizeof(int16_t));
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // Promote to I32
                    V_I32(locals[rd]) = val_i16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.I16S R%u <- mem[%" PRIu32 "] = %" PRId16, rd, target, val_i16);
#endif
                    goto interpreter_loop_start;
                }

                op_0x92: { // TRUNC.I64.I8 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // store truncated byte in 32-bit slot
                    V_I32(locals[rd]) = (int8_t)V_I64(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "TRUNC.I64.I8 R%u, R%u = %" PRId8, (unsigned)rd, (unsigned)rs, (int8_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x94: { // TRUNC.I32.I8 (alias) Rd(u8), Rs(u8)
                    uint8_t rd94 = READ_U8();
                    uint8_t rs94 = READ_U8();
                    SET_TYPE(locals[rd94], ESPB_TYPE_I8);
                    V_I32(locals[rd94]) = (int8_t)V_I32(locals[rs94]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I32.I8 R%u, R%u = %" PRId8, (unsigned)rd94, (unsigned)rs94, (int8_t)V_I32(locals[rd94]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x95: { // TRUNC.I16.I8 Rd(u8), Rs(u8)
                    uint8_t rd95 = READ_U8();
                    uint8_t rs95 = READ_U8();
                    SET_TYPE(locals[rd95], ESPB_TYPE_I8);
                    V_I32(locals[rd95]) = (int8_t)(int16_t)V_I32(locals[rs95]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I16.I8 R%u, R%u = %" PRId8, (unsigned)rd95, (unsigned)rs95, (int8_t)V_I32(locals[rd95]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x96: { // ZEXT.I8.I16 Rd(u8), Rs(u8) - Zero-extend I8 to I16
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I16);
                    V_I32(locals[rd]) = (uint8_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "ZEXT.I8.I16 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x97: { // ZEXT.I8.I32 Rd(u8), Rs(u8) - Zero-extend I8 to I32
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (uint8_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "ZEXT.I8.I32 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x9E: { // SEXT.I8.I64 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (int64_t)(int8_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I8.I64 R%u, R%u = %" PRId64, (unsigned)rd, (unsigned)rs, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x98: { // ZEXT.I8.I64 Rd(u8), Rs(u8) - Zero-extend I8 to I64
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (uint64_t)(V_I32(locals[rs]) & 0xFFu);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "ZEXT.I8.I64 R%u, R%u = %" PRId64, (unsigned)rd, (unsigned)rs, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x99: { // ZEXT.I16.I32 Rd(u8), Rs(u8) - Zero-extend I16 to I32
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (uint16_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "ZEXT.I16.I32 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x11: { // MOV.I16 - Now generic MOV
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    V_RAW(locals[rd]) = V_RAW(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "MOV.I16 R%d, R%d = (raw=0x%016" PRIx64 ")", rd, rs, V_RAW(locals[rs]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x83: { // LOAD.U16 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t off2 = READ_I16();
                    uint8_t *b2 = instance->memory_data;
                    uint32_t ms2 = instance->memory_size_bytes;
                    uintptr_t addr2 = (uintptr_t)V_PTR(locals[ra]);
                    if (addr2 < (uintptr_t)b2) {
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t off_base = (uint32_t)(addr2 - (uintptr_t)b2);
                    int64_t t2 = (int64_t)off_base + off2;
                    if (t2 < 0 || (uint64_t)t2 + sizeof(uint16_t) > ms2) {
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t tgt2 = (uint32_t)t2;
                    // Use memcpy to handle potentially unaligned access safely (packed structs)
                    uint16_t lval;
                    memcpy(&lval, b2 + tgt2, sizeof(uint16_t));
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // Promote to I32
                    V_I32(locals[rd]) = lval;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.U16 R%u <- mem[%" PRIu32 "] = %" PRIu16, rd, tgt2, lval);
#endif
                    goto interpreter_loop_start;
                }
                op_0x93: { // TRUNC.I32.I16 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs2 = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32); // The value is truncated, but we store it in a 32 bit slot
                    V_I32(locals[rd]) = (int16_t)V_I32(locals[rs2]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I32.I16 R%d, R%d = %" PRId16, rd, rs2, (int16_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x9D: { // SEXT.I8.I32 Rd(u8), Rs(u8)
                    uint8_t rd2 = READ_U8();
                    uint8_t rs3 = READ_U8();
                    SET_TYPE(locals[rd2], ESPB_TYPE_I32);
                    V_I32(locals[rd2]) = (int8_t)V_I32(locals[rs3]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I8.I32 R%d, R%d = %" PRId32, rd2, rs3, V_I32(locals[rd2]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x9F: { // SEXT.I16.I32 Rd(u8), Rs(u8)
                    uint8_t rd2 = READ_U8();
                    uint8_t rs3 = READ_U8();
                    SET_TYPE(locals[rd2], ESPB_TYPE_I32);
                    V_I32(locals[rd2]) = (int16_t)V_I32(locals[rs3]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I16.I32 R%d, R%d = %" PRId32, rd2, rs3, V_I32(locals[rd2]));
#endif
                    goto interpreter_loop_start;
                }
                op_0xA0: { // SEXT.I16.I64 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (int64_t)(int16_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I16.I64 R%u, R%u = %" PRId64, (unsigned)rd, (unsigned)rs, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x9B: { // ZEXT.I32.I64 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = (uint64_t)(uint32_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ZEXT.I32.I64 R%d, R%d = %" PRIu64, rd, rs, (uint64_t)V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0x9C: { // SEXT.I8.I16 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();

                    SET_TYPE(locals[rd], ESPB_TYPE_I16);
                    V_I32(locals[rd]) = (int16_t)(int8_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SEXT.I8.I16 R%d, R%d = %" PRId16, rd, rs, (int16_t)V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0xA1: { // SEXT.I32.I64 Rd(u8), Rs(u8)
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (int64_t)V_I32(locals[rs]);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SEXT.I32.I64 R%d, R%d = %" PRId64, rd, rs, V_I64(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                op_0xA6: { // CVT.F32.U32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_U32);
                    V_I32(locals[rd]) = (uint32_t)V_F32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xA7: { // CVT.F32.U64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = (uint64_t)V_F32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xA8: { // CVT.F64.U32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_U32);
                    V_I32(locals[rd]) = (uint32_t)V_F64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xA9: { // CVT.F64.U64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = (uint64_t)V_F64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAA: { // CVT.F32.I32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)V_F32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAB: { // CVT.F32.I64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (int64_t)V_F32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAC: { // CVT.F64.I32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = (int32_t)V_F64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAD: { // CVT.F64.I64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = (int64_t)V_F64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAE: { // CVT.U32.F32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = (float)V_I32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xAF: { // CVT.U32.F64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F64);
                    V_F64(locals[rd]) = (double)V_I32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB0: { // CVT.U64.F32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = (float)V_I64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB1: { // CVT.U64.F64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F64);
                    V_F64(locals[rd]) = (double)V_I64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB2: { // CVT.I32.F32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = (float)V_I32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB3: { // CVT.I32.F64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F64);
                    V_F64(locals[rd]) = (double)V_I32(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB4: { // CVT.I64.F32
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F32);
                    V_F32(locals[rd]) = (float)V_I64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0xB5: { // CVT.I64.F64
                    uint8_t rd = READ_U8(); uint8_t rs = READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_F64);
                    V_F64(locals[rd]) = (double)V_I64(locals[rs]);
                    goto interpreter_loop_start;
                }
                op_0x01: { // NOP: Если встречается отдельно (не после END)
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "NOP");
#endif
                    goto interpreter_loop_start;
                }
            op_0x00: { // padding/выравнивающий NOP
                    goto interpreter_loop_start;
                }
                op_0x1D: { // LD_GLOBAL_ADDR Rd(u8), symbol_idx(u16)
                    uint8_t rd = READ_U8();
                    uint16_t symbol_idx = READ_U16();
                    uintptr_t addr = 0;
                    bool found_in_func_map = false;

                    // Function pointer flag: high bit set
                    if (symbol_idx & 0x8000) {
                        uint32_t func_idx = (uint32_t)(symbol_idx & 0x7FFF);
                        if (module->func_ptr_map_by_index && func_idx < module->func_ptr_map_by_index_size) {
                            uint32_t data_offset = module->func_ptr_map_by_index[func_idx];
                            if (data_offset != UINT32_MAX) {
                                if (!instance->memory_data) {
                                    ESP_LOGE(TAG, "LD_GLOBAL_ADDR - instance->memory_data is NULL for func_ptr_map idx=%hu", symbol_idx);
                                    return ESPB_ERR_INSTANTIATION_FAILED;
                                }
                                addr = (uintptr_t)(instance->memory_data + data_offset);
                                DEBUG_CHECK_REG(rd, max_reg_used, "LD_GLOBAL_ADDR");
                                SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                                V_PTR(locals[rd]) = (void*)addr;
                                goto interpreter_loop_start;
                            }
                        }
                        ESP_LOGE(TAG, "LD_GLOBAL_ADDR: Invalid func_idx %u (symbol_idx=0x%04x)", func_idx, symbol_idx);
                        return ESPB_ERR_INVALID_GLOBAL_INDEX;
                    }

                    // Globals first (symbol_idx in [0, num_globals))
                    if (__builtin_expect(symbol_idx < module->num_globals, 1)) {
                        const EspbGlobalDesc *global_desc = &module->globals[symbol_idx];
                        if (global_desc->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                            if (!instance->memory_data) {
                                ESP_LOGE(TAG, "LD_GLOBAL_ADDR - instance->memory_data is NULL for DATA_OFFSET global_idx=%hu", symbol_idx);
                                return ESPB_ERR_INSTANTIATION_FAILED;
                            }
                            addr = (uintptr_t)(instance->memory_data + global_desc->initializer.data_section_offset);
                        } else if (global_desc->init_kind == ESPB_INIT_KIND_CONST || global_desc->init_kind == ESPB_INIT_KIND_ZERO) {
                            if (!instance->globals_data || !instance->global_offsets) {
                                ESP_LOGE(TAG, "LD_GLOBAL_ADDR - globals_data or global_offsets is NULL for global_idx=%hu", symbol_idx);
                                return ESPB_ERR_INSTANTIATION_FAILED;
                            }
                            addr = (uintptr_t)(instance->globals_data + instance->global_offsets[symbol_idx]);
                        } else {
                            ESP_LOGE(TAG, "LD_GLOBAL_ADDR - Unknown init_kind %d for global_idx=%hu", global_desc->init_kind, symbol_idx);
                            return ESPB_ERR_INVALID_OPERAND;
                        }
                        DEBUG_CHECK_REG(rd, max_reg_used, "LD_GLOBAL_ADDR");
                        SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                        V_PTR(locals[rd]) = (void*)addr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "LD_GLOBAL_ADDR R%u <- global[%hu] = %p", rd, symbol_idx, (void*)addr);
#endif
                        goto interpreter_loop_start;
                    }

                    ESP_LOGE(TAG, "LD_GLOBAL_ADDR: Invalid symbol_idx %hu (not a valid global)", symbol_idx);
                    return ESPB_ERR_INVALID_GLOBAL_INDEX;
                }
                op_0x09: { // CALL_IMPORT import_idx(u16)
                    uint16_t import_idx;
                    memcpy(&import_idx, pc, sizeof(import_idx)); pc += sizeof(import_idx);
                    uint8_t ret_reg = 0; // возвращаем в R0 по умолчанию
                    
                    // ДИАГНОСТИКА: Отслеживаем состояние системы перед каждым FFI вызовом
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    TickType_t current_tick = xTaskGetTickCount();
                    #if ESPB_RUNTIME_OC_DEBUG
                    ESP_LOGD(TAG, "ESPB FFI DEBUG: CALL_IMPORT #%u at tick %u", import_idx, (unsigned)current_tick);
                    #endif
#endif
                    
                    // Расширенный формат CALL_IMPORT: проверяем, есть ли дополнительная информация о типах
                    bool has_variadic_info = false;
                    uint8_t num_total_args = 0;
                    EspbValueType arg_types[FFI_ARGS_MAX] = {0};

                    // Проверяем специальный расширенный формат: если следующий байт == 0xAA, это маркер вариативной информации
                    if (__builtin_expect(pc < instructions_end_ptr && *pc == 0xAA, 0)) {
                         has_variadic_info = true;
                         pc++; // Пропускаем маркер
                         // Читаем общее количество аргументов
                         if (pc < instructions_end_ptr) {
                             num_total_args = *pc++;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                            ESP_LOGD(TAG, "Found extended CALL_IMPORT with %u total args", num_total_args);
#endif
                             // Читаем типы всех аргументов
                             for (uint8_t i = 0; i < num_total_args && i < FFI_ARGS_MAX && pc < instructions_end_ptr; i++) {
                                 arg_types[i] = (EspbValueType)*pc++;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                ESP_LOGD(TAG, "Arg %u type: %d", i, arg_types[i]);
#endif
                             }
                         } else {
                             ESP_LOGE(TAG, "Truncated extended CALL_IMPORT format");
                             // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPCODE;
                         }
                     }

                    if (import_idx >= instance->module->num_imports || instance->module->imports[import_idx].kind != ESPB_IMPORT_KIND_FUNC) {
                        ESP_LOGE(TAG, "Invalid import index %u or not a function.", import_idx);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                    }

                    EspbImportDesc *import_desc = &instance->module->imports[import_idx];
                    uint16_t sig_idx = import_desc->desc.func.type_idx;
                    EspbFuncSignature *native_sig = &instance->module->signatures[sig_idx];
                    
                    // Число аргументов для FFI
                    uint32_t num_native_args = has_variadic_info ? num_total_args : native_sig->num_params;
                    // Число фиксированных аргументов для FFI (из сигнатуры)
                    uint32_t nfixedargs = native_sig->num_params;

                    void *fptr = instance->resolved_import_funcs[import_idx];
                    if (!fptr) {
                        ESP_LOGE(TAG, "resolved_import_funcs[%u] is NULL for module_num=%u name=%s",
                                import_idx, (unsigned)import_desc->module_num,
                                import_desc->entity_name ? import_desc->entity_name : "<indexed>");
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
                    }
                    
                    // DEBUG-only diagnostics; for indexed imports entity_name can be NULL
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (import_desc->entity_name && strcmp(import_desc->entity_name, "vTaskDelay") == 0) {
                        ESP_LOGD(TAG, "ESPB FFI DEBUG: Checking main thread state before vTaskDelay");
                        TickType_t tick_now = xTaskGetTickCount();
                        ESP_LOGD(TAG, "ESPB FFI DEBUG: Current tick_count=%u", (unsigned)tick_now);
                    }
#endif

   
                    ffi_cif cif_native_call;
                    ffi_type *ffi_native_arg_types[FFI_ARGS_MAX];
                    void *ffi_native_arg_values[FFI_ARGS_MAX];
                    
                    // Временные хранилища для значений, которые нужно преобразовать
                    int64_t temp_i64_values[FFI_ARGS_MAX];
                    uint64_t temp_u64_values[FFI_ARGS_MAX];
                    
                    // Временные хранилища для указателей на замыкания и контексты, если они создаются
                    void *created_closure_exec_ptr[FFI_ARGS_MAX] = {NULL}; // Указатели на исполняемый код замыкания
                    // EspbCallbackClosure *created_closures[FFI_ARGS_MAX] = {NULL}; // Unused variable commented out

                    //ESP_LOGD(TAG,"ESPB FFI DEBUG: Preparing ffi_call for import '%s::%s' (idx %u), sig_idx=%hu, num_native_args=%" PRIu32 ", fptr=%p\n",
                    //        import_desc->module_name, import_desc->entity_name, import_idx, sig_idx, num_native_args, fptr);

                    if (num_native_args > FFI_ARGS_MAX) {
                        ESP_LOGE(TAG, "Number of native arguments %" PRIu32 " exceeds FFI_ARGS_MAX %d", num_native_args, FFI_ARGS_MAX);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                    }

                    for (uint32_t i = 0; i < num_native_args; ++i) {
                        // Определяем тип аргумента: из расширенной информации или из сигнатуры
                        EspbValueType es_arg_type;
                        if (has_variadic_info) {
                            es_arg_type = arg_types[i];
                        } else if (i < native_sig->num_params) {
                            es_arg_type = native_sig->param_types[i];
                        } else {
                            ESP_LOGE(TAG, "Cannot determine type for argument %u", (unsigned)i);
                            // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }
                        
                        ffi_native_arg_types[i] = espb_type_to_ffi_type(es_arg_type);
                        if (!ffi_native_arg_types[i]) {
                            ESP_LOGE(TAG, "Unsupported ESPB param type %d for FFI (arg %" PRIu32 ") for module_num=%u name=%s",
                                   es_arg_type, i, (unsigned)import_desc->module_num,
                                   import_desc->entity_name ? import_desc->entity_name : "<indexed>");
                            // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }

                        // По умолчанию берем значение как есть из регистра locals[i]
                        // Тип данных в locals[i] должен соответствовать es_arg_type из сигнатуры нативной функции
                        // или быть совместимым (например, BOOL и I32)
                        switch (es_arg_type) {
                            case ESPB_TYPE_I8: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_U8: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_I16: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_U16: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_I32: case ESPB_TYPE_BOOL: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_U32: ffi_native_arg_values[i] = &V_I32(locals[i]); break;
                            case ESPB_TYPE_I64:
                                // Для 64-битных знаковых аргументов
                                temp_i64_values[i] = V_I64(locals[i]);
                                ffi_native_arg_values[i] = &temp_i64_values[i];
                                break;
                            case ESPB_TYPE_U64:
                                // Для 64-битных беззнаковых аргументов
                                temp_u64_values[i] = V_I64(locals[i]);
                                ffi_native_arg_values[i] = &temp_u64_values[i];
                                break;
                            case ESPB_TYPE_F32: ffi_native_arg_values[i] = &V_F32(locals[i]); break;
                            case ESPB_TYPE_F64: ffi_native_arg_values[i] = &V_F64(locals[i]); break;
                            case ESPB_TYPE_PTR: ffi_native_arg_values[i] = &V_PTR(locals[i]); break;
                                default:
                                ESP_LOGE(TAG, "Cannot get value for unsupported ESPB type %d (arg %" PRIu32 ")", es_arg_type, i);
                                // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }
                        //ESP_LOGD(TAG,"ESPB FFI DEBUG: Arg %" PRIu32 " (ESPB Type %d, FFI Type %p): val_ptr=%p\n", i, es_arg_type, (void*)ffi_native_arg_types[i], ffi_native_arg_values[i]);


                        // логика обработки колбэков
                        // ИСПРАВЛЕНИЕ: Проверяем callback'и также для типа PTR, так как сигнатуры импортов
                        // используют PTR для указателей на функции, но транслятор генерирует значение с CALLBACK_FLAG_BIT
                        if (exec_ctx->feature_callback_auto_active && (es_arg_type == ESPB_TYPE_I32 || es_arg_type == ESPB_TYPE_PTR)) {
                            int32_t potential_cb_arg = V_I32(locals[i]);
                            
                            // ЕДИНСТВЕННЫЙ СПОСОБ: Проверка на специальный флаг CALLBACK_FLAG_BIT
                            bool is_callback = false;
                            uint32_t espb_func_idx = 0;
                            
                            // Универсальный подход для определения колбэков:
                            // 1. Должен иметь установленный CALLBACK_FLAG_BIT
                            // 2. После удаления флага, должен указывать на валидный индекс функции
                            // 3. Не должен быть стандартным значением без функционального смысла
                            if ((potential_cb_arg & CALLBACK_FLAG_BIT) == CALLBACK_FLAG_BIT) {
                                // Извлекаем предполагаемый индекс функции
                                uint32_t func_idx_candidate = potential_cb_arg & ~CALLBACK_FLAG_BIT;
                                
                                // Проверяем, является ли это валидным индексом функции
                                if (func_idx_candidate < instance->module->num_functions) {
                                    is_callback = true;
                                    espb_func_idx = func_idx_candidate;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                   ESP_LOGD(TAG, "Arg %u определен как колбэк по FLAG_BIT. Индекс функции ESPB: %u",
                                          (unsigned int)i, (unsigned int)espb_func_idx);
#endif
                                } else {
                                    // Флаг установлен, но индекс функции невалидный - это user_data
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                   ESP_LOGD(TAG,"FFI DEBUG: Arg %u имеет флаг колбэка, но индекс функции %u за пределами диапазона [0, %u) - интерпретируем как user_data",
                                          (unsigned int)i, (unsigned int)func_idx_candidate,
                                          (unsigned int)instance->module->num_functions);
#endif
                                }
                            }
                            
                            if (is_callback) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                               ESP_LOGD(TAG,"FFI DEBUG: Processing callback for ESPB func_idx %" PRIu32 " at arg %" PRIu32, espb_func_idx, i);
#endif

                                if (espb_func_idx >= instance->module->num_functions) {
                                     ESP_LOGE(TAG, "Callback ESPB func_idx %" PRIu32 " out of bounds (num_functions %" PRIu32 ").",
                                             espb_func_idx, instance->module->num_functions);
                                     // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_FUNC_INDEX;
                                }
                                
                                // Для xTaskCreatePinnedToCore: создаем EspbClosureCtx и передаем его как user_data
                                // в нативную функцию. В espb_ffi_closure_handler будем использовать
                                // ctx->original_user_data, который будет установлен в closure_ctx.
                                void* original_user_data_for_espb = NULL;
                                uint32_t user_data_arg_idx = 0; // Инициализируем для всех случаев
                                
                                // --- Универсальная обработка колбэков через cbmeta ---
                                user_data_arg_idx = 0xFFFFFFFF; // Используем как маркер "не найдено"
                                bool user_data_found = false;

                                if (instance->module && instance->module->cbmeta.num_imports_with_cb > 0 && instance->module->cbmeta.imports) {
                                    for (uint16_t mi = 0; mi < instance->module->cbmeta.num_imports_with_cb; ++mi) {
                                        const EspbCbmetaImportEntry *m = &instance->module->cbmeta.imports[mi];
                                        if (m->import_index == import_idx) {
                                            const uint8_t *ep = m->entries;
                                            for (uint8_t pi = 0; pi < m->num_callbacks; ++pi) {
                                                uint8_t cbHeader = *ep;
                                                uint8_t cb_idx = (uint8_t)(cbHeader & 0x0F);
                                                uint8_t ud_idx = (uint8_t)((cbHeader >> 4) & 0x0F);

                                                if (cb_idx == (uint8_t)i) {
                                                    if (ud_idx != 0x0F) {
                                                        user_data_arg_idx = (uint32_t)ud_idx;
                                                        user_data_found = true;
                                                    }
                                                    break; // Нашли наш колбэк, выходим из внутреннего цикла
                                                }
                                                ep += 3; // Переходим к следующей записи
                                            }
                                            if (user_data_found) break; // Выходим из внешнего цикла
                                        }
                                    }
                                }

                                if (user_data_found) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGD(TAG, "cbmeta found user_data for cb at arg %" PRIu32 " -> user_data is at arg %" PRIu32, i, user_data_arg_idx);
#endif
#endif
                                    if (user_data_arg_idx < num_native_args) {
                                         if (native_sig->param_types[user_data_arg_idx] == ESPB_TYPE_PTR) {
                                            original_user_data_for_espb = V_PTR(locals[user_data_arg_idx]);
                                        } else {
                                            original_user_data_for_espb = (void*)(uintptr_t)V_I32(locals[user_data_arg_idx]);
                                        }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                        ESP_LOGD(TAG, "Extracted user_data from arg %" PRIu32 ", ptr_val=%p", user_data_arg_idx, original_user_data_for_espb);
#endif
#endif
                                    } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                        ESP_LOGD(TAG, "user_data_arg_idx %" PRIu32 " is out of bounds (num_native_args: %" PRIu32 ")", user_data_arg_idx, num_native_args);
#endif
#endif
                                    }
                                } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGD(TAG, "cbmeta not found for import %u, cb_arg %u. No user_data assigned.", import_idx, (unsigned int)i);
#endif
#endif
                                }

                                // --- ЭТАП 1: Поиск/создание callback замыкания через espb_callback_system ---
                                // EspbCallbackClosure **created_closure_ptr = &created_closures[i]; // Unused variable commented out
                                void **created_exec_ptr = &created_closure_exec_ptr[i];

                                // Используем новую систему callback'ов
                                EspbResult cb_result = espb_create_callback_closure(
                                    instance,
                                    import_idx,
                                    i,  // callback_param_idx
                                    espb_func_idx,
                                    user_data_arg_idx,
                                    original_user_data_for_espb,
                                    created_exec_ptr
                                );

                                if (cb_result != ESPB_OK) {
                                    ESP_LOGE(TAG, "espb_create_callback_closure failed with code %d for ESPB func_idx %" PRIu32,
                                           cb_result, espb_func_idx);
                                    // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return cb_result;
                                }

                                created_closure_exec_ptr[i] = *created_exec_ptr; // Используем указатель на исполняемый код
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                               ESP_LOGD(TAG,"FFI DEBUG:   New callback closure created via espb_callback_system. Exec ptr: %p", created_closure_exec_ptr[i]);
#endif

                                // Новую систему уже вызвали выше через espb_create_callback_closure
                                
                                // --- ЭТАП 2: Аргументы уже правильно настроены новой системой callback'ов ---
                                // Заказмаемся созданием callback closure через espb_callback_system
                                if (created_closure_exec_ptr[i]) {
                                    // Аргумент, который был индексом колбэка (locals[i]), теперь должен стать указателем на замыкание.
                                    // УНИВЕРСАЛЬНЫЙ ПОДХОД: Тип аргумента в сигнатуре FFI всегда должен быть PTR для указателей на функции,
                                    // независимо от того, какой тип указан в сигнатуре ESPB. Это потому что нативный код всегда 
                                    // ожидает указатель на функцию (т.е. тип ffi_type_pointer), даже если в ESPB это I32.
                                    
                                    // Всегда используем указатель для передачи исполняемого кода замыкания в нативную функцию
                                    ffi_native_arg_types[i] = &ffi_type_pointer; // Всегда PTR для указателя на функцию
                                    
                                    if (native_sig->param_types[i] != ESPB_TYPE_PTR) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG,"FFI INFO: Native function module_num=%u name=%s arg %"PRIu32" type adjusted from %d to PTR for function closure",
                                           (unsigned)import_desc->module_num,
                                           import_desc->entity_name ? import_desc->entity_name : "<indexed>",
                                           i, native_sig->param_types[i]);
#endif
                                    }
                                    
                                    created_closure_exec_ptr[i] = *created_exec_ptr;
                                    ffi_native_arg_values[i] = &created_closure_exec_ptr[i]; // Указатель на указатель для ffi_call
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                   ESP_LOGD(TAG,"FFI DEBUG:   Arg %" PRIu32 " (callback) replaced with closure exec_ptr %p (value_slot now points to %p)",
                                          i, created_closure_exec_ptr[i], ffi_native_arg_values[i]);
#endif
                                }
                                
                                // Аргумент user_data: обычно заменяем на указатель на EspbClosureCtx,
                                // кроме хост-хелперов, где оставляем исходный user_data из ESPB.
                                if (user_data_arg_idx < num_native_args) {
                                    // Универсальная обработка: НЕ подменяем user_data на closure_ctx.
                                    // Указатель на значение user_data уже был правильно установлен в ffi_native_arg_values
                                    // в основном цикле обработки аргументов. Здесь просто логируем этот факт.
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGD(TAG, "FFI DEBUG:   User data for callback found at arg %" PRIu32 ". Passing original value.", user_data_arg_idx);
#endif
                                }
                            }
                        }
                    }
                    // --- Конец обработки аргументов и колбэков ---

                    // === UNIVERSAL IMMETA-BASED MARSHALLING (per-arg plan) ===
                    ArgPlan arg_plans[FFI_ARGS_MAX] = {0};
                    
                    EspbImmetaImportEntry *immeta_entry = NULL;
                    bool has_immeta = false;
                    if ((module->header.features & FEATURE_MARSHALLING_META) != 0) {
                        has_immeta = espb_find_marshalling_metadata(module, import_idx, &immeta_entry);
                    }

                    bool has_async_out_params = false;
                    uint8_t std_alloc_count = 0;

                    if (has_immeta && immeta_entry) {
                        for (uint8_t i = 0; i < num_native_args; ++i) {
                            const EspbImmetaArgEntry *arg_info = NULL;
                            if (espb_get_arg_marshalling_info(immeta_entry, i, (const EspbImmetaArgEntry**)&arg_info)) {
                                arg_plans[i].has_meta    = 1;
                                arg_plans[i].direction   = arg_info->direction_flags;
                                arg_plans[i].handler_idx = arg_info->handler_index;
                                arg_plans[i].buffer_size = espb_calculate_buffer_size(arg_info, locals, num_native_args);
                                arg_plans[i].original_ptr = V_PTR(locals[i]);
                                ESP_LOGD(TAG, "IMMETA SETUP: arg %u, direction=0x%x, handler=%u, buffer_size=%u, original_ptr=%p (from R%u)",
                                         i, arg_info->direction_flags, arg_info->handler_index, 
                                         arg_plans[i].buffer_size, arg_plans[i].original_ptr, i);
                                if ((arg_info->direction_flags & ESPB_IMMETA_DIRECTION_OUT) && arg_info->handler_index == 1) {
                                    has_async_out_params = true;
                                }
                            }
                        }
                    }

                    EspbValueType native_ret_es_type = (native_sig->num_returns > 0) ? native_sig->return_types[0] : ESPB_TYPE_VOID;
                    ffi_type *ffi_native_ret_type = espb_type_to_ffi_type(native_ret_es_type);
                    if (!ffi_native_ret_type && native_ret_es_type != ESPB_TYPE_VOID) {
                        return ESPB_ERR_INVALID_OPERAND;
                    }

                    union { int8_t i8; uint8_t u8; int16_t i16; uint16_t u16; int32_t i32; uint32_t u32;
                            int64_t i64; uint64_t u64; float f32; double f64; void *p; } native_call_ret_val_container;

                    int ffi_status;
                    if (has_variadic_info) {
                        ffi_status = ffi_prep_cif_var(&cif_native_call, FFI_DEFAULT_ABI,
                                                      nfixedargs, num_native_args,
                                                      ffi_native_ret_type, ffi_native_arg_types);
                    } else {
                        ffi_status = ffi_prep_cif(&cif_native_call, FFI_DEFAULT_ABI,
                                                  num_native_args, ffi_native_ret_type, ffi_native_arg_types);
                    }

                    if (ffi_status != FFI_OK) {
                        return ESPB_ERR_RUNTIME_ERROR;
                    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD("espb_debug", "=== CALL_IMPORT DEBUG === Import #%u, has_immeta: %s, has_async_out_params: %s",
                            import_idx, has_immeta ? "YES" : "NO", has_async_out_params ? "YES" : "NO");
#endif
                    
                    void* final_fptr = fptr;

                    if (has_immeta && !has_async_out_params) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD("espb_debug", "USING STANDARD MARSHALLING");
#endif
                        for (uint8_t i = 0; i < num_native_args; ++i) {
                            if (arg_plans[i].has_meta && arg_plans[i].handler_idx == 0 && arg_plans[i].buffer_size > 0) {
                                void *temp = malloc(arg_plans[i].buffer_size);
                                if (!temp) { return ESPB_ERR_MEMORY_ALLOC; }
                                arg_plans[i].temp_buffer = temp;
                                if ((arg_plans[i].direction & ESPB_IMMETA_DIRECTION_IN) && arg_plans[i].original_ptr) {
                                    memcpy(temp, arg_plans[i].original_ptr, arg_plans[i].buffer_size);
                                    ESP_LOGD(TAG, "IMMETA TEMP: arg %u IN - copied %u bytes from original %p to temp %p",
                                             i, arg_plans[i].buffer_size, arg_plans[i].original_ptr, temp);
                                } else {
                                    memset(temp, 0, arg_plans[i].buffer_size);
                                    ESP_LOGD(TAG, "IMMETA TEMP: arg %u OUT - zeroed %u bytes at temp %p", 
                                             i, arg_plans[i].buffer_size, temp);
                                }
                                ffi_native_arg_values[i] = &arg_plans[i].temp_buffer;
                                ESP_LOGD(TAG, "IMMETA FFI: arg %u - ffi_native_arg_values[%u] = %p (points to temp_buffer ptr at %p, value=%p)",
                                         i, i, ffi_native_arg_values[i], &arg_plans[i].temp_buffer, arg_plans[i].temp_buffer);
                                std_alloc_count++;
                            }
                        }
                    } else if (has_immeta && has_async_out_params) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD("espb_async", "HANDLING ASYNC MARSHALLING CALL for import #%u", import_idx);
#endif
                        
                        if (!instance->async_wrappers) {
                            instance->num_async_wrappers = module->num_imports;
                            instance->async_wrappers = (AsyncWrapper**)calloc(instance->num_async_wrappers, sizeof(AsyncWrapper*));
                            if (!instance->async_wrappers) { return ESPB_ERR_OUT_OF_MEMORY; }
                        }
                        
                        if (import_idx < instance->num_async_wrappers && !instance->async_wrappers[import_idx]) {
                            AsyncWrapper *wrapper = create_async_wrapper_for_import(instance, import_idx,
                                                                                   immeta_entry, arg_plans, num_native_args, &cif_native_call);
                            if (!wrapper) { return ESPB_ERR_RUNTIME_ERROR; }
                            instance->async_wrappers[import_idx] = wrapper;
                        }
                        
                        AsyncWrapper *wrapper = (import_idx < instance->num_async_wrappers) ? instance->async_wrappers[import_idx] : NULL;
                        if (!wrapper) { return ESPB_ERR_RUNTIME_ERROR; }
                        
                        for (uint8_t i = 0; i < wrapper->context.num_out_params; ++i) {
                            uint8_t arg_idx = wrapper->context.out_params[i].arg_index;
                            wrapper->context.out_params[i].espb_memory_ptr = arg_plans[arg_idx].original_ptr;
                            wrapper->context.out_params[i].buffer_size = arg_plans[arg_idx].buffer_size;
                        }
                        
                        final_fptr = wrapper->executable_code;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD("espb_async", "Calling through async wrapper: %p", final_fptr);
#endif
                    }

                   bool is_blocking_call = instance->import_is_blocking[import_idx];
                   
                   size_t frame_size_bytes = num_virtual_regs * sizeof(Value);

                   if (is_blocking_call) {
                       // "Быстрый путь" - проверка стека встроена inline
                       if (__builtin_expect(exec_ctx->sp + frame_size_bytes > exec_ctx->shadow_stack_capacity, 0)) {
                           int stack_status = _espb_grow_shadow_stack(exec_ctx, frame_size_bytes);
                           if (stack_status < 0) { return ESPB_ERR_OUT_OF_MEMORY; }
                           if (stack_status > 0) { // Буфер перемещен, обновляем указатель на locals
                               locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                           }
                       }
                       memcpy(exec_ctx->shadow_stack_buffer + exec_ctx->sp, locals, frame_size_bytes);
                       exec_ctx->sp += frame_size_bytes; // Protect the saved frame
                   }

                    ffi_call(&cif_native_call, FFI_FN(final_fptr), &native_call_ret_val_container, ffi_native_arg_values);

                    if (has_immeta && !has_async_out_params && std_alloc_count > 0) {
                        for (uint8_t i = 0; i < num_native_args; ++i) {
                            if (arg_plans[i].has_meta && arg_plans[i].handler_idx == 0 && arg_plans[i].temp_buffer) {
                                // Copy-back is determined solely by immeta direction, not by function readonly status
                                if (arg_plans[i].direction & ESPB_IMMETA_DIRECTION_OUT) {
                                    if (arg_plans[i].original_ptr) {
                                        ESP_LOGD(TAG, "IMMETA COPY-BACK: arg %u, copying %u bytes from temp %p to original %p",
                                                 i, arg_plans[i].buffer_size, arg_plans[i].temp_buffer, arg_plans[i].original_ptr);
                                        memcpy(arg_plans[i].original_ptr, arg_plans[i].temp_buffer, arg_plans[i].buffer_size);
                                    } else {
                                        ESP_LOGW(TAG, "IMMETA COPY-BACK: arg %u, original_ptr is NULL!", i);
                                    }
                                }
                                free(arg_plans[i].temp_buffer);
                                arg_plans[i].temp_buffer = NULL;
                            }
                        }
                    }

                   if (is_blocking_call) {
                       exec_ctx->sp -= frame_size_bytes; // Unwind the stack pointer
                       memcpy(locals, exec_ctx->shadow_stack_buffer + exec_ctx->sp, frame_size_bytes);
                   }

                    // Обработка результата вызова
                    if (native_ret_es_type != ESPB_TYPE_VOID) {
                        // Конвертируем результат из native_call_ret_val_container в соответствующий тип ESPB
                        switch(native_ret_es_type) {
                            case ESPB_TYPE_I8:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.i8;
                                break;
                            case ESPB_TYPE_U8:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.u8;
                                break;
                            case ESPB_TYPE_I16:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.i16;
                                break;
                            case ESPB_TYPE_U16:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.u16;
                                break;
                            case ESPB_TYPE_I32:
                            case ESPB_TYPE_BOOL:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.i32;
                                break;
                            case ESPB_TYPE_U32:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_U32);
                                V_I32(locals[ret_reg]) = native_call_ret_val_container.u32;
                                break;
                            case ESPB_TYPE_I64:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_I64);
                                V_I64(locals[ret_reg]) = native_call_ret_val_container.i64;
                                break;
                            case ESPB_TYPE_U64:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_U64);
                                V_I64(locals[ret_reg]) = native_call_ret_val_container.u64;
                                break;
                            case ESPB_TYPE_F32:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_F32);
                                V_F32(locals[ret_reg]) = native_call_ret_val_container.f32;
                                break;
                            case ESPB_TYPE_F64:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_F64);
                                V_F64(locals[ret_reg]) = native_call_ret_val_container.f64;
                                break;
                            case ESPB_TYPE_PTR:
                                SET_TYPE(locals[ret_reg], ESPB_TYPE_PTR);
                                V_PTR(locals[ret_reg]) = native_call_ret_val_container.p;
                                break;
                            default:
                                ESP_LOGE(TAG, "Unsupported return type %d for FFI result conversion", native_ret_es_type);
                                break;
                        }
                    }

                    goto interpreter_loop_start;

             }
// --- Начало блока для SELECT ---
op_0xBE: // SELECT.I32 / BOOL
{
    uint8_t rd = READ_U8();
    uint8_t r_cond = READ_U8();
    uint8_t r_true = READ_U8();
    uint8_t r_false = READ_U8();
    bool condition = (V_I32(locals[r_cond]) != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.I32 R%u, R%u(%s), R%u, R%u -> val=%" PRId32, rd, r_cond, condition ? "true" : "false", r_true, r_false, V_I32(locals[rd]));
#endif
    goto interpreter_loop_start;
}
op_0xBF: // SELECT.I64
{
    uint8_t rd = READ_U8();
    uint8_t r_cond = READ_U8();
    uint8_t r_true = READ_U8();
    uint8_t r_false = READ_U8();
    bool condition = (V_I32(locals[r_cond]) != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.I64 R%u, R%u(%s), R%u, R%u -> val=%" PRId64, rd, r_cond, condition ? "true" : "false", r_true, r_false, V_I64(locals[rd]));
#endif
    goto interpreter_loop_start;
}
op_0xD4: // SELECT.F32
{
    uint8_t rd = READ_U8();
    uint8_t r_cond = READ_U8();
    uint8_t r_true = READ_U8();
    uint8_t r_false = READ_U8();
    bool condition = (V_I32(locals[r_cond]) != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.F32 R%u, R%u(%s), R%u, R%u -> val=%f", rd, r_cond, condition ? "true" : "false", r_true, r_false, V_F32(locals[rd]));
#endif
    goto interpreter_loop_start;
}
op_0xD5: // SELECT.F64
{
    uint8_t rd = READ_U8();
    uint8_t r_cond = READ_U8();
    uint8_t r_true = READ_U8();
    uint8_t r_false = READ_U8();
    bool condition = (V_I32(locals[r_cond]) != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.F64 R%u, R%u(%s), R%u, R%u -> val=%f", rd, r_cond, condition ? "true" : "false", r_true, r_false, V_F64(locals[rd]));
#endif
    goto interpreter_loop_start;
}
op_0xD6: // SELECT.PTR
{
    uint8_t rd = READ_U8();
    uint8_t r_cond = READ_U8();
    uint8_t r_true = READ_U8();
    uint8_t r_false = READ_U8();
    bool condition = (V_I32(locals[r_cond]) != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.PTR R%u, R%u(%s), R%u, R%u -> val=%p", rd, r_cond, condition ? "true" : "false", r_true, r_false, V_PTR(locals[rd]));
#endif
    goto interpreter_loop_start;
}
// --- Конец блока для SELECT ---
                op_0xC0: // CMP.EQ.I32
                op_0xC1: // CMP.NE.I32
                op_0xC2: // CMP.LT.I32S
                op_0xC3: // CMP.GT.I32S
                op_0xC4: // CMP.LE.I32S
                op_0xC5: // CMP.GE.I32S
                op_0xC6: // CMP.LT.I32U
                op_0xC7: // CMP.GT.I32U
                op_0xC8: // CMP.LE.I32U
                op_0xC9: { // CMP.GE.I32U
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t r2 = READ_U8();
                    DEBUG_CHECK_REGS_3(rd, r1, r2, max_reg_used, "CMP");

                    int32_t val1 = V_I32(locals[r1]);
                    int32_t val2 = V_I32(locals[r2]);
                    bool cmp_res = false;

                    switch(opcode) {
                        case 0xC0: cmp_res = (val1 == val2); break;
                        case 0xC1: cmp_res = (val1 != val2); break;
                        case 0xC2: cmp_res = (val1 < val2); break;
                        case 0xC3: cmp_res = (val1 > val2); break;
                        case 0xC4: cmp_res = (val1 <= val2); break;
                        case 0xC5: cmp_res = (val1 >= val2); break;
                        case 0xC6: cmp_res = ((uint32_t)val1 < (uint32_t)val2); break;
                        case 0xC7: cmp_res = ((uint32_t)val1 > (uint32_t)val2); break;
                        case 0xC8: cmp_res = ((uint32_t)val1 <= (uint32_t)val2); break;
                        case 0xC9: cmp_res = ((uint32_t)val1 >= (uint32_t)val2); break;
                    }
                    
                    SET_TYPE(locals[rd], ESPB_TYPE_BOOL);
                    V_I32(locals[rd]) = cmp_res ? 1 : 0;
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "CMP Opcode 0x%02X: R%u, R%u, R%u -> %d", opcode, rd, r1, r2, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
            op_0xCA: // CMP.EQ.I64
            op_0xCB: // CMP.NE.I64
            op_0xCC: // CMP.LT.I64S
            op_0xCD: // CMP.GT.I64S
            op_0xCE: // CMP.LE.I64S
            op_0xCF: // CMP.GE.I64S
            op_0xD0: // CMP.LT.I64U
            op_0xD1: // CMP.GT.I64U
            op_0xD2: // CMP.LE.I64U
            op_0xD3: // CMP.GE.I64U
            {
                uint8_t rd = READ_U8();
                uint8_t r1 = READ_U8();
                uint8_t r2 = READ_U8();
                int64_t val1 = V_I64(locals[r1]);
                int64_t val2 = V_I64(locals[r2]);
                bool cmp_res = false;
                switch(opcode) {
                    case 0xCA: cmp_res = (val1 == val2); break;
                    case 0xCB: cmp_res = (val1 != val2); break;
                    case 0xCC: cmp_res = (val1 < val2); break;
                    case 0xCD: cmp_res = (val1 > val2); break;
                    case 0xCE: cmp_res = (val1 <= val2); break;
                    case 0xCF: cmp_res = (val1 >= val2); break;
                    case 0xD0: cmp_res = ((uint64_t)val1 < (uint64_t)val2); break;
                    case 0xD1: cmp_res = ((uint64_t)val1 > (uint64_t)val2); break;
                    case 0xD2: cmp_res = ((uint64_t)val1 <= (uint64_t)val2); break;
                    case 0xD3: cmp_res = ((uint64_t)val1 >= (uint64_t)val2); break;
                }
                SET_TYPE(locals[rd], ESPB_TYPE_BOOL);
                V_I32(locals[rd]) = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.I64 Opcode 0x%02X -> %d", opcode, V_I32(locals[rd]));
#endif
                goto interpreter_loop_start;
            }
            op_0xE0: // CMP.EQ.F32
            op_0xE1: // CMP.NE.F32
            op_0xE2: // CMP.LT.F32
            op_0xE3: // CMP.GT.F32
            op_0xE4: // CMP.LE.F32
            op_0xE5: // CMP.GE.F32
            {
                uint8_t rd = READ_U8();
                uint8_t r1 = READ_U8();
                uint8_t r2 = READ_U8();
                float val1 = V_F32(locals[r1]);
                float val2 = V_F32(locals[r2]);
                bool cmp_res = false;
                if (isnan(val1) || isnan(val2)) {
                    if (opcode == 0xE0 || opcode == 0xE1) { // Trap for EQ/NE on NaN
                        return ESPB_ERR_RUNTIME_TRAP;
                    }
                }
                switch(opcode) {
                    case 0xE0: cmp_res = (val1 == val2); break;
                    case 0xE1: cmp_res = (val1 != val2); break;
                    case 0xE2: cmp_res = (val1 < val2); break;
                    case 0xE3: cmp_res = (val1 > val2); break;
                    case 0xE4: cmp_res = (val1 <= val2); break;
                    case 0xE5: cmp_res = (val1 >= val2); break;
                }
                SET_TYPE(locals[rd], ESPB_TYPE_BOOL);
                V_I32(locals[rd]) = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.F32 Opcode 0x%02X -> %d", opcode, V_I32(locals[rd]));
#endif
                goto interpreter_loop_start;
            }
            op_0xE6: // CMP.EQ.F64
            op_0xE7: // CMP.NE.F64
            op_0xE8: // CMP.LT.F64
            op_0xE9: // CMP.GT.F64
            op_0xEA: // CMP.LE.F64
            op_0xEB: // CMP.GE.F64
            {
                uint8_t rd = READ_U8();
                uint8_t r1 = READ_U8();
                uint8_t r2 = READ_U8();
                double val1 = V_F64(locals[r1]);
                double val2 = V_F64(locals[r2]);
                bool cmp_res = false;
                 if (isnan(val1) || isnan(val2)) {
                    if (opcode == 0xE6 || opcode == 0xE7) { // Trap for EQ/NE on NaN
                        return ESPB_ERR_RUNTIME_TRAP;
                    }
                }
                switch(opcode) {
                    case 0xE6: cmp_res = (val1 == val2); break;
                    case 0xE7: cmp_res = (val1 != val2); break;
                    case 0xE8: cmp_res = (val1 < val2); break;
                    case 0xE9: cmp_res = (val1 > val2); break;
                    case 0xEA: cmp_res = (val1 <= val2); break;
                    case 0xEB: cmp_res = (val1 >= val2); break;
                }
                SET_TYPE(locals[rd], ESPB_TYPE_BOOL);
                V_I32(locals[rd]) = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.F64 Opcode 0x%02X -> %d", opcode, V_I32(locals[rd]));
#endif
                goto interpreter_loop_start;
            }

                op_0x8E: { // ADDR_OF Rd(u8), Rs(u8) - Создает указатель на виртуальный регистр
                    uint8_t rd = READ_U8();
                    uint8_t rs = READ_U8();
                    DEBUG_CHECK_REGS_2(rd, rs, max_reg_used, "ADDR_OF");
                    
                    // Получаем адрес регистра rs в памяти виртуальной машины
                    void *ptr_to_reg = &locals[rs];
                    SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                    V_PTR(locals[rd]) = ptr_to_reg;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG,"ESPB DEBUG: ADDR_OF R%u, R%u = %p\n", rd, rs, ptr_to_reg);
#endif
                    goto interpreter_loop_start;
                }
                op_0x84: { // LOAD.I32 Rd(u8), Ra(u8), offset(i16)
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int16_t offset = READ_I16();
                    
                    uintptr_t ra_addr;
                    // Since we removed type info, we assume it's a pointer,
                    // but the old logic had a hack to handle integers. We'll try to keep it.
                    // A better approach would be to ensure PTR type is always used for addresses.
                    ra_addr = (uintptr_t)V_PTR(locals[ra]);
                    
                    // HACK: Workaround for buggy bytecode using LOAD.I32 to move an immediate or native pointer.
                    // This entire block is commented out as ra_type is no longer available.
                    /*
                    if (ra_type == ESPB_TYPE_I32 || ra_type == ESPB_TYPE_U32) {
                        bool is_likely_immediate = (ra_addr < 65536); // Check for small, non-negative integers
                        bool is_native_pointer = (ra_addr >= 0x3FFAE000 && ra_addr < 0x40000000); // ESP32 DRAM address range

                        if (is_likely_immediate) {
                            // Value is likely an immediate integer, not a pointer to be dereferenced. Treat as MOV.
                           ESP_LOGW(TAG, "HACK: LOAD.I32 on R%u (val=0x%x) is treated as MOV.I32", ra, (unsigned int)ra_addr);
                            SET_TYPE(locals[rd], ESPB_TYPE_I32);
                            V_I32(locals[rd]) = ra_addr + offset; // Also apply offset
                            goto interpreter_loop_start; // Skip dereferencing
                        }
                        if (is_native_pointer) {
                            // ВОССТАНОВЛЕНО: Правильно читаем значение из нативной памяти ESP32
                           ESP_LOGD(TAG, "FIXED: LOAD.I32 on R%u (addr=0x%x) - reading I32 value from native memory", ra, (unsigned int)ra_addr);
                            
                            // Безопасно читаем 32-битное значение из нативной памяти
                            int32_t loaded_value;
                            void* src_addr = (void*)(ra_addr + offset);
                            memcpy(&loaded_value, src_addr, sizeof(int32_t));
                            
                            SET_TYPE(locals[rd], ESPB_TYPE_I32);
                            V_I32(locals[rd]) = loaded_value;
                            
                           ESP_LOGD(TAG, "FIXED: Loaded I32 value %d from native addr 0x%x", loaded_value, (unsigned int)(ra_addr + offset));
                            goto interpreter_loop_start; // Завершаем обработку
                        }
                    }
                    */

                    // Original LOAD.I32 logic for dereferencing pointers
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t base_addr = (uintptr_t)base;

                    if (ra_addr >= base_addr && ra_addr < base_addr + mem_size) {
                        // Standard case: Ra points into ESPB memory.
                        uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                        int64_t tgt = (int64_t)ra_off + offset;
                        if (tgt < 0 || (uint64_t)tgt + sizeof(int32_t) > mem_size) {
                            ESP_LOGE(TAG, "LOAD.I32 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_off, (unsigned int)offset);
                            // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                        }
                        uint32_t target = (uint32_t)tgt;
                        // Use memcpy for safe unaligned access
                        memcpy(&V_I32(locals[rd]), base + target, sizeof(int32_t));
                        SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    } else {
                        // Ra holds an absolute native address (that's not in our hacked range).
                        uintptr_t target_addr = ra_addr + offset;
                        // This is unsafe, but we assume the bytecode knows what it's doing.
                        memcpy(&V_I32(locals[rd]), (void*)target_addr, sizeof(int32_t));
                        SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I32 R%u <- mem[R%u(0x%x)+%d] = %" PRId32, rd, ra, (unsigned int)ra_addr, V_I32(locals[rd]));
#endif
                    goto interpreter_loop_start;
                }
                // Remove duplicate/old added opcodes block (0x47/0x48/0x49) - handled earlier

                // Целочисленная арифметика с Imm (I64)
                op_0x50: { // ADD.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) + imm;
                   ESP_LOGD(TAG, "ADD.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, V_I64(locals[rd]));
                goto interpreter_loop_start;
            }
                op_0x51: { // SUB.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) - imm;
                    ESP_LOGD(TAG, "SUB.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                op_0x52: { // MUL.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = V_I64(locals[r1]) * imm;
                    ESP_LOGD(TAG, "MUL.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }

                op_0x53: { // DIVS.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    int64_t dividend = V_I64(locals[r1]);
                    int64_t divisor = (int64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT64_MIN && divisor == -1) { return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = dividend / divisor;
                    ESP_LOGD(TAG, "DIVS.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                op_0x54: { // DIVU.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint64_t dividend = (uint64_t)V_I64(locals[r1]);
                    uint64_t divisor = (uint64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = dividend / divisor;
                    ESP_LOGD(TAG, "DIVU.I64.IMM8 R%u, R%u, %" PRIu8 " = %" PRIu64, rd, r1, imm, (uint64_t)V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                op_0x55: { // REMS.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    int8_t imm = (int8_t)READ_U8();
                    int64_t dividend = V_I64(locals[r1]);
                    int64_t divisor = (int64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    if (dividend == INT64_MIN && divisor == -1) {
                        V_I64(locals[rd]) = 0; // The remainder is 0 in this overflow case
                    } else {
                        V_I64(locals[rd]) = dividend % divisor;
                    }
                    ESP_LOGD(TAG, "REMS.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                op_0x56: { // REMU.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint64_t dividend = (uint64_t)V_I64(locals[r1]);
                    uint64_t divisor = (uint64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = dividend % divisor;
                    ESP_LOGD(TAG, "REMU.I64.IMM8 R%u, R%u, %" PRIu8 " = %" PRIu64, rd, r1, imm, (uint64_t)V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                // 0x57 is RESERVED
                op_0x58: { // SHRU.I64.IMM8 Rd, R1, imm8 (Logical Shift Right)
                    uint8_t rd = READ_U8();
                    uint8_t r1 = READ_U8();
                    uint8_t imm = READ_U8();
                    uint32_t shift = (uint32_t)imm & 63; // mask to 6 bits for I64
                    uint64_t val = (uint64_t)V_I64(locals[r1]);
                    SET_TYPE(locals[rd], ESPB_TYPE_U64);
                    V_I64(locals[rd]) = val >> shift;
                    ESP_LOGD(TAG, "SHRU.I64.IMM8 R%u, R%u, %u = %" PRIu64, rd, r1, imm, (uint64_t)V_I64(locals[rd]));
                    goto interpreter_loop_start;
                }
                
                op_0x1E: { // LD_GLOBAL Rd(u8), global_idx(u16)
                    uint8_t rd = READ_U8();
                    uint16_t global_idx = READ_U16();

                    if (global_idx >= module->num_globals) {
                        ESP_LOGE(TAG, "LD_GLOBAL - Invalid global_idx %hu (num_globals %" PRIu32 ")",
                                global_idx, module->num_globals);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_GLOBAL_INDEX;
                    }
                    DEBUG_CHECK_REG(rd, max_reg_used, "TABLE_OP");
                    if (0) { // Unreachable after debug check // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX; }

                    const EspbGlobalDesc *g = &module->globals[global_idx];
                    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                        if (!instance->memory_data) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->memory_data + g->initializer.data_section_offset;
                        // Если глобал — это PTR, возвращаем адрес; иначе читаем значение по типу
                        if (g->type == ESPB_TYPE_PTR) {
                            uintptr_t addr = (uintptr_t)base;
                            SET_TYPE(locals[rd], ESPB_TYPE_PTR);
                            V_PTR(locals[rd]) = (void*)addr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                           ESP_LOGD(TAG, "LD_GLOBAL (DATA_OFFSET PTR) R%u <- %p (global_idx=%hu)", rd, (void*)addr, global_idx);
#endif
                        } else {
                            // Reading non-pointer types from data section
                            // Zero out the register first to avoid garbage in upper bytes for small types (I8, U8, I16, U16)
                            if (g->type <= ESPB_TYPE_U16) {
                                V_RAW(locals[rd]) = 0;
                            }
                            memcpy(&V_RAW(locals[rd]), base, value_size_map[g->type]);
                            SET_TYPE(locals[rd], g->type); // Set type for debug/introspection
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                           ESP_LOGD(TAG, "LD_GLOBAL (DATA_OFFSET VAL) R%u <- global[%hu] (type=%d)", rd, global_idx, g->type);
#endif
                        }
                    } else {
                        if (!instance->globals_data || !instance->global_offsets) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->globals_data + instance->global_offsets[global_idx];
                        // Zero out the register first to avoid garbage in upper bytes for small types (I8, U8, I16, U16)
                        if (g->type <= ESPB_TYPE_U16) {
                            V_RAW(locals[rd]) = 0;
                        }
                        memcpy(&V_RAW(locals[rd]), base, value_size_map[g->type]);
                        SET_TYPE(locals[rd], g->type);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                       ESP_LOGD(TAG, "LD_GLOBAL R%u <- global[%hu] (type=%d)", rd, global_idx, g->type);
#endif
                    }
                    goto interpreter_loop_start;
                }

                op_0x1F: { // ST_GLOBAL global_idx(u16), Rs(u8)
                    uint16_t global_idx = READ_U16();
                    uint8_t rs = READ_U8();

                    if (global_idx >= module->num_globals) { return ESPB_ERR_INVALID_GLOBAL_INDEX; }
                    DEBUG_CHECK_REG(rs, max_reg_used, "TABLE_GET");

                    const EspbGlobalDesc *g = &module->globals[global_idx];
                    if (!g->mutability) { ESP_LOGE(TAG, "ST_GLOBAL to immutable global %hu", global_idx); return ESPB_ERR_INVALID_OPERAND; }

                    uint8_t* target_addr = NULL;
                    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                        if (!instance->memory_data) { return ESPB_ERR_INSTANTIATION_FAILED; }
                        target_addr = instance->memory_data + g->initializer.data_section_offset;
                    } else {
                        if (!instance->globals_data || !instance->global_offsets) { return ESPB_ERR_INSTANTIATION_FAILED; }
                        target_addr = instance->globals_data + instance->global_offsets[global_idx];
                    }

                    memcpy(target_addr, &V_RAW(locals[rs]), value_size_map[g->type]);
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                       ESP_LOGD(TAG, "ST_GLOBAL global[%hu] <- R%u (type=%d)", global_idx, rs, g->type);
#endif
                    goto interpreter_loop_start;
                }
                op_0xD7: { // ATOMIC.RMW.*.I32
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    uint8_t rv = READ_U8();
                    int32_t* addr = (int32_t*)V_PTR(locals[ra]);
                    int32_t value = V_I32(locals[rv]);
                    int32_t old_val;
                    switch(opcode) {
                        case 0xD7: old_val = __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xD8: old_val = __atomic_fetch_sub(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xD9: old_val = __atomic_fetch_and(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xDA: old_val = __atomic_fetch_or(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xDB: old_val = __atomic_fetch_xor(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xDC: old_val = __atomic_exchange_n(addr, value, __ATOMIC_SEQ_CST); break;
                        default: return ESPB_ERR_UNKNOWN_OPCODE;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = old_val;
                    goto interpreter_loop_start;
                }
                op_0xDD: { // ATOMIC.RMW.CMPXCHG.I32
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    uint8_t rexp = READ_U8();
                    uint8_t rdes = READ_U8();
                    int32_t* addr = (int32_t*)V_PTR(locals[ra]);
                    int32_t expected = V_I32(locals[rexp]);
                    int32_t desired = V_I32(locals[rdes]);
                    __atomic_compare_exchange_n(addr, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = expected;
                    goto interpreter_loop_start;
                }
                op_0xDE: { // ATOMIC.LOAD.I32
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int32_t* addr = (int32_t*)V_PTR(locals[ra]);
                    SET_TYPE(locals[rd], ESPB_TYPE_I32);
                    V_I32(locals[rd]) = __atomic_load_n(addr, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                op_0xDF: { // ATOMIC.STORE.I32
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int32_t* addr = (int32_t*)V_PTR(locals[ra]);
                    int32_t value = V_I32(locals[rs]);
                    __atomic_store_n(addr, value, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                // --- I64 Atomics ---
                op_0xF0: { // ATOMIC.RMW.*.I64
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    uint8_t rv = READ_U8();
                    int64_t* addr = (int64_t*)V_PTR(locals[ra]);
                    int64_t value = V_I64(locals[rv]);
                    int64_t old_val;
                    switch(opcode) {
                        case 0xF0: old_val = __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xF1: old_val = __atomic_fetch_sub(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xF2: old_val = __atomic_fetch_and(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xF3: old_val = __atomic_fetch_or(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xF4: old_val = __atomic_fetch_xor(addr, value, __ATOMIC_SEQ_CST); break;
                        case 0xF5: old_val = __atomic_exchange_n(addr, value, __ATOMIC_SEQ_CST); break;
                        default: return ESPB_ERR_UNKNOWN_OPCODE;
                    }
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = old_val;
                    goto interpreter_loop_start;
                }
                op_0xF6: { // ATOMIC.RMW.CMPXCHG.I64
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    uint8_t rexp = READ_U8();
                    uint8_t rdes = READ_U8();
                    int64_t* addr = (int64_t*)V_PTR(locals[ra]);
                    int64_t expected = V_I64(locals[rexp]);
                    int64_t desired = V_I64(locals[rdes]);
                    __atomic_compare_exchange_n(addr, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = expected;
                    goto interpreter_loop_start;
                }
                op_0xEC: { // ATOMIC.LOAD.I64
                    uint8_t rd = READ_U8();
                    uint8_t ra = READ_U8();
                    int64_t* addr = (int64_t*)V_PTR(locals[ra]);
                    SET_TYPE(locals[rd], ESPB_TYPE_I64);
                    V_I64(locals[rd]) = __atomic_load_n(addr, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                op_0xED: { // ATOMIC.STORE.I64
                    uint8_t rs = READ_U8();
                    uint8_t ra = READ_U8();
                    int64_t* addr = (int64_t*)V_PTR(locals[ra]);
                    int64_t value = V_I64(locals[rs]);
                    __atomic_store_n(addr, value, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                op_0xEE: { // ATOMIC.FENCE
                    __atomic_thread_fence(__ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                
                op_0xFC: { // Префикс для расширенных опкодов
    uint8_t extended_opcode = *pc++;
    ESP_LOGD(TAG, "=== EXTENDED OPCODE 0xFC DEBUG === sub-opcode=0x%02X", extended_opcode);
    switch (extended_opcode) {
        // --- СУЩЕСТВУЮЩИЕ ОПКОДЫ (ОСТАЮТСЯ) ---
        case 0x00: { // MEMORY.INIT data_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
            uint32_t data_seg_idx = READ_U32();
            uint8_t rd_dest = READ_U8();
            uint8_t rs_src_offset = READ_U8();
            uint8_t rn_size = READ_U8();

            if (data_seg_idx >= module->num_data_segments) { return ESPB_ERR_INVALID_OPERAND; }
            
            uint32_t dest_addr = V_I32(locals[rd_dest]);
            uint32_t src_offset = V_I32(locals[rs_src_offset]);
            uint32_t size = V_I32(locals[rn_size]);

            const EspbDataSegment *segment = &module->data_segments[data_seg_idx];

            if ((uint64_t)dest_addr + size > instance->memory_size_bytes || (uint64_t)src_offset + size > segment->data_size) {
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }
            
            memcpy(instance->memory_data + dest_addr, segment->data + src_offset, size);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "MEMORY.INIT: Copied %u bytes from data segment %u (offset %u) to memory addr %u", size, data_seg_idx, src_offset, dest_addr);
#endif
#endif
            goto interpreter_loop_start;
        }
        case 0x01: { // DATA.DROP data_seg_idx(u32)
            uint32_t data_seg_idx;
            memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);
            if (data_seg_idx >= module->num_data_segments) { return ESPB_ERR_INVALID_OPERAND; }
            EspbDataSegment *segment = (EspbDataSegment*)&instance->module->data_segments[data_seg_idx];
            segment->data_size = 0; // "Drop" by setting size to 0
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "DATA.DROP: Dropped data segment %u", data_seg_idx);
#endif
#endif
            goto interpreter_loop_start;
        }
        case 0x02: { // MEMORY.COPY Rd(u8), Rs(u8), Rn(u8)
            uint8_t rd_dest = READ_U8();
            uint8_t rs_src = READ_U8();
            uint8_t rn_size = READ_U8();
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "=== MEMORY.COPY DEBUG ===");
            ESP_LOGD(TAG, "rd_dest=%u, rs_src=%u, rn_size=%u", rd_dest, rs_src, rn_size);
#endif
#endif

            // ИСПРАВЛЕНИЕ: Преобразуем абсолютные адреса в относительные смещения
            uintptr_t dest_abs = (uintptr_t)V_PTR(locals[rd_dest]);
            uintptr_t src_abs = (uintptr_t)V_PTR(locals[rs_src]);
            uint32_t size = V_I32(locals[rn_size]);
            
            uintptr_t mem_base = (uintptr_t)instance->memory_data;
            uint32_t dest_addr = (uint32_t)(dest_abs - mem_base);
            uint32_t src_addr = (uint32_t)(src_abs - mem_base);
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "dest_addr=0x%x, src_addr=0x%x, size=%u", dest_addr, src_addr, size);
            ESP_LOGD(TAG, "memory_size_bytes=%u", instance->memory_size_bytes);

            ESP_LOGD(TAG, "MEMORY.COPY BOUNDS CHECK:");
            ESP_LOGD(TAG, "dest_addr=%u, src_addr=%u, size=%u", dest_addr, src_addr, size);
            ESP_LOGD(TAG, "memory_size_bytes=%u", instance->memory_size_bytes);
            ESP_LOGD(TAG, "dest_end=%llu, src_end=%llu", (uint64_t)dest_addr + size, (uint64_t)src_addr + size);
#endif
#endif
            
            if ((uint64_t)dest_addr + size > instance->memory_size_bytes || (uint64_t)src_addr + size > instance->memory_size_bytes) {
                ESP_LOGE(TAG, "MEMORY.COPY: OUT OF BOUNDS!");
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Performing memmove: from %p to %p, size %u", (void*)src_abs, (void*)dest_abs, size);
#endif
#endif
            
            ESP_LOGD(TAG, "MEMORY.COPY: Before copy state:");
            print_memory("SRC MEM", (const uint8_t*)src_abs, size);
            print_memory("DST MEM", (const uint8_t*)dest_abs, size);
            
            memmove((void*)dest_abs, (void*)src_abs, size);
            
            ESP_LOGD(TAG, "MEMORY.COPY: After copy state:");
            print_memory("SRC MEM (after)", (const uint8_t*)src_abs, size);
            print_memory("DST MEM (after)", (const uint8_t*)dest_abs, size);

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "MEMORY.COPY: Successfully copied %u bytes from offset %u to offset %u", size, src_addr, dest_addr);
#endif
#endif
            goto interpreter_loop_start;
        }
        case 0x03: { // MEMORY.FILL Rd(u8), Rval(u8), Rn(u8)
            uint8_t rd_dest = READ_U8();
            uint8_t r_val = READ_U8();
            uint8_t rn_size = READ_U8();

            // ИСПРАВЛЕНИЕ: Преобразуем абсолютный адрес в относительное смещение
            uintptr_t dest_abs = (uintptr_t)V_PTR(locals[rd_dest]);
            uint8_t val = (uint8_t)V_I32(locals[r_val]);
            uint32_t size = V_I32(locals[rn_size]);
            
            uintptr_t mem_base = (uintptr_t)instance->memory_data;
            uint32_t dest_addr = (uint32_t)(dest_abs - mem_base);
            
            ESP_LOGD(TAG, "=== MEMORY.FILL DEBUG ===");
            ESP_LOGD(TAG, "dest_addr=%u, val=%u, size=%u", dest_addr, val, size);
            ESP_LOGD(TAG, "memory_size_bytes=%u", instance->memory_size_bytes);

            if ((uint64_t)dest_addr + size > instance->memory_size_bytes) {
                ESP_LOGE(TAG, "MEMORY.FILL: OUT OF BOUNDS!");
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }

            memset(instance->memory_data + dest_addr, val, size);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "MEMORY.FILL: Filled %u bytes at addr %u with value %u", size, dest_addr, val);
#endif
#endif
            goto interpreter_loop_start;
        }
        
        // --- НОВЫЕ ОПКОДЫ УПРАВЛЕНИЯ КУЧЕЙ ---
        case 0x06: { // HEAP_REALLOC Rd(u8), Rp(u8), Rs(u8)
            uint8_t rd = READ_U8();
            uint8_t rp = READ_U8();
            uint8_t rs = READ_U8();
            void* old_ptr = V_PTR(locals[rp]);
            size_t new_size = (size_t)V_I32(locals[rs]);
            void* new_ptr = espb_heap_realloc(instance, old_ptr, new_size);
            SET_TYPE(locals[rd], ESPB_TYPE_PTR);
            V_PTR(locals[rd]) = new_ptr;
            goto interpreter_loop_start;
        }
        case 0x07: { // HEAP_FREE Rp(u8)
            uint8_t rp = READ_U8();
            void* ptr = V_PTR(locals[rp]);
            espb_heap_free(instance, ptr);
            V_PTR(locals[rp]) = NULL;
            goto interpreter_loop_start;
        }
        case 0x09: { // HEAP_CALLOC Rd(u8), Rn(u8), Rs(u8)
            uint8_t rd = READ_U8();
            uint8_t rn = READ_U8();
            uint8_t rs = READ_U8();
            size_t num = (size_t)V_I32(locals[rn]);
            size_t size = (size_t)V_I32(locals[rs]);
            size_t total = 0;
            void* ptr = NULL;
            if (!__builtin_mul_overflow(num, size, &total)) {
                ptr = espb_heap_malloc(instance, total);
                if (ptr) {
                    memset(ptr, 0, total);
                }
            } else {
                ESP_LOGE(TAG, "calloc arguments overflow: num=%zu, size=%zu", num, size);
            }
            SET_TYPE(locals[rd], ESPB_TYPE_PTR);
            V_PTR(locals[rd]) = ptr;
            goto interpreter_loop_start;
        }
        case 0x0B: { // HEAP_MALLOC Rd(u8), Rs(u8)
            uint8_t rd = READ_U8();
            uint8_t rs = READ_U8();
            size_t size = (size_t)V_I32(locals[rs]);
            void* ptr = espb_heap_malloc(instance, size);
            SET_TYPE(locals[rd], ESPB_TYPE_PTR);
            V_PTR(locals[rd]) = ptr;
            goto interpreter_loop_start;
        }
        
        // --- TABLE ОПКОДЫ ---
        case 0x04: { // TABLE.INIT table_idx(u8), elem_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
            uint8_t table_idx = *pc++;
            uint32_t elem_seg_idx;
            memcpy(&elem_seg_idx, pc, sizeof(elem_seg_idx)); pc += sizeof(elem_seg_idx);
            uint8_t rd_dest = *pc++;
            uint8_t rs_src_offset = *pc++;
            uint8_t rn_size = *pc++;

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.INIT: table_idx=%u, elem_seg_idx=%u, rd_dest=R%u, rs_src=R%u, rn_size=R%u",
                     table_idx, elem_seg_idx, rd_dest, rs_src_offset, rn_size);
#endif

            if (table_idx >= module->num_tables) { 
                ESP_LOGE(TAG, "TABLE.INIT: Invalid table_idx=%u (num_tables=%u)", table_idx, module->num_tables);
                return ESPB_ERR_INVALID_OPERAND; 
            }
            if (elem_seg_idx >= module->num_element_segments) { 
                ESP_LOGE(TAG, "TABLE.INIT: Invalid elem_seg_idx=%u (num_element_segments=%u)", 
                         elem_seg_idx, module->num_element_segments);
                return ESPB_ERR_INVALID_OPERAND; 
            }

            uint32_t dest_offset = V_I32(locals[rd_dest]);
            uint32_t src_offset = V_I32(locals[rs_src_offset]);
            uint32_t size = V_I32(locals[rn_size]);
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.INIT: dest_offset=%u, src_offset=%u, size=%u", dest_offset, src_offset, size);
            ESP_LOGD(TAG, "TABLE.INIT: Current table_size=%u, table_max_size=%u, table_data=%p",
                     instance->table_size, instance->table_max_size, (void*)instance->table_data);
#endif

            EspbTableDesc* table_desc = &module->tables[table_idx];
            EspbElementSegment* segment = &module->element_segments[elem_seg_idx];

            if ((uint64_t)src_offset + size > segment->num_elements) {
                ESP_LOGE(TAG, "TABLE.INIT: Source segment out of bounds (src_offset=%u, size=%u, segment->num_elements=%u)", 
                         src_offset, size, segment->num_elements);
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }

            uint32_t required_size = dest_offset + size;
            if (required_size > instance->table_size) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.INIT: Expanding table from %u to %u entries", instance->table_size, required_size);
#endif
                
                if (required_size > instance->table_max_size) {
                    ESP_LOGE(TAG, "TABLE.INIT: Required size %u exceeds max table size %u", 
                             required_size, instance->table_max_size);
                    return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                }
                
                void** new_table_data = (void**)realloc(instance->table_data, required_size * sizeof(void*));
                if (!new_table_data) {
                    ESP_LOGE(TAG, "TABLE.INIT: Failed to expand table to %u entries", required_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                
                for (uint32_t i = instance->table_size; i < required_size; ++i) {
                    new_table_data[i] = NULL;
                }
                
                instance->table_data = new_table_data;
                instance->table_size = required_size;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.INIT: Table expanded successfully to %u entries", required_size);
#endif
            }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.INIT: Initializing %u elements at offset %u from element segment %u...",
                     size, dest_offset, elem_seg_idx);
#endif
            for (uint32_t i = 0; i < size; ++i) {
                instance->table_data[dest_offset + i] = (void*)(uintptr_t)segment->function_indices[src_offset + i];
            }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.INIT: Successfully copied %u indices from element segment %u to table %u",
                     size, elem_seg_idx, table_idx);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x08: { // TABLE.SIZE Rd(u8), table_idx(u8)
            uint8_t rd = READ_U8();
            uint8_t table_idx = READ_U8();
            
            if (table_idx >= instance->module->num_tables) {
                ESP_LOGE(TAG, "TABLE.SIZE: Invalid table index %u", table_idx);
                return ESPB_ERR_INVALID_OPERAND;
            }

            DEBUG_CHECK_REG(rd, max_reg_used, "TABLE.SIZE");

            uint32_t size = instance->table_size;
            SET_TYPE(locals[rd], ESPB_TYPE_I32);
            V_I32(locals[rd]) = (int32_t)size;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.SIZE R%u <- table[%u].size = %" PRIu32, rd, table_idx, size);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x16: { // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
            uint8_t table_dest_idx = READ_U8();
            uint8_t table_src_idx = READ_U8();
            uint8_t rd_dest = READ_U8();
            uint8_t rs_src = READ_U8();
            uint8_t rn_size = READ_U8();

            if (table_dest_idx >= module->num_tables || table_src_idx >= module->num_tables) {
                return ESPB_ERR_INVALID_OPERAND;
            }

            uint32_t dest_offset = V_I32(locals[rd_dest]);
            uint32_t src_offset = V_I32(locals[rs_src]);
            uint32_t size = V_I32(locals[rn_size]);

            if ((uint64_t)src_offset + size > instance->table_size) {
                ESP_LOGE(TAG, "TABLE.COPY: Source out of bounds (src_offset=%u, size=%u, table_size=%u)",
                         src_offset, size, instance->table_size);
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }

            uint32_t required_size = dest_offset + size;
            if (required_size > instance->table_size) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.COPY: Expanding table from %u to %u entries", instance->table_size, required_size);
#endif
                
                if (required_size > instance->table_max_size) {
                    ESP_LOGE(TAG, "TABLE.COPY: Required size %u exceeds max table size %u",
                             required_size, instance->table_max_size);
                    return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                }
                
                void** new_table_data = (void**)realloc(instance->table_data, required_size * sizeof(void*));
                if (!new_table_data) {
                    ESP_LOGE(TAG, "TABLE.COPY: Failed to expand table to %u entries", required_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                
                for (uint32_t i = instance->table_size; i < required_size; ++i) {
                    new_table_data[i] = NULL;
                }
                
                instance->table_data = new_table_data;
                instance->table_size = required_size;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.COPY: Table expanded successfully to %u entries", required_size);
#endif
            }

            memmove(&instance->table_data[dest_offset], &instance->table_data[src_offset], size * sizeof(void*));

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.COPY: Copied %u elements from table %u (offset %u) to table %u (offset %u)",
                size, table_src_idx, src_offset, table_dest_idx, dest_offset);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x17: { // TABLE.FILL table_idx(u8), Rd(u8), Rval(u8), Rn(u8)
            uint8_t table_idx = READ_U8();
            uint8_t rd_dest = READ_U8();
            uint8_t r_val = READ_U8();
            uint8_t rn_size = READ_U8();

            if (table_idx >= module->num_tables) { return ESPB_ERR_INVALID_OPERAND; }
            
            uint32_t dest_offset = V_I32(locals[rd_dest]);
            void* fill_val = (void*)(uintptr_t)V_I32(locals[r_val]);
            uint32_t size = V_I32(locals[rn_size]);

            uint32_t required_size = dest_offset + size;
            if (required_size > instance->table_size) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.FILL: Expanding table from %u to %u entries", instance->table_size, required_size);
#endif
                
                if (required_size > instance->table_max_size) {
                    ESP_LOGE(TAG, "TABLE.FILL: Required size %u exceeds max table size %u",
                             required_size, instance->table_max_size);
                    return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                }
                
                void** new_table_data = (void**)realloc(instance->table_data, required_size * sizeof(void*));
                if (!new_table_data) {
                    ESP_LOGE(TAG, "TABLE.FILL: Failed to expand table to %u entries", required_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                
                for (uint32_t i = instance->table_size; i < required_size; ++i) {
                    new_table_data[i] = NULL;
                }
                
                instance->table_data = new_table_data;
                instance->table_size = required_size;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.FILL: Table expanded successfully to %u entries", required_size);
#endif
            }

            for (uint32_t i = 0; i < size; ++i) {
                instance->table_data[dest_offset + i] = fill_val;
            }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.FILL: Filled %u elements in table %u at offset %u with value %p",
                size, table_idx, dest_offset, fill_val);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x18: { // TABLE.GET Rd(u8), table_idx(u8), Rs(u8)
            uint8_t rd_dest = READ_U8();
            uint8_t table_idx = READ_U8();
            uint8_t rs_index = READ_U8();
            
            if (table_idx >= module->num_tables) {
                ESP_LOGE(TAG, "TABLE.GET: Invalid table_idx=%u (num_tables=%u)", table_idx, module->num_tables);
                return ESPB_ERR_INVALID_OPERAND;
            }
            
            uint32_t index = V_I32(locals[rs_index]);
            if (index >= instance->table_size) {
                ESP_LOGE(TAG, "TABLE.GET: Index %u out of bounds (table_size=%u)", index, instance->table_size);
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }
            
            // Получаем значение из таблицы (индекс функции или NULL)
            void* table_value = instance->table_data[index];
            // The value from the table is a function "pointer" (encoded as an index)
            // The correct return type is PTR, not I32
            SET_TYPE(locals[rd_dest], ESPB_TYPE_PTR);
            V_PTR(locals[rd_dest]) = table_value;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.GET: R%u = table[%u][%u] = %p",
                     rd_dest, table_idx, index, V_PTR(locals[rd_dest]));
#endif
            goto interpreter_loop_start;
        }
        
        case 0x19: { // TABLE.SET table_idx(u8), Rd(u8), Rval(u8)
            uint8_t table_idx = READ_U8();
            uint8_t rd_index = READ_U8();
            uint8_t rval = READ_U8();
            
            if (table_idx >= module->num_tables) {
                ESP_LOGE(TAG, "TABLE.SET: Invalid table_idx=%u (num_tables=%u)", table_idx, module->num_tables);
                return ESPB_ERR_INVALID_OPERAND;
            }
            
            uint32_t index = V_I32(locals[rd_index]);
            
            // Автоматически расширяем таблицу если нужно
            if (index >= instance->table_size) {
                uint32_t required_size = index + 1;
                
                if (required_size > instance->table_max_size) {
                    ESP_LOGE(TAG, "TABLE.SET: Required size %u exceeds max table size %u",
                             required_size, instance->table_max_size);
                    return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                }
                
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.SET: Expanding table from %u to %u entries",
                         instance->table_size, required_size);
#endif
                
                void** new_table_data = (void**)realloc(instance->table_data, required_size * sizeof(void*));
                if (!new_table_data) {
                    ESP_LOGE(TAG, "TABLE.SET: Failed to expand table to %u entries", required_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                
                for (uint32_t i = instance->table_size; i < required_size; ++i) {
                    new_table_data[i] = NULL;
                }
                
                instance->table_data = new_table_data;
                instance->table_size = required_size;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "TABLE.SET: Table expanded successfully to %u entries", required_size);
#endif
            }
            
            // Устанавливаем значение в таблицу
            void* value = (void*)(uintptr_t)V_I32(locals[rval]);
            instance->table_data[index] = value;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.SET: table[%u][%u] = 0x%x",
                     table_idx, index, V_I32(locals[rval]));
#endif
            goto interpreter_loop_start;
        }
        // --- КОНЕЦ TABLE ОПКОДОВ ---

        default:
            ESP_LOGE(TAG, "Unknown extended opcode 0xFC 0x%02X at pc=%ld", extended_opcode, pos);
            // REFACTOR_REMOVED: // REMOVED_free_locals
            return ESPB_ERR_UNKNOWN_OPCODE;
    }
    goto interpreter_loop_start;
}

                // Опкоды 0x0C-0x0E зарезервированы в спецификации ESPB v1.7
                // 0x0F - END уже реализован выше

            

            if (pc >= instructions_end_ptr && opcode != 0x0F && opcode != 0x01 && !end_reached) {
                ESP_LOGW(TAG, "Reached end of code without END/RET opcode. Last opcode: 0x%02X", opcode);
            }
#if defined(__GNUC__) || defined(__clang__)
                // The loop_continue label is removed; subsequent steps will replace gotos.
        #endif
                interpreter_loop_end:;
        
        if (!end_reached) {
           ESP_LOGD(TAG, "Function execution finished by reaching end of code (no explicit END/RET or END not reached).");
            }

        // Сохраняем результат для возврата вызывающей функции
        if (results && num_virtual_regs > 0) {
             results[0] = locals[return_register]; // return_register обычно 0
        } else if (results) {
            SET_TYPE(results[0], ESPB_TYPE_I32); V_I32(results[0]) = 0;
        }
        
        goto function_epilogue;
    // --- function epilogue start ---
function_epilogue:
    // ИСПРАВЛЕНИЕ: Копируем возвращаемое значение из R0 в results
    if (results != NULL) {
        // Функция может вернуть значение в R0 (locals[0])
        const EspbFunctionBody *func_body_ptr = &module->function_bodies[local_func_idx];
        uint32_t sig_index = module->function_signature_indices[local_func_idx];
        const EspbFuncSignature *func_sig = &module->signatures[sig_index];
        
        if (func_sig->num_returns > 0 && num_virtual_regs > 0) {
            *results = locals[0];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Function epilogue: Copying return value %d from R0 to results",
            V_I32(*results));
#endif
        } else {
            // Функция ничего не возвращает, устанавливаем results в 0
            SET_TYPE(*results, ESPB_TYPE_I32);
            V_I32(*results) = 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Function epilogue: Function returns void, setting results to 0");
#endif
        }
    }

    // РЕФАКТОРИНГ: КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ - НЕ используем // REFACTOR_REMOVED: // REMOVED_free_locals!
    // Новая система управления стеком освобождает память автоматически при возврате из вызова.
    // Ничего освобождать не нужно.
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Function epilogue: Stack frame will be cleaned up by the caller.");
#endif

    // Контекст выполнения (exec_ctx) освобождается внешней функцией, которая его создала.

    return ESPB_OK;
// --- function epilogue end ---
    } else {
        ESP_LOGE(TAG, "Function index %" PRIu32 " is not a valid local function index.", func_idx);
        return ESPB_ERR_INVALID_FUNC_INDEX;
    }
}

/* Оригинальную функцию espb_call_function переименовываем в original_espb_call_function,
   чтобы сохранить для последующего восстановления */

EspbResult original_espb_call_function(EspbInstance *instance, uint32_t func_idx, const Value *args, Value *results) {
    const EspbModule *module __attribute__((unused)) = instance->module;
    EspbResult result __attribute__((unused)) = ESPB_OK;

    // Отладочный вывод sizeof(Value)
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
   ESP_LOGD(TAG, "sizeof(Value) = %zu", sizeof(Value));
#endif

    // 1. --- Подготовка среды выполнения ---
    // Стеки размещаются на стеке C, что быстро, но ограничивает глубину рекурсии.
    // ... (оставшаяся часть оригинальной функции)
    // ... 
    
    return ESPB_OK; // Просто для примера
}
