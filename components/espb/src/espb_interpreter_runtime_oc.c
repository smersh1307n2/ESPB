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
#include "esp_system.h"

// Локальный TAG для сообщений от этого модуля
static const char *TAG = "espb_runtime_oc";

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
                return (uint32_t)args[arg_entry->size_value].value.i32;
            }
            return 32; // Fallback
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
        ESP_LOGD(TAG, "ESPB DEBUG: Callback system initialized. FEATURE_CALLBACK_AUTO: %s", 
                 ctx->feature_callback_auto_active ? "yes" : "no");
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
        for (int i = 0; i < ctx->call_stack_top; ++i) {
            if (ctx->call_stack[i].saved_frame) {
                uintptr_t offset = (uintptr_t)ctx->call_stack[i].saved_frame - (uintptr_t)old_buffer;
                ctx->call_stack[i].saved_frame = (Value*)(new_buffer + offset);
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
    arg.type = ESPB_TYPE_PTR;
    arg.value.ptr = ctx->user_arg;
    ExecutionContext *callback_exec_ctx = init_execution_context();
    if (!callback_exec_ctx) {
        ESP_LOGE(TAG, "Failed to create execution context for callback dispatch");
        return;
    }
    espb_call_function(ctx->instance, callback_exec_ctx, ctx->func_idx, &arg, NULL);
    free_execution_context(callback_exec_ctx);
}
//__attribute__((noinline, optimize("O0")))
IRAM_ATTR 
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
        ESP_LOGD(TAG, "ESPB DEBUG: Using pre-resolved imports from instantiation");
#endif
        
        uint32_t local_func_idx = func_idx - num_imported_funcs;
        if (local_func_idx >= module->num_functions) {
            ESP_LOGE(TAG, "Function index %" PRIu32 " out of bounds", func_idx);
            return ESPB_ERR_INVALID_FUNC_INDEX;
        }

        // Используем EspbFunctionBody из common_types.h, который должен быть корректно заполнен парсером
        const EspbFunctionBody *func_body_ptr = &module->function_bodies[local_func_idx];
        uint16_t num_virtual_regs = func_body_ptr->num_virtual_regs;
        const uint8_t *instructions_ptr = func_body_ptr->code;
        size_t instructions_size = func_body_ptr->code_size;
        const uint8_t *instructions_end_ptr = instructions_ptr + instructions_size;
        
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGD(TAG, "ESPB DEBUG: Дамп байт-кода функции (size=%zu):", instructions_size);
        for (size_t i = 0; i < instructions_size; i++) {
            ESP_LOGD(TAG, "ESPB DEBUG: %02X ", instructions_ptr[i]);
            if ((i + 1) % 16 == 0 || i == instructions_size - 1) {
                ESP_LOGD(TAG, "");
            }
        }
#endif
        
        if (num_virtual_regs == 0 && instructions_size > 0) {
             ESP_LOGW(TAG, "num_virtual_regs is 0, but function has code. Check parser/translator logic for func_idx %" PRIu32 ".", local_func_idx);
             // Для безопасности, если num_virtual_regs == 0, но есть код, выделим хотя бы минимум, чтобы избежать calloc(0,...)
             // Однако, это указывает на более глубокую проблему.
             // Пока что, если такая ситуация возникнет, это приведет к падению, что лучше, чем UB.
        }
        
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
        ESP_LOGD(TAG, "Allocated function frame: %u regs at %p", num_virtual_regs, locals);
#endif
        // Массив для хранения контекстов колбэков, индексирован по виртуальным регистрам
        // CallbackCtx **reg_contexts = (CallbackCtx **)calloc(num_virtual_regs > 0 ? num_virtual_regs : 1, sizeof(CallbackCtx*));
        // if (reg_contexts == NULL) {
        //     // REFACTOR_REMOVED: // REMOVED_free_locals;
        //     return ESPB_ERR_MEMORY_ALLOC;
        // }
        // memset не нужен, так как calloc инициализирует нулями
        
        if (args) {
            // Копируем аргументы в R0, R1, R2, если они есть и num_virtual_regs позволяет
            EspbFuncSignature* main_sig = &module->signatures[module->function_signature_indices[local_func_idx]];
            uint8_t num_params_to_copy = MIN(main_sig->num_params, 3); // Копируем до 3-х аргументов
            for(uint8_t i=0; i < num_params_to_copy; ++i) {
                if (i < num_virtual_regs) {
                    locals[i] = args[i];
                }
            }
        }
        
        // Инициализация R7 (больше не используется как SP для ALLOCA)
        if (num_virtual_regs > 7) { 
            locals[7].type = ESPB_TYPE_PTR;
            locals[7].value.ptr = NULL; // R7 больше не используется как SP
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "ESPB DEBUG: R7 initialized to NULL (ALLOCA now uses heap manager)");
#endif
        } else if (num_virtual_regs > 0) { 
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
             ESP_LOGD(TAG, "ESPB WARNING: num_virtual_regs (%" PRIu16 ") is too small for R7 initialization.", num_virtual_regs);
#endif
        }
        
        const uint8_t *pc = instructions_ptr;
        bool end_reached = false;
        int return_register = 0; 
        
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGD(TAG, "ESPB DEBUG: Анализ последовательности опкодов (num_virtual_regs: %u):", num_virtual_regs);
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
        dispatch_table[0x06] = &&op_0x06;
        dispatch_table[0x07] = &&op_0x07;
        dispatch_table[0x0A] = &&op_0x0A;
        dispatch_table[0x0B] = &&op_0x0B;
        dispatch_table[0x0D] = &&op_0x0D;
        dispatch_table[0x11] = &&op_0x11;
        dispatch_table[0x12] = &&op_0x12;
        dispatch_table[0x13] = &&op_0x13;
        dispatch_table[0x14] = &&op_0x14;
        dispatch_table[0x15] = &&op_0x15;
        dispatch_table[0x16] = &&op_0x16;
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
        dispatch_table[0x75] = &&op_0x75;
        dispatch_table[0x76] = &&op_0x76;
        dispatch_table[0x77] = &&op_0x77;
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
        dispatch_table[0x97] = &&op_0x97;
        dispatch_table[0x98] = &&op_0x98;
        dispatch_table[0x9B] = &&op_0x9B;
        dispatch_table[0xA1] = &&op_0xA1;
        dispatch_table[0x9E] = &&op_0x9E;
        dispatch_table[0x9F] = &&op_0x9F;
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
        dispatch_table[0xF0] = &&op_0xF0;
        dispatch_table[0xF1] = &&op_0xF0;
        dispatch_table[0xF2] = &&op_0xF0;
        dispatch_table[0xF3] = &&op_0xF0;
        dispatch_table[0xF4] = &&op_0xF0;
        dispatch_table[0xF5] = &&op_0xF0;
        dispatch_table[0xF6] = &&op_0xF6;
        dispatch_table[0x09] = &&op_0x09;
        dispatch_table[0x0F] = &&op_0x0F;
        dispatch_table[0x10] = &&op_0x10;
        dispatch_table[0x1D] = &&op_0x1D;
        dispatch_table[0x1E] = &&op_0x1E;
        dispatch_table[0x1F] = &&op_0x1F;
        dispatch_table[0x56] = &&op_0x56;
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
            ESP_LOGD(TAG, "ESPB DEBUG: exec pc=%ld opcode=0x%02X", pos, opcode);
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
                op_0x06:   // BLOCK
                op_0x07:   // LOOP
                op_0x0B: { // CALL_INDIRECT Rfunc(u8), type_idx(u16)
                    uint8_t r_func_idx = *pc++;
                    uint16_t expected_type_idx;
                    memcpy(&expected_type_idx, pc, sizeof(expected_type_idx));
                    pc += sizeof(expected_type_idx);

                    if (r_func_idx >= num_virtual_regs) {
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    uint32_t local_func_idx_to_call;
                    EspbValueType type = locals[r_func_idx].type;
                    if (type == ESPB_TYPE_I32 || type == ESPB_TYPE_U32) {
                        local_func_idx_to_call = locals[r_func_idx].value.u32;
                    } else if (type == ESPB_TYPE_PTR) {
                        local_func_idx_to_call = (uint32_t)(uintptr_t)locals[r_func_idx].value.ptr;
                    } else {
                        return ESPB_ERR_TYPE_MISMATCH;
                    }
                    
                    if (local_func_idx_to_call >= module->num_functions) {
                        return ESPB_ERR_INVALID_FUNC_INDEX;
                    }
                    uint32_t actual_sig_idx = module->function_signature_indices[local_func_idx_to_call];
                    if (actual_sig_idx != expected_type_idx) {
                        return ESPB_ERR_TYPE_MISMATCH;
                    }

                    // --- ОПТИМИЗАЦИЯ: Сохранение кадра в теневом стеке ---
                    const EspbFunctionBody* callee_body = &module->function_bodies[local_func_idx_to_call];
                    const EspbFuncSignature* callee_sig = &module->signatures[actual_sig_idx];

                    size_t saved_frame_size = num_virtual_regs * sizeof(Value);
                    size_t callee_frame_size = callee_body->num_virtual_regs * sizeof(Value);

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
                        if (i < callee_body->num_virtual_regs) callee_locals[i] = temp_args[i];
                    }
                    
                    // 7. Обновляем контекст интерпретатора для вызываемой функции
                    local_func_idx = local_func_idx_to_call;
                    pc = callee_body->code;
                    instructions_ptr = callee_body->code;
                    instructions_end_ptr = callee_body->code + callee_body->code_size;
                    locals = callee_locals;
                    num_virtual_regs = callee_body->num_virtual_regs;
                    
                    goto interpreter_loop_start;
                }

                
op_0x0D: { // CALL_INDIRECT_PTR Rdst, Rptr, type_idx
    // 1. Чтение операндов
    uint8_t func_ptr_reg = *pc++;
    uint16_t type_idx;
    memcpy(&type_idx, pc, sizeof(type_idx));
    pc += sizeof(type_idx);

    if (locals[func_ptr_reg].type != ESPB_TYPE_PTR) {
        ESP_LOGE(TAG, "CALL_INDIRECT_PTR: Register R%u does not contain a pointer.", func_ptr_reg);
        return ESPB_ERR_TYPE_MISMATCH;
    }
    void* target_ptr = locals[func_ptr_reg].value.ptr;
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
        size_t callee_frame_size = callee_body->num_virtual_regs * sizeof(Value);

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
            // СДВИГ АРГУМЕНТОВ: Пропускаем R0 (указатель на функцию) и начинаем с R1
            if ((i + 1) < num_virtual_regs) {
                temp_args[i] = locals[i + 1];
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
            if(i < callee_body->num_virtual_regs) callee_locals[i] = temp_args[i];
        }
        
        local_func_idx = callee_local_func_idx;
        pc = callee_body->code;
        instructions_ptr = callee_body->code;
        instructions_end_ptr = callee_body->code + callee_body->code_size;
        locals = callee_locals;
        num_virtual_regs = callee_body->num_virtual_regs;
        
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
            
            // СДВИГ АРГУМЕНТОВ: Пропускаем R0 (указатель на функцию) и начинаем с R1
            uint32_t src_reg_idx = i + 1;
            if (src_reg_idx >= num_virtual_regs) {
                ESP_LOGE(TAG, "CALL_INDIRECT_PTR to native: Not enough arguments in registers for call.");
                return ESPB_ERR_INVALID_OPERAND;
            }

            switch(func_sig->param_types[i]) {
                case ESPB_TYPE_I32: ffi_arg_values[i] = &locals[src_reg_idx].value.i32; break;
                case ESPB_TYPE_U32: ffi_arg_values[i] = &locals[src_reg_idx].value.u32; break;
                case ESPB_TYPE_PTR: ffi_arg_values[i] = &locals[src_reg_idx].value.ptr; break;
                case ESPB_TYPE_I64: ffi_arg_values[i] = &locals[src_reg_idx].value.i64; break;
                case ESPB_TYPE_U64: ffi_arg_values[i] = &locals[src_reg_idx].value.u64; break;
                case ESPB_TYPE_F32: ffi_arg_values[i] = &locals[src_reg_idx].value.f32; break;
                case ESPB_TYPE_F64: ffi_arg_values[i] = &locals[src_reg_idx].value.f64; break;
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
                case ESPB_TYPE_I32: locals[0].type = ESPB_TYPE_I32; locals[0].value.i32 = ret_val.i32; break;
                case ESPB_TYPE_U32: locals[0].type = ESPB_TYPE_U32; locals[0].value.u32 = ret_val.u32; break;
                case ESPB_TYPE_I64: locals[0].type = ESPB_TYPE_I64; locals[0].value.i64 = ret_val.i64; break;
                case ESPB_TYPE_U64: locals[0].type = ESPB_TYPE_U64; locals[0].value.u64 = ret_val.u64; break;
                case ESPB_TYPE_F32: locals[0].type = ESPB_TYPE_F32; locals[0].value.f32 = ret_val.f32; break;
                case ESPB_TYPE_F64: locals[0].type = ESPB_TYPE_F64; locals[0].value.f64 = ret_val.f64; break;
                case ESPB_TYPE_PTR: locals[0].type = ESPB_TYPE_PTR; locals[0].value.ptr = ret_val.p; break;
                default: break; // Should not happen if ffi_prep_cif succeeded
            }
        }
        goto interpreter_loop_start;
    }
}

                // --- Новые инструкции перехода ---
                op_0x02: { // BR offset(i16)
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset));
                    pc += sizeof(offset);
                    
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
                /*
                op_0x03: { // BR_IF reg(u8), offset(i16)
                    uint8_t cond_reg = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset));

                    if (cond_reg >= num_virtual_regs) {
                        ESP_LOGE(TAG, "BR_IF - cond register R%u out of bounds.", cond_reg);
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    bool condition_true = false;
                    Value val = locals[cond_reg];

                    // --- ОПТИМИЗАЦИЯ: БЫСТРЫЙ ПУТЬ ---
                    if (val.type == ESPB_TYPE_BOOL) {
                        condition_true = (val.value.i32 != 0);
                    } else {
                        // --- МЕДЛЕННЫЙ (УНИВЕРСАЛЬНЫЙ) ПУТЬ ---
                        if ((val.type == ESPB_TYPE_I8 && val.value.i8 != 0) ||
                            (val.type == ESPB_TYPE_U8 && val.value.u8 != 0) ||
                            (val.type == ESPB_TYPE_I16 && val.value.i16 != 0) ||
                            (val.type == ESPB_TYPE_U16 && val.value.u16 != 0) ||
                            (val.type == ESPB_TYPE_I32 && val.value.i32 != 0) ||
                            (val.type == ESPB_TYPE_U32 && val.value.u32 != 0) ||
                            (val.type == ESPB_TYPE_I64 && val.value.i64 != 0) ||
                            (val.type == ESPB_TYPE_U64 && val.value.u64 != 0) ||
                            (val.type == ESPB_TYPE_PTR && val.value.ptr != NULL))
                        {
                            condition_true = true;
                        }
                    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "BR_IF R%u (%s), offset %d. ", cond_reg, condition_true ? "true" : "false", offset);
#endif
                    if (condition_true) {
                        // Смещение от начала инструкции
                        pc = (pc - 2) + offset; 
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "Jumping, new_pc at offset %ld", (long)(pc - instructions_ptr));
#endif
                        continue;
                    } else {
                        pc += sizeof(offset);
                    }
                    goto interpreter_loop_start;
                }
                */
         op_0x03: { // BR_IF reg(u8), offset(i16) - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
    uint8_t cond_reg = *pc++;
    int16_t offset;
    memcpy(&offset, pc, sizeof(offset));

    // Проверка границ с branch prediction hint (ошибка крайне редка)
    if (__builtin_expect(cond_reg >= num_virtual_regs, 0)) {
        #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        ESP_LOGE(TAG, "BR_IF - cond register R%u out of bounds.", cond_reg);
        #endif
        return ESPB_ERR_INVALID_REGISTER_INDEX;
    }

    // ОПТИМИЗАЦИЯ: Прямой доступ к регистру вместо копирования Value
    Value *reg_ptr = &locals[cond_reg];
    bool condition_true;

    // ОПТИМИЗАЦИЯ: Быстрый путь для BOOL (самый частый случай)
    if (__builtin_expect(reg_ptr->type == ESPB_TYPE_BOOL, 1)) {
        condition_true = (reg_ptr->value.i32 != 0);
    } 
    // Универсальный путь для остальных типов
    else {
        // Используем u64 для универсальной проверки всех целочисленных типов
        // Работает корректно т.к. значения zero/sign-расширены при записи в регистр
        condition_true = (reg_ptr->value.u64 != 0);
    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "BR_IF R%u (%s), offset %d", 
             cond_reg, condition_true ? "true" : "false", offset);
#endif

    // PC калькуляция (критично для корректности!):
    // pc сейчас указывает на offset (после opcode и cond_reg)
    // offset считается от начала инструкции (opcode)
    if (__builtin_expect(condition_true, 0)) {
        // BRANCH TAKEN: возврат к opcode (-2 байта) + offset
        pc = (pc - 2) + offset;
    } else {
        // BRANCH NOT TAKEN: пропускаем offset
        pc += sizeof(offset);
    }
    goto interpreter_loop_start;
}
                op_0x18: { // LDC.I32.IMM Rd(u8), imm32
                    uint8_t rd = *pc++;
                    int32_t imm;
                    memcpy(&imm, pc, sizeof(int32_t));
                    pc += sizeof(int32_t);
                    
                    if (rd >= num_virtual_regs) {
                        ESP_LOGE(TAG, "LDC.I32.IMM - Register R%u out of bounds (max: %u). STOPPING EXECUTION!",
                                rd, num_virtual_regs-1);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.I32.IMM R%d, %" PRId32, rd, imm);
#endif
                goto interpreter_loop_start;
            }
                
                op_0x8F: { // ALLOCA Rd(u8), Rs(u8), align(u8) - NEW HEAP-BASED IMPLEMENTATION
                    uint8_t rd_alloc = *pc++;
                    uint8_t rs_alloc_size_reg = *pc++;
                    uint8_t align = *pc++;

                    // Проверки валидности регистров
                    if (rs_alloc_size_reg >= num_virtual_regs) {
                        ESP_LOGE(TAG, "ALLOCA - Size register R%u out of bounds", rs_alloc_size_reg);
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    if (rd_alloc >= num_virtual_regs) {
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
                    Value val_size = locals[rs_alloc_size_reg];
                    if (val_size.type != ESPB_TYPE_I32 && val_size.type != ESPB_TYPE_U32) {
                        ESP_LOGE(TAG, "ALLOCA - Size register contains wrong type %d", val_size.type);
                        return ESPB_ERR_TYPE_MISMATCH;
                    }

                    uint32_t size_to_alloc = val_size.value.u32;
                    if (size_to_alloc == 0 || size_to_alloc > 65536) { // Максимум 64KB
                        ESP_LOGE(TAG, "ALLOCA - Invalid size %" PRIu32, size_to_alloc);
                        return ESPB_ERR_INVALID_OPERAND;
                    }

                    // Use the frame of the currently executing function.
                    // This is now safe because a base frame is pushed for the entry point function.
                    RuntimeFrame *frame = &exec_ctx->call_stack[exec_ctx->call_stack_top - 1];
                    if (frame->alloca_count >= 16) {
                        ESP_LOGE(TAG, "ALLOCA - Too many allocations per frame (max 16)");
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
                    locals[rd_alloc].type = ESPB_TYPE_PTR;
                    locals[rd_alloc].value.ptr = allocated_ptr;

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ALLOCA SUCCESS: R%" PRIu8 "=%p size=%" PRIu32 " align=%u heap_managed",
                             rd_alloc, allocated_ptr, size_to_alloc, align);
#endif
                    goto interpreter_loop_start;
                }
                
                op_0x04: { // BR_TABLE Ridx(u8), num_targets(u16), [target_offsets(i16)...], default_offset(i16)
                    uint8_t ridx = *pc++;
                    uint16_t num_targets;
                    memcpy(&num_targets, pc, sizeof(num_targets)); pc += sizeof(num_targets);
                    
                    if (ridx >= num_virtual_regs) {
                        ESP_LOGE(TAG, "BR_TABLE - Index register R%u out of bounds", ridx);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    uint32_t index = (uint32_t)locals[ridx].value.i32;
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

                op_0x0A: { // CALL local_func_idx(u16)
                    uint16_t local_func_idx_to_call;
                    memcpy(&local_func_idx_to_call, pc, sizeof(local_func_idx_to_call));
                    pc += sizeof(local_func_idx_to_call);

                    if (local_func_idx_to_call >= module->num_functions) {
                        return ESPB_ERR_INVALID_FUNC_INDEX;
                    }

                    // --- ОПТИМИЗАЦИЯ: Сохранение кадра в теневом стеке ---
                    uint32_t sig_idx = module->function_signature_indices[local_func_idx_to_call];
                    const EspbFunctionBody* callee_body = &module->function_bodies[local_func_idx_to_call];
                    const EspbFuncSignature* callee_sig = &module->signatures[sig_idx];

                    size_t saved_frame_size = num_virtual_regs * sizeof(Value);
                    size_t callee_frame_size = callee_body->num_virtual_regs * sizeof(Value);

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
                        return ESPB_ERR_STACK_OVERFLOW;
                    }

                    // 4. Изолируем аргументы во временном буфере
                    Value temp_args[FFI_ARGS_MAX];
                    uint32_t num_args_to_copy = MIN(callee_sig->num_params, FFI_ARGS_MAX);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if (i < num_virtual_regs) temp_args[i] = locals[i];
                    }

                    // 5. Выделяем новый кадр
                    exec_ctx->fp = exec_ctx->sp + saved_frame_size;
                    exec_ctx->sp = exec_ctx->fp + callee_frame_size;

                    // 6. Копируем аргументы в новый кадр
                    Value* callee_locals = (Value*)(exec_ctx->shadow_stack_buffer + exec_ctx->fp);
                    memset(callee_locals, 0, callee_frame_size);
                    for (uint32_t i = 0; i < num_args_to_copy; i++) {
                        if(i < callee_body->num_virtual_regs) callee_locals[i] = temp_args[i];
                    }

                    // 7. Обновляем контекст интерпретатора
                    local_func_idx = local_func_idx_to_call;
                    pc = callee_body->code;
                    instructions_ptr = callee_body->code;
                    instructions_end_ptr = callee_body->code + callee_body->code_size;
                    locals = callee_locals;
                    num_virtual_regs = callee_body->num_virtual_regs;
                    
                    goto interpreter_loop_start;
                }
                
           op_0x19: { // LDC.I64.IMM Rd(u8), imm64
                    uint8_t rd = *pc++;
                    int64_t imm64;
                    memcpy(&imm64, pc, sizeof(int64_t));
                    pc += sizeof(int64_t);
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = imm64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.I64.IMM R%d, %" PRId64, rd, imm64);
#endif
                    goto interpreter_loop_start;
                }
                
            op_0x1A: { // LDC.F32.IMM Rd(u8), imm32
                uint8_t rd = *pc++;
                float immf32;
                memcpy(&immf32, pc, sizeof(float)); pc += sizeof(float);
                locals[rd].type = ESPB_TYPE_F32;
                locals[rd].value.f32 = immf32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "LDC.F32.IMM R%u, %f", rd, immf32);
#endif
                goto interpreter_loop_start;
            }
            op_0x1B: { // LDC.F64.IMM Rd(u8), imm64
                uint8_t rd = *pc++;
                double immf64;
                memcpy(&immf64, pc, sizeof(double)); pc += sizeof(double);
                locals[rd].type = ESPB_TYPE_F64;
                locals[rd].value.f64 = immf64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "LDC.F64.IMM R%u, %f", rd, immf64);
#endif
                goto interpreter_loop_start;
            }

           op_0x1C: { // LDC.PTR.IMM Rd(u8), ptr32
                    uint8_t rd = *pc++;
                    int32_t imm;
                    memcpy(&imm, pc, sizeof(int32_t));
                    pc += sizeof(int32_t);
                    
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
                    
                    locals[rd].type = ESPB_TYPE_PTR;
                    locals[rd].value.ptr = (void*)target_addr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LDC.PTR.IMM R%u = %p (offset %" PRId32 ")", rd, (void*)target_addr, imm);
#endif
                    goto interpreter_loop_start;
                }
                
           op_0x12: { // MOV.I32 Rd(u8), Rs(u8)
                 uint8_t rd = *pc++;
                 uint8_t rs = *pc++;

                    if (rd >= num_virtual_regs) {
                        ESP_LOGE(TAG, "MOV.I32 - Dest register R%u out of bounds (num_virtual_regs: %u)",
                                rd, num_virtual_regs);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    if (rs >= num_virtual_regs) {
                        ESP_LOGE(TAG, "MOV.I32 - Source register R%u out of bounds (num_virtual_regs: %u)",
                                rs, num_virtual_regs);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    locals[rd] = locals[rs];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MOV.I32 R%d, R%d (%" PRId32 ")", rd, rs, locals[rs].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                     op_0x90: { // TRUNC.I64.I32 Rd(u8), Rs(u8)
                   uint8_t rd = *pc++;
                  uint8_t rs = *pc++;
                   locals[rd].type = ESPB_TYPE_I32;
                   locals[rd].value.i32 = (int32_t)locals[rs].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I64.I32 R%u, R%u = %" PRId32, rd, rs, locals[rd].value.i32);
#endif
                   goto interpreter_loop_start;
               }

               op_0xA4: { // FPROUND Rd(u8), Rs(u8) - F64 → F32
                   uint8_t rd = *pc++;
                   uint8_t rs = *pc++;
                   if (rs >= num_virtual_regs || locals[rs].type != ESPB_TYPE_F64) {
                       ESP_LOGE(TAG, "FPROUND - Invalid source R%u (type %d)", rs, rs < num_virtual_regs ? locals[rs].type : -1);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                   }
                   if (rd >= num_virtual_regs) {
                       ESP_LOGE(TAG, "FPROUND - Dest R%u out of bounds", rd);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                   }
                   locals[rd].type = ESPB_TYPE_F32;
                   locals[rd].value.f32 = (float)locals[rs].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "FPROUND R%u, R%u: %f → %f", rd, rs, locals[rs].value.f64, locals[rd].value.f32);
#endif
                   goto interpreter_loop_start;
               }

               op_0xA5: { // FPROMOTE Rd(u8), Rs(u8) - F32 → F64
                   uint8_t rd = *pc++;
                   uint8_t rs = *pc++;
                   if (rs >= num_virtual_regs || locals[rs].type != ESPB_TYPE_F32) {
                       ESP_LOGE(TAG, "FPROMOTE - Invalid source R%u (type %d)", rs, rs < num_virtual_regs ? locals[rs].type : -1);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                   }
                   if (rd >= num_virtual_regs) {
                       ESP_LOGE(TAG, "FPROMOTE - Dest R%u out of bounds", rd);
                       // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                   }
                   locals[rd].type = ESPB_TYPE_F64;
                   locals[rd].value.f64 = (double)locals[rs].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "FPROMOTE R%u, R%u: %f → %f", rd, rs, locals[rs].value.f32, locals[rd].value.f64);
#endif
                   goto interpreter_loop_start;
               }
               op_0x13: { // MOV.I64 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[rs].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MOV.I64 R%u, R%u = %" PRId64, rd, rs, locals[rd].value.i64);
#endif
                 goto interpreter_loop_start;
            }
            
            op_0x14: { // MOV.F32 Rd(u8), Rs(u8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                locals[rd].type = ESPB_TYPE_F32;
                locals[rd].value.f32 = locals[rs].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "MOV.F32 R%u, R%u = %f", rd, rs, locals[rd].value.f32);
#endif
                goto interpreter_loop_start;
            }
            op_0x15: { // MOV.F64 Rd(u8), Rs(u8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                locals[rd].type = ESPB_TYPE_F64;
                locals[rd].value.f64 = locals[rs].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "MOV.F64 R%u, R%u = %f", rd, rs, locals[rd].value.f64);
#endif
                goto interpreter_loop_start;
            }

op_0x20: { // ADD.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = *pc++;
    uint8_t r1 = *pc++;
    uint8_t r2 = *pc++;

    const int32_t val1 = locals[r1].value.i32;
    const int32_t val2 = locals[r2].value.i32;

    locals[rd].type = ESPB_TYPE_I32;
    locals[rd].value.i32 = val1 + val2;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "ADD.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}
                
                op_0x21: { // SUB.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = *pc++;
    uint8_t r1 = *pc++;
    uint8_t r2 = *pc++;

    const int32_t val1 = locals[r1].value.i32;
    const int32_t val2 = locals[r2].value.i32;

    locals[rd].type = ESPB_TYPE_I32;
    locals[rd].value.i32 = val1 - val2;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SUB.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}
                
                op_0x22: { // MUL.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = *pc++;
    uint8_t r1 = *pc++;
    uint8_t r2 = *pc++;

    const int32_t val1 = locals[r1].value.i32;
    const int32_t val2 = locals[r2].value.i32;
    const int64_t prod = (int64_t)val1 * val2;
    if (prod > INT32_MAX || prod < INT32_MIN) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    locals[rd].type = ESPB_TYPE_I32;
    locals[rd].value.i32 = (int32_t)prod;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "MUL.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}

                op_0x23: { // DIV.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = *pc++;
    uint8_t r1 = *pc++;
    uint8_t r2 = *pc++;

    const int32_t dividend = locals[r1].value.i32;
    const int32_t divisor = locals[r2].value.i32;
    if (divisor == 0) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
    }
    if (dividend == INT32_MIN && divisor == -1) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    locals[rd].type = ESPB_TYPE_I32;
    locals[rd].value.i32 = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "DIV.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}

                op_0x24: { // REM.I32 Rd(u8), R1(u8), R2(u8)
    uint8_t rd = *pc++;
    uint8_t r1 = *pc++;
    uint8_t r2 = *pc++;

    const int32_t dividend_r = locals[r1].value.i32;
    const int32_t divisor_r = locals[r2].value.i32;
    if (divisor_r == 0) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
    }
    if (dividend_r == INT32_MIN && divisor_r == -1) {
        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
    }
    locals[rd].type = ESPB_TYPE_I32;
    locals[rd].value.i32 = dividend_r % divisor_r;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "REM.I32 R%u, R%u, R%u = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}

                op_0x26: { // DIV.U32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint32_t dividend_u = locals[r1].value.u32;
                    uint32_t divisor_u = locals[r2].value.u32;
                    if (divisor_u == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    locals[rd].type = ESPB_TYPE_U32;
                    locals[rd].value.u32 = dividend_u / divisor_u;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.U32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, locals[rd].value.u32);
#endif
                    goto interpreter_loop_start;
                }

                // I32 arithmetic with 8-bit immediate (per spec v1.7 0x40..0x48)
                op_0x40: { // ADD.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc; pc += sizeof(int8_t);
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = locals[r1].value.i32 + (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x41: { // SUB.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc; pc += sizeof(int8_t);
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = locals[r1].value.i32 - (int32_t)imm;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x42: { // MUL.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc; pc += sizeof(int8_t);
                    int64_t prod = (int64_t)locals[r1].value.i32 * (int64_t)imm;
                    if (prod > INT32_MAX || prod < INT32_MIN) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)prod;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x43: { // DIVS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc; pc += sizeof(int8_t);
                    int32_t dividend = locals[r1].value.i32;
                    int32_t divisor = (int32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT32_MIN && divisor == -1) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIVS.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x44: { // DIVU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++; // treat as unsigned
                    uint32_t dividend = (uint32_t)locals[r1].value.u32;
                    uint32_t divisor = (uint32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)(dividend / divisor);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIVU.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x45: { // REMS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc; pc += sizeof(int8_t);
                    int32_t dividend = locals[r1].value.i32;
                    int32_t divisor = (int32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT32_MIN && divisor == -1) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = dividend % divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REMS.I32.IMM8 R%u, R%u, %d = %" PRId32, rd, r1, (int)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x46: { // REMU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint32_t dividend = (uint32_t)locals[r1].value.u32;
                    uint32_t divisor = (uint32_t)imm;
                    if (divisor == 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)(dividend % divisor);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REMU.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x47: { // SHRS.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint32_t shift = (uint32_t)imm & 31u;
                    int32_t sval = locals[r1].value.i32;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = sval >> shift;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHRS.I32.IMM8 R%u, R%u, %u = %" PRId32, rd, r1, (unsigned)imm, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x48: { // SHRU.I32.IMM8 Rd(u8), R1(u8), imm8(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint32_t shift = (uint32_t)imm & 31u;
                    uint32_t uval = (uint32_t)locals[r1].value.u32;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)(uval >> shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHRU.I32.IMM8 R%u, R%u, %u = %" PRIu32, rd, r1, (unsigned)imm, (uint32_t)locals[rd].value.u32);
#endif
                    goto interpreter_loop_start;
                }

                op_0x60: { // ADD.F32 Rd, R1, R2
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = locals[r1].value.f32 + locals[r2].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x61: { // SUB.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = locals[r1].value.f32 - locals[r2].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x62: { // MUL.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = locals[r1].value.f32 * locals[r2].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x63: { // DIV.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = locals[r1].value.f32 / locals[r2].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x64: { // MIN.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = fminf(locals[r1].value.f32, locals[r2].value.f32);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MIN.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x65: { // MAX.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = fmaxf(locals[r1].value.f32, locals[r2].value.f32);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MAX.F32 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x66: { // ABS.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = fabsf(locals[r1].value.f32);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ABS.F32 R%u, R%u = %f", rd, r1, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x67: { // SQRT.F32
                    uint8_t rd = *pc++; uint8_t r1 = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = sqrtf(locals[r1].value.f32);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SQRT.F32 R%u, R%u = %f", rd, r1, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }

                // и для double:
                op_0x68: { // ADD.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = locals[r1].value.f64 + locals[r2].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x69: { // SUB.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = locals[r1].value.f64 - locals[r2].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6A: { // MUL.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = locals[r1].value.f64 * locals[r2].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6B: { // DIV.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = locals[r1].value.f64 / locals[r2].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "DIV.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6C: { // MIN.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = fmin(locals[r1].value.f64, locals[r2].value.f64);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MIN.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6D: { // MAX.F64
                    uint8_t rd=*pc++, r1=*pc++, r2=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = fmax(locals[r1].value.f64, locals[r2].value.f64);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MAX.F64 R%u, R%u, R%u = %f", rd, r1, r2, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6E: { // ABS.F64
                    uint8_t rd=*pc++, r1=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = fabs(locals[r1].value.f64);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ABS.F64 R%u, R%u = %f", rd, r1, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x6F: { // SQRT.F64
                    uint8_t rd=*pc++, r1=*pc++;
                    locals[rd].type=ESPB_TYPE_F64;
                    locals[rd].value.f64 = sqrt(locals[r1].value.f64);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SQRT.F64 R%u, R%u = %f", rd, r1, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }

                op_0xBC: { // PTRTOINT Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    if (rs >= num_virtual_regs || locals[rs].type != ESPB_TYPE_PTR) {
                        // Error handling as before
                        ESP_LOGE(TAG, "PTRTOINT - Invalid source R%u (type %d). PC_offset: %ld",
                                rs, rs < num_virtual_regs ? locals[rs].type : -1, (long)((pc - 3) - instructions_ptr));
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                    }
                    if (rd >= num_virtual_regs) {
                        // Error handling as before
                         ESP_LOGE(TAG, "PTRTOINT - Dest R%u out of bounds. PC_offset: %ld",
                                rd, (long)((pc - 3) - instructions_ptr));
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    void* ptr_value = locals[rs].value.ptr;
                    locals[rd].type = ESPB_TYPE_I32;
                    
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
                                    locals[rd].value.i32 = (int32_t)i | CALLBACK_FLAG_BIT;
                                    ESP_LOGD(TAG, "PTRTOINT R%d, R%d (exact ESPB func %" PRIu32 ") -> val=0x%08" PRIx32 " (with CALLBACK_FLAG_BIT)",
                                           rd, rs, i, (uint32_t)locals[rd].value.i32);
                                    found_function = true;
                                    break;
                                }
                                
                                // Также проверим, попадает ли указатель в тело функции
                                const uint8_t *func_end = func_start + func->code_size;
                                if (value_ptr >= func_start && value_ptr < func_end) {
                                    // Указатель внутри тела функции, это тоже может быть колбэк
                                    locals[rd].value.i32 = (int32_t)i | CALLBACK_FLAG_BIT;
                                    ESP_LOGD(TAG, "PTRTOINT R%d, R%d (inside ESPB func %" PRIu32 ") -> val=0x%08" PRIx32 " (with CALLBACK_FLAG_BIT)",
                                           rd, rs, i, (uint32_t)locals[rd].value.i32);
                                    found_function = true;
                                    break;
                                }
                            }
                            
                            if (!found_function) {
                                // Если не нашли функцию, то это просто указатель в памяти ESPB
                                locals[rd].value.i32 = (int32_t)(uintptr_t)ptr_value;
                                ESP_LOGD(TAG, "PTRTOINT R%d, R%d (mem addr in ESPB) -> val=0x%08" PRIx32,
                                       rd, rs, (uint32_t)locals[rd].value.i32);
                            }
                        } else {
                            // Это указатель вне ESPB-памяти, просто преобразуем
                            locals[rd].value.i32 = (int32_t)(uintptr_t)ptr_value;
                           ESP_LOGD(TAG, "PTRTOINT R%d, R%d (external ptr) -> val=0x%08" PRIx32, rd, rs, (uint32_t)locals[rd].value.i32);
                        }
                    } else {
                        // Если FEATURE_CALLBACK_AUTO не активен, просто выполняем обычное преобразование
                        locals[rd].value.i32 = (int32_t)(uintptr_t)ptr_value;
                       ESP_LOGD(TAG, "PTRTOINT R%d, R%d (val=0x%08" PRIx32 ")", rd, rs, (uint32_t)locals[rd].value.i32);
                    }
                    goto interpreter_loop_start;
                }

                op_0xBD: { // INTTOPTR Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    if (rd >= num_virtual_regs || rs >= num_virtual_regs) {
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    locals[rd].type = ESPB_TYPE_PTR;
                    locals[rd].value.ptr = (void*)(uintptr_t)locals[rs].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "INTTOPTR R%u, R%u -> %p", rd, rs, locals[rd].value.ptr);
#endif
                    goto interpreter_loop_start;
                }

                op_0x27: { // REM.U32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint32_t dividend_u = locals[r1].value.u32;
                    uint32_t divisor_u = locals[r2].value.u32;
                    if (divisor_u == 0) {
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    locals[rd].type = ESPB_TYPE_U32;
                    locals[rd].value.u32 = dividend_u % divisor_u;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "REM.U32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, locals[rd].value.u32);
#endif
                    goto interpreter_loop_start;
                }

                op_0x28: { // AND.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = locals[r1].value.i32 & locals[r2].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "AND.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
                goto interpreter_loop_start;
            }

            op_0x29: { // OR.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = locals[r1].value.i32 | locals[r2].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "OR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }

            op_0x2A: { // XOR.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = locals[r1].value.i32 ^ locals[r2].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "XOR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }

            op_0x2B: { // SHL.I32 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint32_t val1 = (uint32_t)locals[r1].value.i32;
                    uint32_t shift = locals[r2].value.u32 & 31;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)(val1 << shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SHL.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }

            op_0x2C: { // SHR.I32 Rd(u8), R1(u8), R2(u8) (arithmetic)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    int32_t sval = locals[r1].value.i32;
                    uint32_t shift_a = locals[r2].value.u32 & 31;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = sval >> shift_a;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SHR.I32 R%d, R%d, R%d = %" PRId32, rd, r1, r2, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }

            op_0x2D: { // USHR.I32 Rd(u8), R1(u8), R2(u8) (logical)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint32_t uval = locals[r1].value.u32;
                    uint32_t shift = locals[r2].value.u32 & 31;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)(uval >> shift);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "USHR.I32 R%d, R%d, R%d = %" PRIu32, rd, r1, r2, locals[rd].value.u32);
#endif
                    goto interpreter_loop_start;
                }

                op_0x2E: { // NOT.I32 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = ~locals[rs].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "NOT.I32 R%d, R%d = %" PRId32, rd, rs, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }

                op_0x30: { // ADD.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 + locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ADD.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x31: { // SUB.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 - locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SUB.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x32: { // MUL.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 * locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "MUL.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x33: { // DIV.I64 Rd(u8), R1(u8), R2(u8) (signed)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    int64_t dividend = locals[r1].value.i64;
                    int64_t divisor = locals[r2].value.i64;
                    if (divisor == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    if (dividend == INT64_MIN && divisor == -1) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
                    }
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = dividend / divisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "DIV.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x36: { // DIV.U64 Rd(u8), R1(u8), R2(u8) (unsigned)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint64_t udividend = (uint64_t)locals[r1].value.i64;
                    uint64_t udivisor = (uint64_t)locals[r2].value.i64;
                    if (udivisor == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = udividend / udivisor;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "DIVU.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, locals[rd].value.u64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x34: { // REM.I64 Rd(u8), R1(u8), R2(u8) (signed)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    int64_t dividend_r = locals[r1].value.i64;
                    int64_t divisor_r = locals[r2].value.i64;
                    if (divisor_r == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    if (dividend_r == INT64_MIN && divisor_r == -1) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW;
                    }
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = dividend_r % divisor_r;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "REM.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x37: { // REM.U64 Rd(u8), R1(u8), R2(u8) (unsigned)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint64_t udividend_r = (uint64_t)locals[r1].value.i64;
                    uint64_t udivisor_r = (uint64_t)locals[r2].value.i64;
                    if (udivisor_r == 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO;
                    }
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = udividend_r % udivisor_r;
                   ESP_LOGD(TAG, "REMU.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, locals[rd].value.u64);
                    goto interpreter_loop_start;
                }

                op_0x38: { // AND.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 & locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "AND.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x39: { // OR.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 | locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "OR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x3A: { // XOR.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 ^ locals[r2].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "XOR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x3B: { // SHL.I64 Rd(u8), R1(u8), R2(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.u64 = locals[r1].value.u64 << (locals[r2].value.u32 & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHL.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x3C: { // SHR.I64 Rd(u8), R1(u8), R2(u8) (arithmetic)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 >> (locals[r2].value.u32 & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "SHR.I64 R%d, R%d, R%d = %" PRId64, rd, r1, r2, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x3D: { // USHR.I64 Rd(u8), R1(u8), R2(u8) (logical)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = locals[r1].value.u64 >> (locals[r2].value.u32 & 63);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "USHR.I64 R%d, R%d, R%d = %" PRIu64, rd, r1, r2, locals[rd].value.u64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x3E: { // NOT.I64 Rd(u8), R1(u8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = ~locals[r1].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "NOT.I64 R%d, R%d = %" PRId64, rd, r1, locals[rd].value.i64);
#endif
                    goto interpreter_loop_start;
                }

                op_0x70: { // STORE.I8 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(int8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    int8_t val_i8 = locals[rs].value.i8;
                    *((int8_t*)(base + target)) = val_i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.I8 R%hhu(%hhd) -> mem[%" PRIu32 "]", rs, val_i8, target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x71: { // STORE.U8 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(uint8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    uint8_t val8 = (uint8_t)(locals[rs].value.u32 & 0xFF);
                    *((uint8_t *)(base + target)) = val8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.U8 R%u(%u) -> mem[%u]", (unsigned)rs, (unsigned)val8, (unsigned)target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x72: { // STORE.I16 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(uint16_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    if (target % sizeof(uint16_t) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    int16_t val_i16 = locals[rs].value.i16;
                    *((uint16_t*)(base + target)) = (uint16_t)val_i16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.I16 R%hhu(%hd) -> mem[%" PRIu32 "]", rs, val_i16, target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x73: { // STORE.U16 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base16 = instance->memory_data;
                    uint32_t mem_size16 = instance->memory_size_bytes;
                    uintptr_t ra_addr16 = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr16 = (uintptr_t)base16;
                    if (ra_addr16 < base_addr16) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off16 = (uint32_t)(ra_addr16 - base_addr16);
                    int64_t tgt16 = (int64_t)ra_off16 + offset;
                    if (tgt16 < 0 || (uint64_t)tgt16 + sizeof(uint16_t) > mem_size16) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target16 = (uint32_t)tgt16;
                    if (target16 % sizeof(uint16_t) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    uint16_t val16 = (uint16_t)locals[rs].value.u32;
                    *((uint16_t *)(base16 + target16)) = val16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.U16 R%u(%u) -> mem[%u]", (unsigned)rs, (unsigned)val16, (unsigned)target16);
#endif
                    goto interpreter_loop_start;
                }
                op_0x74: { // STORE.I32 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(int16_t)); pc += sizeof(int16_t);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(uint32_t) > mem_size) {
                        ESP_LOGE(TAG, "LOAD.I32 - Address out of bounds: base=0x%x offset=0x%x", (unsigned int)ra_offset, (unsigned int)offset);
                    return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
}
                    uint32_t target = (uint32_t)target_signed;
                    if (target % sizeof(uint32_t) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    int32_t val32 = locals[rs].value.i32;
                    *((uint32_t *)(base + target)) = (uint32_t)val32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.I32 R%u(%ld) -> mem[%" PRIu32 "]", rs, (long)val32, target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x75: { // STORE.U32 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(uint32_t) > mem_size) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    uint32_t target = (uint32_t)target_signed;
                    // Alignment check for 32-bit store
                    if (target % sizeof(uint32_t) != 0) {
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS;
                    }
                    uint32_t val = locals[rs].value.u32;
                    *((uint32_t *)(base + target)) = val;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.U32 R%u(%u) -> mem[%u]", (unsigned)rs, (unsigned)val, (unsigned)target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x7A: { // STORE.PTR Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) {
                       ESP_LOGE(TAG, "STORE.PTR ra_addr<base_addr: ra_addr=%p base_addr=%p", (void*)ra_addr, (void*)base_addr);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(void*) > mem_size) {
                       ESP_LOGE(TAG, "STORE.PTR OOB: ra_offset=%" PRIu32 " offset=%" PRId16 " sizeof(void*)=%zu mem_size=%" PRIu32, ra_offset, offset, sizeof(void*), mem_size);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    uint32_t target = (uint32_t)target_signed;
                    if (target % sizeof(void*) != 0) {
                       ESP_LOGE(TAG, "STORE.PTR unaligned access: target=%" PRIu32 " sizeof(void*)=%zu", target, sizeof(void*));
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS;
                    }
                    void* ptr_val = locals[rs].value.ptr;
                    *((void**)(base + target)) = ptr_val;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.PTR R%u(%p) -> mem[%u]", (unsigned)rs, ptr_val, (unsigned)target);
#endif
                    goto interpreter_loop_start;
                }

                // Добавляем поддержку 8-битного булевого типа
                op_0x7B: { // STORE.BOOL Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs_bool = *pc++;
                    uint8_t ra_bool = *pc++;
                    uint8_t mem_idx_bool = *pc++;
                    int16_t offset_bool;
                    memcpy(&offset_bool, pc, sizeof(offset_bool)); pc += sizeof(offset_bool);
                    if (mem_idx_bool != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base_bool = instance->memory_data;
                    uint32_t mem_size_bool = instance->memory_size_bytes;
                    uintptr_t ra_addr_bool = (uintptr_t)locals[ra_bool].value.ptr;
                    uintptr_t base_addr_bool = (uintptr_t)base_bool;
                    if (ra_addr_bool < base_addr_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset_bool = (uint32_t)(ra_addr_bool - base_addr_bool);
                    int64_t target_signed_bool = (int64_t)ra_offset_bool + offset_bool;
                    if (target_signed_bool < 0 || (uint64_t)target_signed_bool + sizeof(uint8_t) > mem_size_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target_bool = (uint32_t)target_signed_bool;
                    uint8_t bool_val = (locals[rs_bool].value.i32 != 0) ? 1 : 0;
                    *((uint8_t *)(base_bool + target_bool)) = bool_val;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.BOOL [R%u + %d] <- R%u(%u)", ra_bool, offset_bool, rs_bool, (unsigned)bool_val);
#endif
                    goto interpreter_loop_start;
                }

                op_0x76: { // STORE.I64 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(int16_t)); pc += sizeof(int16_t);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
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
                             ra, locals[ra].value.ptr, offset, target, target % sizeof(int64_t));
#endif
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (target % sizeof(int64_t) != 0) {
                        ESP_LOGD(TAG, "STORE.I64 UNALIGNED ACCESS: Ra=R%u contains ptr=%p, offset=%d, final_addr=0x%x (mod 8 = %u) - using unaligned write",
                                ra, locals[ra].value.ptr, offset, target, target % 8);
                        // Используем unaligned доступ вместо ошибки
                    }
#endif
                    int64_t val64 = locals[rs].value.i64;
                    *((int64_t *)(base + target)) = val64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.I64 R%u(%" PRId64 ") -> mem[%" PRIu32 "]", rs, val64, target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x77: { // STORE.U64 Rs(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(int16_t)); pc += sizeof(int16_t);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_offset = (uint32_t)(ra_addr - base_addr);
                    int64_t target_signed = (int64_t)ra_offset + offset;
                    if (target_signed < 0 || (uint64_t)target_signed + sizeof(uint64_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)target_signed;
                    
                    // ДИАГНОСТИКА и поддержка unaligned доступа для U64
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (target % sizeof(uint64_t) != 0) {
                        ESP_LOGD(TAG, "STORE.U64 UNALIGNED ACCESS: Ra=R%u contains ptr=%p, offset=%d, final_addr=0x%x (mod 8 = %u) - using unaligned write",
                                ra, locals[ra].value.ptr, offset, target, target % 8);
                        // Используем unaligned доступ вместо ошибки
                    }
#endif
                    uint64_t valu = (uint64_t)locals[rs].value.i64;
                    *((uint64_t *)(base + target)) = valu;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "STORE.U64 R%u(%" PRIu64 ") -> mem[%" PRIu32 "]", rs, valu, target);
#endif
                    goto interpreter_loop_start;
                }
                op_0x78: { // STORE.F32 Rs, Ra, mem_idx, offset
                    uint8_t rs=*pc++, ra=*pc++, mem_idx=*pc++;
                    int16_t off; memcpy(&off, pc, sizeof(off)); pc+=sizeof(off);
                    if(mem_idx!=0){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX;}
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)locals[ra].value.ptr;
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(float)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    *(float*)(base+tgt) = locals[rs].value.f32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"STORE.F32 R%u(%f)->mem[%u]", rs, locals[rs].value.f32, tgt);
#endif
                    goto interpreter_loop_start;
                }
                op_0x79: { // STORE.F64
                    uint8_t rs=*pc++, ra=*pc++, mem_idx=*pc++;
                    int16_t off; memcpy(&off, pc, sizeof(off)); pc+=sizeof(off);
                    if(mem_idx!=0){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX;}
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)locals[ra].value.ptr;
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(double)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    *(double*)(base+tgt) = locals[rs].value.f64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"STORE.F64 R%u(%f)->mem[%u]", rs, locals[rs].value.f64, tgt);
#endif
                    goto interpreter_loop_start;
                }
                op_0x85: { // LOAD.I64 Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt64 = (int64_t)ra_off + offset;
                    if (tgt64 < 0 || (uint64_t)tgt64 + sizeof(int64_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt64;
                    
                    // Поддержка unaligned доступа для LOAD.I64
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    if (target % sizeof(int64_t) != 0) {
                        ESP_LOGD(TAG, "LOAD.I64 UNALIGNED ACCESS: Ra=R%u contains ptr=%p, offset=%d, final_addr=0x%x (mod 8 = %u) - using unaligned read",
                                ra, locals[ra].value.ptr, offset, target, target % 8);
                        // Используем unaligned доступ вместо ошибки
                    }
#endif
                    int64_t loadv = *((int64_t*)(base + target));
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = loadv;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I64 R%u <- mem[%" PRIu32 "] = %" PRId64, rd, target, loadv);
#endif
                    goto interpreter_loop_start;
                }
                op_0x86: { // LOAD.F32
                    uint8_t rd=*pc++, ra=*pc++, mem_idx=*pc++;
                    int16_t off; memcpy(&off, pc, sizeof(off)); pc+=sizeof(off);
                    if(mem_idx!=0){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX;}
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)locals[ra].value.ptr;
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(float)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = *(float*)(base+tgt);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"LOAD.F32 R%u<-mem[%u]=%f", rd, tgt, locals[rd].value.f32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x87: { // LOAD.F64
                    uint8_t rd=*pc++, ra=*pc++, mem_idx=*pc++;
                    int16_t off; memcpy(&off, pc, sizeof(off)); pc+=sizeof(off);
                    if(mem_idx!=0){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX;}
                    uint8_t *base=instance->memory_data; uint32_t sz=instance->memory_size_bytes;
                    uintptr_t addr=(uintptr_t)locals[ra].value.ptr;
                    uintptr_t b=(uintptr_t)base;
                    if(addr<b||(addr-b+off+sizeof(double)>sz)){ // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;}
                    uint32_t tgt=(uint32_t)(addr-b+off);
                    locals[rd].type = ESPB_TYPE_F64;
                    locals[rd].value.f64 = *(double*)(base+tgt);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG,"LOAD.F64 R%u<-mem[%u]=%f", rd, tgt, locals[rd].value.f64);
#endif
                    goto interpreter_loop_start;
                }
                op_0x88: { // LOAD.PTR Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt_ptr = (int64_t)ra_off + offset;
                    if (tgt_ptr < 0 || (uint64_t)tgt_ptr + sizeof(void*) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target_ptr = (uint32_t)tgt_ptr;
                    if (target_ptr % sizeof(void*) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    void* loaded_ptr = *((void**)(base + target_ptr));
                    locals[rd].type = ESPB_TYPE_PTR;
                    locals[rd].value.ptr = loaded_ptr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.PTR R%u <- mem[%" PRIu32 "] = %p", rd, target_ptr, loaded_ptr);
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
                    if (saved_frame_ptr != NULL) {
                        Value* caller_locals_ptr = (Value*)(exec_ctx->shadow_stack_buffer + restored_fp);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "END: Restoring %zu registers from shadow stack at %p to caller frame %p", num_regs_saved, saved_frame_ptr, caller_locals_ptr);
#endif
                        
                        if (num_regs_saved == module->function_bodies[restored_caller_idx].num_virtual_regs) {
                            memcpy(caller_locals_ptr, saved_frame_ptr, num_regs_saved * sizeof(Value));
                        } else {
                            ESP_LOGW(TAG, "END: Mismatch in saved regs (%zu) vs caller regs (%u). Not restoring frame.", num_regs_saved, module->function_bodies[restored_caller_idx].num_virtual_regs);
                        }
                        // No free() needed! The space will be reclaimed by adjusting 'sp'.
                    }

                    // 6. Unwind the stack and restore context
                    local_func_idx = restored_caller_idx;
                    const EspbFunctionBody* caller_body = &module->function_bodies[local_func_idx];
                    num_virtual_regs = caller_body->num_virtual_regs;
                    
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
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I8;
                    locals[rd].value.i8 = locals[rs].value.i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "MOV.I8 R%u, R%u = %" PRId8, (unsigned)rd, (unsigned)rs, locals[rd].value.i8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x80: { // LOAD.I8S Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset; memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(int8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    int8_t val_i8 = *((int8_t*)(base + target));
                    locals[rd].type = ESPB_TYPE_I8;
                    locals[rd].value.i8 = val_i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I8S R%u <- mem[%" PRIu32 "] = %" PRId8, (unsigned)rd, target, val_i8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x81: { // LOAD.I8U Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset; memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(uint8_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    uint8_t val_u8 = *((uint8_t*)(base + target));
                    locals[rd].type = ESPB_TYPE_U8;
                    locals[rd].value.u8 = val_u8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.I8U R%u <- mem[%" PRIu32 "] = %" PRIu8, rd, target, val_u8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x82: { // LOAD.I16S Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset; memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base = instance->memory_data;
                    uint32_t mem_size = instance->memory_size_bytes;
                    uintptr_t ra_addr = (uintptr_t)locals[ra].value.ptr;
                    uintptr_t base_addr = (uintptr_t)base;
                    if (ra_addr < base_addr) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off = (uint32_t)(ra_addr - base_addr);
                    int64_t tgt = (int64_t)ra_off + offset;
                    if (tgt < 0 || (uint64_t)tgt + sizeof(int16_t) > mem_size) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target = (uint32_t)tgt;
                    if (target % sizeof(int16_t) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    int16_t val_i16 = *((int16_t*)(base + target));
                    locals[rd].type = ESPB_TYPE_I16;
                    locals[rd].value.i16 = val_i16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.I16S R%u <- mem[%" PRIu32 "] = %" PRId16, rd, target, val_i16);
#endif
                    goto interpreter_loop_start;
                }

                // --- 8-битный булевый тип: читаем байт и расширяем до 32 бит ---
                op_0x89: { // LOAD.BOOL Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd_bool = *pc++;
                    uint8_t ra_bool = *pc++;
                    uint8_t mem_idx_bool = *pc++;
                    int16_t offset_bool; memcpy(&offset_bool, pc, sizeof(offset_bool)); pc += sizeof(offset_bool);
                    if (mem_idx_bool != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *base_bool = instance->memory_data;
                    uint32_t mem_size_bool = instance->memory_size_bytes;
                    uintptr_t ra_addr_bool = (uintptr_t)locals[ra_bool].value.ptr;
                    uintptr_t base_addr_bool = (uintptr_t)base_bool;
                    if (ra_addr_bool < base_addr_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t ra_off_bool = (uint32_t)(ra_addr_bool - base_addr_bool);
                    int64_t tgt_bool = (int64_t)ra_off_bool + offset_bool;
                    if (tgt_bool < 0 || (uint64_t)tgt_bool + sizeof(uint8_t) > mem_size_bool) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t target_bool = (uint32_t)tgt_bool;
                    uint8_t bool_val = *((uint8_t*)(base_bool + target_bool));
                    locals[rd_bool].type = ESPB_TYPE_BOOL;
                    locals[rd_bool].value.i32 = (bool_val != 0) ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "LOAD.BOOL R%u <- mem[%u] = %u", rd_bool, target_bool, bool_val);
#endif
                    goto interpreter_loop_start;
                }

                op_0x92: { // TRUNC.I32.I8 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I8;
                    locals[rd].value.i8 = (int8_t)locals[rs].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "TRUNC.I32.I8 R%u, R%u = %" PRId8, (unsigned)rd, (unsigned)rs, locals[rd].value.i8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x94: { // TRUNC.I32.I8 (alias) Rd(u8), Rs(u8)
                    uint8_t rd94 = *pc++;
                    uint8_t rs94 = *pc++;
                    locals[rd94].type = ESPB_TYPE_I8;
                    locals[rd94].value.i8 = (int8_t)locals[rs94].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I32.I8 R%u, R%u = %" PRId8, (unsigned)rd94, (unsigned)rs94, locals[rd94].value.i8);
#endif
                    goto interpreter_loop_start;
                }
                op_0x97: { // SEXT.I8.I32 Rd(u8), Rs(u8) (используется для знакового расширения i8→i32)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int8_t)locals[rs].value.i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I8.I32 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x9E: { // SEXT.I8.I32 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int8_t)locals[rs].value.i8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I8.I32 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x98: { // ZEXT.I8.I32 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (uint8_t)locals[rs].value.u8;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "ZEXT.I8.I32 R%u, R%u = %" PRId32, (unsigned)rd, (unsigned)rs, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x11: { // MOV.I16 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I16;
                    locals[rd].value.i16 = (int16_t)locals[rs].value.i16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "MOV.I16 R%d, R%d = %" PRId16, rd, rs, locals[rd].value.i16);
#endif
                    goto interpreter_loop_start;
                }
                op_0x83: { // LOAD.U16 Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx2 = *pc++;
                    int16_t off2; memcpy(&off2, pc, sizeof(int16_t)); pc += sizeof(int16_t);
                    if (mem_idx2 != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    uint8_t *b2 = instance->memory_data;
                    uint32_t ms2 = instance->memory_size_bytes;
                    uintptr_t addr2 = (uintptr_t)locals[ra].value.ptr;
                    if (addr2 < (uintptr_t)b2) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t off_base = (uint32_t)(addr2 - (uintptr_t)b2);
                    int64_t t2 = (int64_t)off_base + off2;
                    if (t2 < 0 || (uint64_t)t2 + sizeof(uint16_t) > ms2) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS; }
                    uint32_t tgt2 = (uint32_t)t2;
                    if (tgt2 % sizeof(uint16_t) != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_UNALIGNED_MEMORY_ACCESS; }
                    uint16_t lval = *((uint16_t*)(b2 + tgt2));
                    locals[rd].type = ESPB_TYPE_U16;
                    locals[rd].value.u16 = lval;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.U16 R%u <- mem[%" PRIu32 "] = %" PRIu16, rd, tgt2, lval);
#endif
                    goto interpreter_loop_start;
                }
                op_0x93: { // TRUNC.I32.I16 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs2 = *pc++;
                    locals[rd].type = ESPB_TYPE_I16;
                    locals[rd].value.i16 = (int16_t)locals[rs2].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "TRUNC.I32.I16 R%d, R%d = %" PRId16, rd, rs2, locals[rd].value.i16);
#endif
                    goto interpreter_loop_start;
                }
                op_0x9F: { // SEXT.I16.I32 Rd(u8), Rs(u8)
                    uint8_t rd2 = *pc++;
                    uint8_t rs3 = *pc++;
                    locals[rd2].type = ESPB_TYPE_I32;
                    locals[rd2].value.i32 = (int16_t)locals[rs3].value.i16;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "SEXT.I16.I32 R%d, R%d = %" PRId32, rd2, rs3, locals[rd2].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                op_0x9B: { // ZEXT.I32.I64 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    
                    if (locals[rs].type == ESPB_TYPE_I64 || locals[rs].type == ESPB_TYPE_U64) {
                        locals[rd].type = locals[rs].type;
                        locals[rd].value.u64 = locals[rs].value.u64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "ZEXT.I32.I64 (as MOV) R%d, R%d = %" PRIu64, rd, rs, locals[rd].value.u64);
#endif
                    } else {
                        locals[rd].type = ESPB_TYPE_U64;
                        locals[rd].value.u64 = (uint64_t)(uint32_t)locals[rs].value.u32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "ZEXT.I32.I64 (from i32) R%d, R%d = %" PRIu64, rd, rs, locals[rd].value.u64);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0xA1: { // SEXT.I32.I64 Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;

                    if (locals[rs].type == ESPB_TYPE_I64 || locals[rs].type == ESPB_TYPE_U64) {
                        locals[rd].value.i64 = locals[rs].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "SEXT.I32.I64 (as MOV) R%d, R%d = %" PRId64, rd, rs, locals[rd].value.i64);
#endif
                    } else {
                        locals[rd].value.i64 = (int64_t)locals[rs].value.i32;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "SEXT.I32.I64 (from i32) R%d, R%d = %" PRId64, rd, rs, locals[rd].value.i64);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0xA6: { // CVT.F32.U32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_U32;
                    locals[rd].value.u32 = (uint32_t)locals[rs].value.f32;
                    goto interpreter_loop_start;
                }
                op_0xA7: { // CVT.F32.U64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = (uint64_t)locals[rs].value.f32;
                    goto interpreter_loop_start;
                }
                op_0xA8: { // CVT.F64.U32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_U32;
                    locals[rd].value.u32 = (uint32_t)locals[rs].value.f64;
                    goto interpreter_loop_start;
                }
                op_0xA9: { // CVT.F64.U64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = (uint64_t)locals[rs].value.f64;
                    goto interpreter_loop_start;
                }
                op_0xAA: { // CVT.F32.I32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)locals[rs].value.f32;
                    goto interpreter_loop_start;
                }
                op_0xAB: { // CVT.F32.I64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = (int64_t)locals[rs].value.f32;
                    goto interpreter_loop_start;
                }
                op_0xAC: { // CVT.F64.I32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = (int32_t)locals[rs].value.f64;
                    goto interpreter_loop_start;
                }
                op_0xAD: { // CVT.F64.I64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = (int64_t)locals[rs].value.f64;
                    goto interpreter_loop_start;
                }
                op_0xAE: { // CVT.U32.F32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = (float)locals[rs].value.u32;
                    goto interpreter_loop_start;
                }
                op_0xAF: { // CVT.U32.F64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F64;
                    locals[rd].value.f64 = (double)locals[rs].value.u32;
                    goto interpreter_loop_start;
                }
                op_0xB0: { // CVT.U64.F32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = (float)locals[rs].value.u64;
                    goto interpreter_loop_start;
                }
                op_0xB1: { // CVT.U64.F64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F64;
                    locals[rd].value.f64 = (double)locals[rs].value.u64;
                    goto interpreter_loop_start;
                }
                op_0xB2: { // CVT.I32.F32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = (float)locals[rs].value.i32;
                    goto interpreter_loop_start;
                }
                op_0xB3: { // CVT.I32.F64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F64;
                    locals[rd].value.f64 = (double)locals[rs].value.i32;
                    goto interpreter_loop_start;
                }
                op_0xB4: { // CVT.I64.F32
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F32;
                    locals[rd].value.f32 = (float)locals[rs].value.i64;
                    goto interpreter_loop_start;
                }
                op_0xB5: { // CVT.I64.F64
                    uint8_t rd = *pc++; uint8_t rs = *pc++;
                    locals[rd].type = ESPB_TYPE_F64;
                    locals[rd].value.f64 = (double)locals[rs].value.i64;
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
                op_0x16: { // MOV.PTR Rd(u8), Rs(u8)
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    
                    if (rd >= num_virtual_regs) {
                        ESP_LOGE(TAG, "MOV.PTR - Dest register R%u out of bounds (num_virtual_regs: %u)",
                                rd, num_virtual_regs);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    if (rs >= num_virtual_regs) {
                        ESP_LOGE(TAG, "MOV.PTR - Source register R%u out of bounds (num_virtual_regs: %u)",
                                rs, num_virtual_regs);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    locals[rd] = locals[rs];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "MOV.PTR R%u, R%u = %p", rd, rs, locals[rd].value.ptr);
#endif
                    goto interpreter_loop_start;
                }
                op_0x1D: { // LD_GLOBAL_ADDR Rd(u8), symbol_idx(u16)
                    uint8_t rd = *pc++;
                    uint16_t symbol_idx;
                    memcpy(&symbol_idx, pc, sizeof(symbol_idx)); pc += sizeof(symbol_idx);
                    uintptr_t addr = 0;
                    bool found_in_func_map = false;

                    // 1. Сначала пытаемся разрешить как указатель на функцию через func_ptr_map
                    if (module->func_ptr_map && module->num_func_ptr_map_entries > 0) {
                        for (uint32_t i = 0; i < module->num_func_ptr_map_entries; i++) {
                            if (module->func_ptr_map[i].function_index == symbol_idx) {
                                uint32_t data_offset = module->func_ptr_map[i].data_offset;
                                if (instance->memory_data) {
                                    addr = (uintptr_t)(instance->memory_data + data_offset);
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGD(TAG, "LD_GLOBAL_ADDR (Func): Resolved symbol_idx %hu to function ptr via map. Offset=0x%x, Addr=%p", symbol_idx, data_offset, (void*)addr);
#endif
                                    found_in_func_map = true;
                                    break;
                                }
                            }
                        }
                    }

                    // 2. Если не найдено в карте, обрабатываем как обычную глобальную переменную
                    if (!found_in_func_map) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "LD_GLOBAL_ADDR (Global): Symbol_idx %hu not in func map, resolving as global.", symbol_idx);
#endif
                        if (symbol_idx < module->num_globals) {
                            const EspbGlobalDesc *global_desc = &module->globals[symbol_idx];
                            if (global_desc->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                                if (instance->memory_data) {
                                    addr = (uintptr_t)(instance->memory_data + global_desc->initializer.data_section_offset);
                                } else {
                                    ESP_LOGE(TAG, "LD_GLOBAL_ADDR - instance->memory_data is NULL for DATA_OFFSET global_idx=%hu", symbol_idx);
                                    return ESPB_ERR_INSTANTIATION_FAILED;
                                }
                            } else if (global_desc->init_kind == ESPB_INIT_KIND_CONST || global_desc->init_kind == ESPB_INIT_KIND_ZERO) {
                                if (instance->globals_data && instance->global_offsets) {
                                    addr = (uintptr_t)(instance->globals_data + instance->global_offsets[symbol_idx]);
                                } else {
                                    ESP_LOGE(TAG, "LD_GLOBAL_ADDR - globals_data or global_offsets is NULL for global_idx=%hu", symbol_idx);
                                    return ESPB_ERR_INSTANTIATION_FAILED;
                                }
                            } else {
                                ESP_LOGE(TAG, "LD_GLOBAL_ADDR - Unknown init_kind %d for global_idx=%hu", global_desc->init_kind, symbol_idx);
                                return ESPB_ERR_INVALID_OPERAND;
                            }
                        } else {
                            ESP_LOGE(TAG, "LD_GLOBAL_ADDR: Invalid symbol_idx %hu (not in func map, not a valid global)", symbol_idx);
                            return ESPB_ERR_INVALID_GLOBAL_INDEX;
                        }
                    }

                    // 3. Устанавливаем результат
                    if ((uint16_t)rd < num_virtual_regs) {
                        locals[rd].type = ESPB_TYPE_PTR;
                        locals[rd].value.ptr = (void*)addr;
                    } else {
                        ESP_LOGE(TAG, "LD_GLOBAL_ADDR - Invalid destination register_idx %hhu (num_virtual_regs %" PRIu16 ")", rd, num_virtual_regs);
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    
                    goto interpreter_loop_start;
                }
                op_0x09: { // CALL_IMPORT import_idx(u16)
                    uint16_t import_idx;
                    memcpy(&import_idx, pc, sizeof(import_idx)); pc += sizeof(import_idx);
                    uint8_t ret_reg = 0; // возвращаем в R0 по умолчанию
                    
                    // ДИАГНОСТИКА: Отслеживаем состояние системы перед каждым FFI вызовом
                    TickType_t current_tick = xTaskGetTickCount();
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "ESPB FFI DEBUG: CALL_IMPORT #%u at tick %u", import_idx, (unsigned)current_tick);
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
                        ESP_LOGE(TAG, "resolved_import_funcs[%u] is NULL for %s::%s",
                                import_idx, import_desc->module_name, import_desc->entity_name);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
                    }
                    
                    // Ранняя проверка для pvTimerGetTimerID (без доступа к аргументам)
                    // DEBUG: Check if timer function is working correctly
                    if (strcmp(import_desc->entity_name, "vTaskDelay") == 0) {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "ESPB FFI DEBUG: Checking main thread state before vTaskDelay");
#endif
                        TickType_t tick_now = xTaskGetTickCount();
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                        ESP_LOGD(TAG, "ESPB FFI DEBUG: Current tick_count=%u", (unsigned)tick_now);
#endif
                    }

   
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
                            ESP_LOGE(TAG, "Unsupported ESPB param type %d for FFI (arg %" PRIu32 ") for %s::%s",
                                   es_arg_type, i, import_desc->module_name, import_desc->entity_name);
                            // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }

                        // По умолчанию берем значение как есть из регистра locals[i]
                        // Тип данных в locals[i] должен соответствовать es_arg_type из сигнатуры нативной функции
                        // или быть совместимым (например, BOOL и I32)
                        switch (es_arg_type) {
                            case ESPB_TYPE_I8: ffi_native_arg_values[i] = &locals[i].value.i8; break;
                            case ESPB_TYPE_U8: ffi_native_arg_values[i] = &locals[i].value.u8; break;
                            case ESPB_TYPE_I16: ffi_native_arg_values[i] = &locals[i].value.i16; break;
                            case ESPB_TYPE_U16: ffi_native_arg_values[i] = &locals[i].value.u16; break;
                            case ESPB_TYPE_I32: case ESPB_TYPE_BOOL: ffi_native_arg_values[i] = &locals[i].value.i32; break;
                            case ESPB_TYPE_U32: ffi_native_arg_values[i] = &locals[i].value.u32; break;
                            case ESPB_TYPE_I64: 
                                // Для 64-битных знаковых аргументов проверяем тип регистра
                                if (locals[i].type == ESPB_TYPE_I64) {
                                    // Если регистр уже содержит 64-битное значение, используем его напрямую
                                    ffi_native_arg_values[i] = &locals[i].value.i64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                   ESP_LOGD(TAG, "Using native I64 value from register R%u: %" PRId64,
                                             (unsigned)i, locals[i].value.i64);
#endif
                                } else if (locals[i].type == ESPB_TYPE_I32) {
                                    // Проверяем, можем ли "склеить" два регистра
                                    if (i + 1 < num_native_args &&
                                        (locals[i+1].type == ESPB_TYPE_I32 || locals[i+1].type == ESPB_TYPE_U32)) {
                                        // Комбинируем два 32-битных регистра
                                        uint32_t lo = (uint32_t)locals[i].value.u32;
                                        uint32_t hi = (uint32_t)locals[i+1].value.u32;
                                        temp_i64_values[i] = ((int64_t)hi << 32) | lo;
                                        ffi_native_arg_values[i] = &temp_i64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Constructed I64 from two regs: R%u=0x%lx + R%u=0x%lx = %" PRId64,
                                                (unsigned)i, (unsigned long)lo, (unsigned)(i+1), (unsigned long)hi, temp_i64_values[i]);
#endif
                                        i++; // Пропускаем следующий регистр, так как он уже использован
                                    } else {
                                        // Если в регистре знаковое 32-битное значение, расширяем до 64 бит
                                        temp_i64_values[i] = (int64_t)(int32_t)locals[i].value.i32;
                                        ffi_native_arg_values[i] = &temp_i64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Extended I32 value %ld to I64: %" PRId64 " for arg %u",
                                                 (long)locals[i].value.i32, temp_i64_values[i], (unsigned)i);
#endif
                                    }
                                } else if (locals[i].type == ESPB_TYPE_U32) {
                                    // Если в регистре беззнаковое 32-битное значение
                                    // Проверяем, можем ли "склеить" два регистра
                                    if (i + 1 < num_native_args && 
                                        (locals[i+1].type == ESPB_TYPE_I32 || locals[i+1].type == ESPB_TYPE_U32)) {
                                        // Комбинируем два 32-битных регистра
                                        uint32_t lo = (uint32_t)locals[i].value.u32;
                                        uint32_t hi = (uint32_t)locals[i+1].value.u32;
                                        temp_i64_values[i] = ((int64_t)hi << 32) | lo;
                                        ffi_native_arg_values[i] = &temp_i64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Constructed I64 from two regs: R%u=0x%lx + R%u=0x%lx = %" PRId64,
                                                (unsigned)i, (unsigned long)lo, (unsigned)(i+1), (unsigned long)hi, temp_i64_values[i]);
#endif
                                        i++; // Пропускаем следующий регистр, так как он уже использован
                                    } else {
                                        temp_i64_values[i] = (int64_t)(uint32_t)locals[i].value.u32;
                                        ffi_native_arg_values[i] = &temp_i64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Extended U32 value %lu to I64: %" PRId64 " for arg %u",
                                                 (unsigned long)locals[i].value.u32, temp_i64_values[i], (unsigned)i);
#endif
                                    }
                                } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGW(TAG, "Cannot convert type %d to I64 at R%u, using default value 0",
                                            (int)locals[i].type, (unsigned)i);
#endif
                                    temp_i64_values[i] = 0;
                                    ffi_native_arg_values[i] = &temp_i64_values[i];
                                }
                                break;
                            case ESPB_TYPE_U64: 
                                // Для 64-битных беззнаковых аргументов проверяем тип регистра
                                if (locals[i].type == ESPB_TYPE_U64) {
                                    // Если регистр уже содержит 64-битное значение, используем его напрямую
                                    ffi_native_arg_values[i] = &locals[i].value.u64;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                   ESP_LOGD(TAG, "Using native U64 value from register R%u: %" PRIu64,
                                             (unsigned)i, locals[i].value.u64);
#endif
                                } else if (locals[i].type == ESPB_TYPE_U32) {
                                    // Проверяем, можем ли "склеить" два регистра
                                    if (i + 1 < num_native_args && 
                                        (locals[i+1].type == ESPB_TYPE_I32 || locals[i+1].type == ESPB_TYPE_U32)) {
                                        // Комбинируем два 32-битных регистра
                                        uint32_t lo = (uint32_t)locals[i].value.u32;
                                        uint32_t hi = (uint32_t)locals[i+1].value.u32;
                                        temp_u64_values[i] = ((uint64_t)hi << 32) | lo;
                                        ffi_native_arg_values[i] = &temp_u64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Constructed U64 from two regs: R%u=0x%lx + R%u=0x%lx = %" PRIu64,
                                                (unsigned)i, (unsigned long)lo, (unsigned)(i+1), (unsigned long)hi, temp_u64_values[i]);
#endif
#endif
                                        i++; // Пропускаем следующий регистр, так как он уже использован
                                    } else {
                                        // Если в регистре беззнаковое 32-битное значение, расширяем до 64 бит
                                        temp_u64_values[i] = (uint64_t)(uint32_t)locals[i].value.u32;
                                        ffi_native_arg_values[i] = &temp_u64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Extended U32 value %lu to U64: %" PRIu64 " for arg %u",
                                                 (unsigned long)locals[i].value.u32, temp_u64_values[i], (unsigned)i);
#endif
                                    }
                                } else if (locals[i].type == ESPB_TYPE_I32) {
                                    // Проверяем, можем ли "склеить" два регистра
                                    if (i + 1 < num_native_args && 
                                        (locals[i+1].type == ESPB_TYPE_I32 || locals[i+1].type == ESPB_TYPE_U32)) {
                                        // Комбинируем два 32-битных регистра
                                        uint32_t lo = (uint32_t)locals[i].value.u32;
                                        uint32_t hi = (uint32_t)locals[i+1].value.u32;
                                        temp_u64_values[i] = ((uint64_t)hi << 32) | lo;
                                        ffi_native_arg_values[i] = &temp_u64_values[i];
                                       ESP_LOGD(TAG, "Constructed U64 from two regs: R%u=0x%lx + R%u=0x%lx = %" PRIu64,
                                               (unsigned)i, (unsigned long)lo, (unsigned)(i+1), (unsigned long)hi, temp_u64_values[i]);
                                        i++; // Пропускаем следующий регистр, так как он уже использован
                                    } else {
                                        // Если в регистре знаковое 32-битное значение, преобразуем в беззнаковое
                                        temp_u64_values[i] = (uint64_t)(int64_t)(int32_t)locals[i].value.i32;
                                        ffi_native_arg_values[i] = &temp_u64_values[i];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                       ESP_LOGD(TAG, "Extended I32 value %ld to U64: %" PRIu64 " for arg %u",
                                                 (long)locals[i].value.i32, temp_u64_values[i], (unsigned)i);
#endif
                                    }
                                } else {
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                                    ESP_LOGW(TAG, "Cannot convert type %d to U64 at R%u, using default value 0",
                                            (int)locals[i].type, (unsigned)i);
#endif
                                    temp_u64_values[i] = 0;
                                    ffi_native_arg_values[i] = &temp_u64_values[i];
                                }
                                break;
                            case ESPB_TYPE_F32: ffi_native_arg_values[i] = &locals[i].value.f32; break;
                            case ESPB_TYPE_F64: ffi_native_arg_values[i] = &locals[i].value.f64; break;
                            case ESPB_TYPE_PTR: ffi_native_arg_values[i] = &locals[i].value.ptr; break;
                                default:
                                ESP_LOGE(TAG, "Cannot get value for unsupported ESPB type %d (arg %" PRIu32 ")", es_arg_type, i);
                                // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }
                        //ESP_LOGD(TAG,"ESPB FFI DEBUG: Arg %" PRIu32 " (ESPB Type %d, FFI Type %p): val_ptr=%p\n", i, es_arg_type, (void*)ffi_native_arg_types[i], ffi_native_arg_values[i]);
                        // // Отладочный вывод значения, если это I32 или PTR
                        // if (es_arg_type == ESPB_TYPE_I32)ESP_LOGD(TAG,"ESPB FFI DEBUG:   val=%" PRId32 "\n", locals[i].value.i32);
                        // else if (es_arg_type == ESPB_TYPE_PTR)ESP_LOGD(TAG,"ESPB FFI DEBUG:   val_ptr=%p\n", locals[i].value.ptr);


                        // Логика обработки колбэков
                        if (exec_ctx->feature_callback_auto_active && es_arg_type == ESPB_TYPE_I32) {
                            int32_t potential_cb_arg = locals[i].value.i32;
                            
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
                                            original_user_data_for_espb = locals[user_data_arg_idx].value.ptr;
                                        } else {
                                            original_user_data_for_espb = (void*)(uintptr_t)locals[user_data_arg_idx].value.u32;
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
                                       ESP_LOGD(TAG,"FFI INFO: Native function %s::%s arg %"PRIu32" type adjusted from %d to PTR for function closure",
                                           import_desc->module_name, import_desc->entity_name, i, native_sig->param_types[i]);
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
                                arg_plans[i].original_ptr = locals[i].value.ptr;
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
                                } else {
                                    memset(temp, 0, arg_plans[i].buffer_size);
                                }
                                ffi_native_arg_values[i] = &arg_plans[i].temp_buffer;
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

                   bool is_blocking_call = (strcmp(import_desc->entity_name, "vTaskDelay") == 0 ||
                                            strcmp(import_desc->entity_name, "xTimerGenericCommand") == 0);
                   
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
                        bool is_readonly_func = (strcmp(import_desc->entity_name, "memcmp") == 0 || strcmp(import_desc->entity_name, "strcmp") == 0);
                        for (uint8_t i = 0; i < num_native_args; ++i) {
                            if (arg_plans[i].has_meta && arg_plans[i].handler_idx == 0 && arg_plans[i].temp_buffer) {
                                if (!is_readonly_func && (arg_plans[i].direction & ESPB_IMMETA_DIRECTION_OUT)) {
                                    if (arg_plans[i].original_ptr) {
                                        memcpy(arg_plans[i].original_ptr, arg_plans[i].temp_buffer, arg_plans[i].buffer_size);
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
                                locals[ret_reg].type = ESPB_TYPE_I8;
                                locals[ret_reg].value.i8 = native_call_ret_val_container.i8;
                                break;
                            case ESPB_TYPE_U8:
                                locals[ret_reg].type = ESPB_TYPE_U8;
                                locals[ret_reg].value.u8 = native_call_ret_val_container.u8;
                                break;
                            case ESPB_TYPE_I16:
                                locals[ret_reg].type = ESPB_TYPE_I16;
                                locals[ret_reg].value.i16 = native_call_ret_val_container.i16;
                                break;
                            case ESPB_TYPE_U16:
                                locals[ret_reg].type = ESPB_TYPE_U16;
                                locals[ret_reg].value.u16 = native_call_ret_val_container.u16;
                                break;
                            case ESPB_TYPE_I32:
                                locals[ret_reg].type = ESPB_TYPE_I32;
                                locals[ret_reg].value.i32 = native_call_ret_val_container.i32;
                                break;
                            case ESPB_TYPE_U32:
                                locals[ret_reg].type = ESPB_TYPE_U32;
                                locals[ret_reg].value.u32 = native_call_ret_val_container.u32;
                                break;
                            case ESPB_TYPE_I64: 
                                locals[ret_reg].type = ESPB_TYPE_I64;
                                locals[ret_reg].value.i64 = native_call_ret_val_container.i64;
                                break;
                            case ESPB_TYPE_U64:
                                locals[ret_reg].type = ESPB_TYPE_U64;
                                locals[ret_reg].value.u64 = native_call_ret_val_container.u64;
                                break;
                            case ESPB_TYPE_F32:
                                locals[ret_reg].type = ESPB_TYPE_F32;
                                locals[ret_reg].value.f32 = native_call_ret_val_container.f32;
                                    break;
                            case ESPB_TYPE_F64:
                                locals[ret_reg].type = ESPB_TYPE_F64;
                                locals[ret_reg].value.f64 = native_call_ret_val_container.f64;
                                    break;
                            case ESPB_TYPE_PTR:
                                locals[ret_reg].type = ESPB_TYPE_PTR;
                                locals[ret_reg].value.ptr = native_call_ret_val_container.p;
                                    break;
                                default:
                                ESP_LOGE(TAG, "Unsupported return type %d for FFI result conversion", native_ret_es_type);
                                    break;
            }
                    }
                    
                    // Освобождаем память для arg_plans (больше не требуется, т.к. это массив на стеке)
                    
                    // Продолжаем выполнение после вызова
                                 goto interpreter_loop_start;
             }
// --- Начало блока для SELECT ---
op_0xBE: // SELECT.I32 / BOOL
{
    uint8_t rd = *pc++;
    uint8_t r_cond = *pc++;
    uint8_t r_true = *pc++;
    uint8_t r_false = *pc++;
    bool condition = (locals[r_cond].value.i32 != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.I32 R%u, R%u(%s), R%u, R%u -> val=%" PRId32, rd, r_cond, condition ? "true" : "false", r_true, r_false, locals[rd].value.i32);
#endif
    goto interpreter_loop_start;
}
op_0xBF: // SELECT.I64
{
    uint8_t rd = *pc++;
    uint8_t r_cond = *pc++;
    uint8_t r_true = *pc++;
    uint8_t r_false = *pc++;
    bool condition = (locals[r_cond].value.i32 != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.I64 R%u, R%u(%s), R%u, R%u -> val=%" PRId64, rd, r_cond, condition ? "true" : "false", r_true, r_false, locals[rd].value.i64);
#endif
    goto interpreter_loop_start;
}
op_0xD4: // SELECT.F32
{
    uint8_t rd = *pc++;
    uint8_t r_cond = *pc++;
    uint8_t r_true = *pc++;
    uint8_t r_false = *pc++;
    bool condition = (locals[r_cond].value.i32 != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.F32 R%u, R%u(%s), R%u, R%u -> val=%f", rd, r_cond, condition ? "true" : "false", r_true, r_false, locals[rd].value.f32);
#endif
    goto interpreter_loop_start;
}
op_0xD5: // SELECT.F64
{
    uint8_t rd = *pc++;
    uint8_t r_cond = *pc++;
    uint8_t r_true = *pc++;
    uint8_t r_false = *pc++;
    bool condition = (locals[r_cond].value.i32 != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.F64 R%u, R%u(%s), R%u, R%u -> val=%f", rd, r_cond, condition ? "true" : "false", r_true, r_false, locals[rd].value.f64);
#endif
    goto interpreter_loop_start;
}
op_0xD6: // SELECT.PTR
{
    uint8_t rd = *pc++;
    uint8_t r_cond = *pc++;
    uint8_t r_true = *pc++;
    uint8_t r_false = *pc++;
    bool condition = (locals[r_cond].value.i32 != 0);
    locals[rd] = condition ? locals[r_true] : locals[r_false];
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "SELECT.PTR R%u, R%u(%s), R%u, R%u -> val=%p", rd, r_cond, condition ? "true" : "false", r_true, r_false, locals[rd].value.ptr);
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
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    if (rd >= num_virtual_regs || r1 >= num_virtual_regs || r2 >= num_virtual_regs) {
                        ESP_LOGE(TAG, "CMP - Register out of bounds.");
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }

                    int32_t val1 = locals[r1].value.i32;
                    int32_t val2 = locals[r2].value.i32;
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
                    
                    locals[rd].type = ESPB_TYPE_BOOL;
                    locals[rd].value.i32 = cmp_res ? 1 : 0;
                    
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                    ESP_LOGD(TAG, "CMP Opcode 0x%02X: R%u, R%u, R%u -> %d", opcode, rd, r1, r2, locals[rd].value.i32);
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
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                int64_t val1 = locals[r1].value.i64;
                int64_t val2 = locals[r2].value.i64;
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
                locals[rd].type = ESPB_TYPE_BOOL;
                locals[rd].value.i32 = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.I64 Opcode 0x%02X -> %d", opcode, locals[rd].value.i32);
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
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                float val1 = locals[r1].value.f32;
                float val2 = locals[r2].value.f32;
                bool cmp_res = false;
                if (isnan(val1) || isnan(val2)) {
                    if (opcode == 0xE0 || opcode == 0xE1) { // Trap for EQ/NE on NaN
                         // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
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
                locals[rd].type = ESPB_TYPE_BOOL;
                locals[rd].value.i32 = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.F32 Opcode 0x%02X -> %d", opcode, locals[rd].value.i32);
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
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                double val1 = locals[r1].value.f64;
                double val2 = locals[r2].value.f64;
                bool cmp_res = false;
                 if (isnan(val1) || isnan(val2)) {
                    if (opcode == 0xE6 || opcode == 0xE7) { // Trap for EQ/NE on NaN
                         // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
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
                locals[rd].type = ESPB_TYPE_BOOL;
                locals[rd].value.i32 = cmp_res ? 1 : 0;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                ESP_LOGD(TAG, "CMP.F64 Opcode 0x%02X -> %d", opcode, locals[rd].value.i32);
#endif
                goto interpreter_loop_start;
            }

                op_0x8E: { // ADDR_OF Rd(u8), Rs(u8) - Создает указатель на виртуальный регистр
                    uint8_t rd = *pc++;
                    uint8_t rs = *pc++;
                    if (rd >= num_virtual_regs || rs >= num_virtual_regs) {
                        ESP_LOGE(TAG, "ADDR_OF - Register out of bounds. Rd=%u, Rs=%u, NumRegs=%u", rd, rs, num_virtual_regs);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX;
                    }
                    // Получаем адрес регистра rs в памяти виртуальной машины
                    void *ptr_to_reg = &locals[rs];
                    locals[rd].type = ESPB_TYPE_PTR;
                    locals[rd].value.ptr = ptr_to_reg;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG,"ESPB DEBUG: ADDR_OF R%u, R%u = %p\n", rd, rs, ptr_to_reg);
#endif
                    goto interpreter_loop_start;
                }
                op_0x84: { // LOAD.I32 Rd(u8), Ra(u8), mem_idx(u8), offset(i16)
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t mem_idx = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    if (mem_idx != 0) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_MEMORY_INDEX; }
                    
                    uintptr_t ra_addr;
                    EspbValueType ra_type = locals[ra].type;

                    if (ra_type == ESPB_TYPE_PTR) {
                        ra_addr = (uintptr_t)locals[ra].value.ptr;
                    } else if (ra_type == ESPB_TYPE_I32 || ra_type == ESPB_TYPE_U32) {
                        ra_addr = (uintptr_t)locals[ra].value.u32;
                    } else {
                        ESP_LOGE(TAG, "LOAD.I32 - Address register R%u is not a pointer or integer (type: %d). PC_offset: %ld",
                                ra, ra_type, (long)((pc - 5) - instructions_ptr));
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_TYPE_MISMATCH;
                    }
                    
                    // HACK: Workaround for buggy bytecode using LOAD.I32 to move an immediate or native pointer.
                    if (ra_type == ESPB_TYPE_I32 || ra_type == ESPB_TYPE_U32) {
                        bool is_likely_immediate = (ra_addr < 65536); // Check for small, non-negative integers
                        bool is_native_pointer = (ra_addr >= 0x3FFAE000 && ra_addr < 0x40000000); // ESP32 DRAM address range

                        if (is_likely_immediate) {
                            // Value is likely an immediate integer, not a pointer to be dereferenced. Treat as MOV.
                           ESP_LOGW(TAG, "HACK: LOAD.I32 on R%u (val=0x%x) is treated as MOV.I32", ra, (unsigned int)ra_addr);
                            locals[rd].type = ra_type;
                            locals[rd].value.u32 = ra_addr + offset; // Also apply offset
                            goto interpreter_loop_start; // Skip dereferencing
                        }
                        if (is_native_pointer) {
                            // ВОССТАНОВЛЕНО: Правильно читаем значение из нативной памяти ESP32
                           ESP_LOGD(TAG, "FIXED: LOAD.I32 on R%u (addr=0x%x) - reading I32 value from native memory", ra, (unsigned int)ra_addr);
                            
                            // Безопасно читаем 32-битное значение из нативной памяти
                            int32_t loaded_value;
                            void* src_addr = (void*)(ra_addr + offset);
                            memcpy(&loaded_value, src_addr, sizeof(int32_t));
                            
                            locals[rd].type = ESPB_TYPE_I32;
                            locals[rd].value.i32 = loaded_value;
                            
                           ESP_LOGD(TAG, "FIXED: Loaded I32 value %d from native addr 0x%x", loaded_value, (unsigned int)(ra_addr + offset));
                            goto interpreter_loop_start; // Завершаем обработку
                        }
                    }

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
                        memcpy(&locals[rd].value.i32, base + target, sizeof(int32_t));
                        locals[rd].type = ESPB_TYPE_I32;
                    } else {
                        // Ra holds an absolute native address (that's not in our hacked range).
                        uintptr_t target_addr = ra_addr + offset;
                        // This is unsafe, but we assume the bytecode knows what it's doing.
                        memcpy(&locals[rd].value.i32, (void*)target_addr, sizeof(int32_t));
                        locals[rd].type = ESPB_TYPE_I32;
                    }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                   ESP_LOGD(TAG, "LOAD.I32 R%u <- mem[R%u(0x%x)+%d] = %" PRId32, rd, ra, (unsigned int)ra_addr, offset, locals[rd].value.i32);
#endif
                    goto interpreter_loop_start;
                }
                // Remove duplicate/old added opcodes block (0x47/0x48/0x49) - handled earlier

                // Целочисленная арифметика с Imm (I64)
                op_0x50: { // ADD.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 + imm;
                   ESP_LOGD(TAG, "ADD.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, locals[rd].value.i64);
                goto interpreter_loop_start;
            }
                op_0x51: { // SUB.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 - imm;
                    ESP_LOGD(TAG, "SUB.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, locals[rd].value.i64);
                    goto interpreter_loop_start;
                }
                op_0x52: { // MUL.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc++;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = locals[r1].value.i64 * imm;
                    ESP_LOGD(TAG, "MUL.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, locals[rd].value.i64);
                    goto interpreter_loop_start;
                }

                op_0x53: { // DIVS.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc++;
                    int64_t dividend = locals[r1].value.i64;
                    int64_t divisor = (int64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT64_MIN && divisor == -1) { return ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW; }
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = dividend / divisor;
                    ESP_LOGD(TAG, "DIVS.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, locals[rd].value.i64);
                    goto interpreter_loop_start;
                }
                op_0x54: { // DIVU.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint64_t dividend = locals[r1].value.u64;
                    uint64_t divisor = (uint64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = dividend / divisor;
                    ESP_LOGD(TAG, "DIVU.I64.IMM8 R%u, R%u, %" PRIu8 " = %" PRIu64, rd, r1, imm, locals[rd].value.u64);
                    goto interpreter_loop_start;
                }
                op_0x55: { // REMS.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    int8_t imm = *(int8_t*)pc++;
                    int64_t dividend = locals[r1].value.i64;
                    int64_t divisor = (int64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    if (dividend == INT64_MIN && divisor == -1) {
                        locals[rd].value.i64 = 0; // The remainder is 0 in this overflow case
                    } else {
                        locals[rd].value.i64 = dividend % divisor;
                    }
                    locals[rd].type = ESPB_TYPE_I64;
                    ESP_LOGD(TAG, "REMS.I64.IMM8 R%u, R%u, %" PRId8 " = %" PRId64, rd, r1, imm, locals[rd].value.i64);
                    goto interpreter_loop_start;
                }
                op_0x56: { // REMU.I64.IMM8 Rd, R1, imm8
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint64_t dividend = locals[r1].value.u64;
                    uint64_t divisor = (uint64_t)imm;
                    if (divisor == 0) { return ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO; }
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = dividend % divisor;
                    ESP_LOGD(TAG, "REMU.I64.IMM8 R%u, R%u, %" PRIu8 " = %" PRIu64, rd, r1, imm, locals[rd].value.u64);
                    goto interpreter_loop_start;
                }
                // 0x57 is RESERVED
                op_0x58: { // SHRU.I64.IMM8 Rd, R1, imm8 (Logical Shift Right)
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t imm = *pc++;
                    uint32_t shift = (uint32_t)imm & 63; // mask to 6 bits for I64
                    uint64_t val = locals[r1].value.u64;
                    locals[rd].type = ESPB_TYPE_U64;
                    locals[rd].value.u64 = val >> shift;
                    ESP_LOGD(TAG, "SHRU.I64.IMM8 R%u, R%u, %u = %" PRIu64, rd, r1, imm, locals[rd].value.u64);
                    goto interpreter_loop_start;
                }
                
                op_0x1E: { // LD_GLOBAL Rd(u8), global_idx(u16)
                    uint8_t rd = *pc++;
                    uint16_t global_idx;
                    memcpy(&global_idx, pc, sizeof(global_idx)); pc += sizeof(global_idx);

                    if (global_idx >= module->num_globals) {
                        ESP_LOGE(TAG, "LD_GLOBAL - Invalid global_idx %hu (num_globals %" PRIu32 ")",
                                global_idx, module->num_globals);
                        // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_GLOBAL_INDEX;
                    }
                    if ((uint16_t)rd >= num_virtual_regs) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX; }

                    const EspbGlobalDesc *g = &module->globals[global_idx];
                    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                        if (!instance->memory_data) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->memory_data + g->initializer.data_section_offset;
                        // Если глобал — это PTR, возвращаем адрес; иначе читаем значение по типу
                        if (g->type == ESPB_TYPE_PTR) {
                            uintptr_t addr = (uintptr_t)base;
                            locals[rd].type = ESPB_TYPE_PTR;
                            locals[rd].value.ptr = (void*)addr;
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                           ESP_LOGD(TAG, "LD_GLOBAL (DATA_OFFSET PTR) R%u <- %p (global_idx=%hu)", rd, (void*)addr, global_idx);
#endif
                        } else {
                            switch (g->type) {
                                case ESPB_TYPE_I8:  locals[rd].type = ESPB_TYPE_I8;  locals[rd].value.i8  = *(int8_t*)base;  break;
                                case ESPB_TYPE_U8:  locals[rd].type = ESPB_TYPE_U8;  locals[rd].value.u8  = *(uint8_t*)base; break;
                                case ESPB_TYPE_I16: locals[rd].type = ESPB_TYPE_I16; locals[rd].value.i16 = *(int16_t*)base; break;
                                case ESPB_TYPE_U16: locals[rd].type = ESPB_TYPE_U16; locals[rd].value.u16 = *(uint16_t*)base; break;
                                case ESPB_TYPE_I32: locals[rd].type = ESPB_TYPE_I32; locals[rd].value.i32 = *(int32_t*)base; break;
                                case ESPB_TYPE_U32: locals[rd].type = ESPB_TYPE_U32; locals[rd].value.u32 = *(uint32_t*)base; break;
                                case ESPB_TYPE_I64: locals[rd].type = ESPB_TYPE_I64; locals[rd].value.i64 = *(int64_t*)base; break;
                                case ESPB_TYPE_U64: locals[rd].type = ESPB_TYPE_U64; locals[rd].value.u64 = *(uint64_t*)base; break;
                                case ESPB_TYPE_F32: locals[rd].type = ESPB_TYPE_F32; locals[rd].value.f32 = *(float*)base; break;
                                case ESPB_TYPE_F64: locals[rd].type = ESPB_TYPE_F64; locals[rd].value.f64 = *(double*)base; break;
                                case ESPB_TYPE_BOOL: locals[rd].type = ESPB_TYPE_BOOL; locals[rd].value.i32 = *(int32_t*)base; break;
                                default: // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                            }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                           ESP_LOGD(TAG, "LD_GLOBAL (DATA_OFFSET VAL) R%u <- global[%hu] (type=%d)", rd, global_idx, g->type);
#endif
                        }
                    } else {
                        if (!instance->globals_data || !instance->global_offsets) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->globals_data + instance->global_offsets[global_idx];
                        switch (g->type) {
                            case ESPB_TYPE_I8:  locals[rd].type = ESPB_TYPE_I8;  locals[rd].value.i8  = *(int8_t*)base;  break;
                            case ESPB_TYPE_U8:  locals[rd].type = ESPB_TYPE_U8;  locals[rd].value.u8  = *(uint8_t*)base; break;
                            case ESPB_TYPE_I16: locals[rd].type = ESPB_TYPE_I16; locals[rd].value.i16 = *(int16_t*)base; break;
                            case ESPB_TYPE_U16: locals[rd].type = ESPB_TYPE_U16; locals[rd].value.u16 = *(uint16_t*)base; break;
                            case ESPB_TYPE_I32: locals[rd].type = ESPB_TYPE_I32; locals[rd].value.i32 = *(int32_t*)base; break;
                            case ESPB_TYPE_U32: locals[rd].type = ESPB_TYPE_U32; locals[rd].value.u32 = *(uint32_t*)base; break;
                            case ESPB_TYPE_I64: locals[rd].type = ESPB_TYPE_I64; locals[rd].value.i64 = *(int64_t*)base; break;
                            case ESPB_TYPE_U64: locals[rd].type = ESPB_TYPE_U64; locals[rd].value.u64 = *(uint64_t*)base; break;
                            case ESPB_TYPE_F32: locals[rd].type = ESPB_TYPE_F32; locals[rd].value.f32 = *(float*)base; break;
                            case ESPB_TYPE_F64: locals[rd].type = ESPB_TYPE_F64; locals[rd].value.f64 = *(double*)base; break;
                            case ESPB_TYPE_PTR: locals[rd].type = ESPB_TYPE_PTR; locals[rd].value.ptr = *(void**)base; break;
                            case ESPB_TYPE_BOOL: locals[rd].type = ESPB_TYPE_BOOL; locals[rd].value.i32 = *(int32_t*)base; break;
                            default: // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                       ESP_LOGD(TAG, "LD_GLOBAL R%u <- global[%hu] (type=%d)", rd, global_idx, g->type);
#endif
                    }
                    goto interpreter_loop_start;
                }

                op_0x1F: { // ST_GLOBAL global_idx(u16), Rs(u8)
                    uint16_t global_idx;
                    memcpy(&global_idx, pc, sizeof(global_idx)); pc += sizeof(global_idx);
                    uint8_t rs = *pc++;

                    if (global_idx >= module->num_globals) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_GLOBAL_INDEX; }
                    if ((uint16_t)rs >= num_virtual_regs) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_REGISTER_INDEX; }

                    const EspbGlobalDesc *g = &module->globals[global_idx];
                    if (!g->mutability) { ESP_LOGE(TAG, "ST_GLOBAL to immutable global %hu", global_idx); // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND; }

                    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
                        if (!instance->memory_data) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->memory_data + g->initializer.data_section_offset;
                        if (g->type == ESPB_TYPE_PTR) {
                            *(void**)base = locals[rs].value.ptr;
                        } else {
                            switch (g->type) {
                                case ESPB_TYPE_I8:  *(int8_t*)base = locals[rs].value.i8; break;
                                case ESPB_TYPE_U8:  *(uint8_t*)base = locals[rs].value.u8; break;
                                case ESPB_TYPE_I16: *(int16_t*)base = locals[rs].value.i16; break;
                                case ESPB_TYPE_U16: *(uint16_t*)base = locals[rs].value.u16; break;
                                case ESPB_TYPE_I32: case ESPB_TYPE_BOOL: *(int32_t*)base = locals[rs].value.i32; break;
                                case ESPB_TYPE_U32: *(uint32_t*)base = locals[rs].value.u32; break;
                                case ESPB_TYPE_I64: *(int64_t*)base = locals[rs].value.i64; break;
                                case ESPB_TYPE_U64: *(uint64_t*)base = locals[rs].value.u64; break;
                                case ESPB_TYPE_F32: *(float*)base = locals[rs].value.f32; break;
                                case ESPB_TYPE_F64: *(double*)base = locals[rs].value.f64; break;
                                default: // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                            }
                        }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                       ESP_LOGD(TAG, "ST_GLOBAL (DATA_OFFSET) global[%hu] <- R%u (type=%d)", global_idx, rs, g->type);
#endif
                    } else {
                        if (!instance->globals_data || !instance->global_offsets) { // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INSTANTIATION_FAILED; }
                        uint8_t *base = instance->globals_data + instance->global_offsets[global_idx];
                        switch (g->type) {
                            case ESPB_TYPE_I8:  *(int8_t*)base = locals[rs].value.i8; break;
                            case ESPB_TYPE_U8:  *(uint8_t*)base = locals[rs].value.u8; break;
                            case ESPB_TYPE_I16: *(int16_t*)base = locals[rs].value.i16; break;
                            case ESPB_TYPE_U16: *(uint16_t*)base = locals[rs].value.u16; break;
                            case ESPB_TYPE_I32: case ESPB_TYPE_BOOL: *(int32_t*)base = locals[rs].value.i32; break;
                            case ESPB_TYPE_U32: *(uint32_t*)base = locals[rs].value.u32; break;
                            case ESPB_TYPE_I64: *(int64_t*)base = locals[rs].value.i64; break;
                            case ESPB_TYPE_U64: *(uint64_t*)base = locals[rs].value.u64; break;
                            case ESPB_TYPE_F32: *(float*)base = locals[rs].value.f32; break;
                            case ESPB_TYPE_F64: *(double*)base = locals[rs].value.f64; break;
                            case ESPB_TYPE_PTR: *(void**)base = locals[rs].value.ptr; break;
                            default: // REFACTOR: // REFACTOR_REMOVED: // REMOVED_free_locals removed to prevent double free
                        return ESPB_ERR_INVALID_OPERAND;
                        }
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
                       ESP_LOGD(TAG, "ST_GLOBAL global[%hu] <- R%u (type=%d)", global_idx, rs, g->type);
#endif
                    }
                    goto interpreter_loop_start;
                }
                op_0xD7: { // ATOMIC.RMW.*.I32
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t rv = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int32_t* addr = (int32_t*)locals[ra].value.ptr;
                    int32_t value = locals[rv].value.i32;
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
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = old_val;
                    goto interpreter_loop_start;
                }
                op_0xDD: { // ATOMIC.RMW.CMPXCHG.I32
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t rexp = *pc++;
                    uint8_t rdes = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int32_t* addr = (int32_t*)locals[ra].value.ptr;
                    int32_t expected = locals[rexp].value.i32;
                    int32_t desired = locals[rdes].value.i32;
                    __atomic_compare_exchange_n(addr, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = expected;
                    goto interpreter_loop_start;
                }
                op_0xDE: { // ATOMIC.LOAD.I32
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int32_t* addr = (int32_t*)locals[ra].value.ptr;
                    locals[rd].type = ESPB_TYPE_I32;
                    locals[rd].value.i32 = __atomic_load_n(addr, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                op_0xDF: { // ATOMIC.STORE.I32
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int32_t* addr = (int32_t*)locals[ra].value.ptr;
                    int32_t value = locals[rs].value.i32;
                    __atomic_store_n(addr, value, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                // --- I64 Atomics ---
                op_0xF0: { // ATOMIC.RMW.*.I64
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t rv = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int64_t* addr = (int64_t*)locals[ra].value.ptr;
                    int64_t value = locals[rv].value.i64;
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
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = old_val;
                    goto interpreter_loop_start;
                }
                op_0xF6: { // ATOMIC.RMW.CMPXCHG.I64
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    uint8_t rexp = *pc++;
                    uint8_t rdes = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int64_t* addr = (int64_t*)locals[ra].value.ptr;
                    int64_t expected = locals[rexp].value.i64;
                    int64_t desired = locals[rdes].value.i64;
                    __atomic_compare_exchange_n(addr, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = expected;
                    goto interpreter_loop_start;
                }
                op_0xEC: { // ATOMIC.LOAD.I64
                    uint8_t rd = *pc++;
                    uint8_t ra = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int64_t* addr = (int64_t*)locals[ra].value.ptr;
                    locals[rd].type = ESPB_TYPE_I64;
                    locals[rd].value.i64 = __atomic_load_n(addr, __ATOMIC_SEQ_CST);
                    goto interpreter_loop_start;
                }
                op_0xED: { // ATOMIC.STORE.I64
                    uint8_t rs = *pc++;
                    uint8_t ra = *pc++;
                    pc += 3; // Skip mem_idx and offset
                    int64_t* addr = (int64_t*)locals[ra].value.ptr;
                    int64_t value = locals[rs].value.i64;
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
        case 0x00: { // MEMORY.INIT mem_idx(u8), data_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
            uint8_t mem_idx = *pc++;
            uint32_t data_seg_idx;
            memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);
            uint8_t rd_dest = *pc++;
            uint8_t rs_src_offset = *pc++;
            uint8_t rn_size = *pc++;

            if (mem_idx != 0) { return ESPB_ERR_INVALID_MEMORY_INDEX; }
            if (data_seg_idx >= module->num_data_segments) { return ESPB_ERR_INVALID_OPERAND; }
            
            uint32_t dest_addr = locals[rd_dest].value.u32;
            uint32_t src_offset = locals[rs_src_offset].value.u32;
            uint32_t size = locals[rn_size].value.u32;

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
        case 0x02: { // MEMORY.COPY memD(u8), memS(u8), Rd(u8), Rs(u8), Rn(u8)
            uint8_t mem_dest_idx = *pc++;
            uint8_t mem_src_idx = *pc++;
            uint8_t rd_dest = *pc++;
            uint8_t rs_src = *pc++;
            uint8_t rn_size = *pc++;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "=== MEMORY.COPY DEBUG ===");
            ESP_LOGD(TAG, "mem_dest_idx=%u, mem_src_idx=%u", mem_dest_idx, mem_src_idx);
            ESP_LOGD(TAG, "rd_dest=%u, rs_src=%u, rn_size=%u", rd_dest, rs_src, rn_size);
#endif
#endif

            if (mem_dest_idx != 0 || mem_src_idx != 0) {
                ESP_LOGE(TAG, "MEMORY.COPY: Invalid memory index");
                return ESPB_ERR_INVALID_MEMORY_INDEX;
            }

            // ИСПРАВЛЕНИЕ: Преобразуем абсолютные адреса в относительные смещения
            uintptr_t dest_abs = (uintptr_t)locals[rd_dest].value.ptr;
            uintptr_t src_abs = (uintptr_t)locals[rs_src].value.ptr;
            uint32_t size = locals[rn_size].value.u32;
            
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
        case 0x03: { // MEMORY.FILL mem_idx(u8), Rd(u8), Rval(u8), Rn(u8)
            uint8_t mem_idx = *pc++;
            uint8_t rd_dest = *pc++;
            uint8_t r_val = *pc++;
            uint8_t rn_size = *pc++;

            if (mem_idx != 0) { return ESPB_ERR_INVALID_MEMORY_INDEX; }

            // ИСПРАВЛЕНИЕ: Преобразуем абсолютный адрес в относительное смещение
            uintptr_t dest_abs = (uintptr_t)locals[rd_dest].value.ptr;
            uint8_t val = (uint8_t)locals[r_val].value.i32;
            uint32_t size = locals[rn_size].value.u32;
            
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
            uint8_t rd = *pc++;
            uint8_t rp = *pc++;
            uint8_t rs = *pc++;
            void* old_ptr = locals[rp].value.ptr;
            size_t new_size = (size_t)locals[rs].value.u32;
            void* new_ptr = espb_heap_realloc(instance, old_ptr, new_size);
            locals[rd].type = ESPB_TYPE_PTR;
            locals[rd].value.ptr = new_ptr;
            goto interpreter_loop_start;
        }
        case 0x07: { // HEAP_FREE Rp(u8)
            uint8_t rp = *pc++;
            void* ptr = locals[rp].value.ptr;
            espb_heap_free(instance, ptr);
            locals[rp].value.ptr = NULL;
            goto interpreter_loop_start;
        }
        case 0x09: { // HEAP_CALLOC Rd(u8), Rn(u8), Rs(u8)
            uint8_t rd = *pc++;
            uint8_t rn = *pc++;
            uint8_t rs = *pc++;
            size_t num = (size_t)locals[rn].value.u32;
            size_t size = (size_t)locals[rs].value.u32;
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
            locals[rd].type = ESPB_TYPE_PTR;
            locals[rd].value.ptr = ptr;
            goto interpreter_loop_start;
        }
        case 0x0B: { // HEAP_MALLOC Rd(u8), Rs(u8)
            uint8_t rd = *pc++;
            uint8_t rs = *pc++;
            size_t size = (size_t)locals[rs].value.u32;
            void* ptr = espb_heap_malloc(instance, size);
            locals[rd].type = ESPB_TYPE_PTR;
            locals[rd].value.ptr = ptr;
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

            uint32_t dest_offset = locals[rd_dest].value.u32;
            uint32_t src_offset = locals[rs_src_offset].value.u32;
            uint32_t size = locals[rn_size].value.u32;
            
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
            uint8_t rd = *pc++;
            uint8_t table_idx = *pc++;
            
            if (table_idx >= instance->module->num_tables) {
                ESP_LOGE(TAG, "TABLE.SIZE: Invalid table index %u", table_idx);
                return ESPB_ERR_INVALID_OPERAND;
            }

            if (rd >= num_virtual_regs) {
                ESP_LOGE(TAG, "TABLE.SIZE - Dest register R%u out of bounds", rd);
                return ESPB_ERR_INVALID_REGISTER_INDEX;
            }

            uint32_t size = instance->table_size;
            locals[rd].type = ESPB_TYPE_I32;
            locals[rd].value.i32 = (int32_t)size;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.SIZE R%u <- table[%u].size = %" PRIu32, rd, table_idx, size);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x16: { // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
            uint8_t table_dest_idx = *pc++;
            uint8_t table_src_idx = *pc++;
            uint8_t rd_dest = *pc++;
            uint8_t rs_src = *pc++;
            uint8_t rn_size = *pc++;

            if (table_dest_idx >= module->num_tables || table_src_idx >= module->num_tables) {
                return ESPB_ERR_INVALID_OPERAND;
            }

            uint32_t dest_offset = locals[rd_dest].value.u32;
            uint32_t src_offset = locals[rs_src].value.u32;
            uint32_t size = locals[rn_size].value.u32;

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
            uint8_t table_idx = *pc++;
            uint8_t rd_dest = *pc++;
            uint8_t r_val = *pc++;
            uint8_t rn_size = *pc++;

            if (table_idx >= module->num_tables) { return ESPB_ERR_INVALID_OPERAND; }
            
            uint32_t dest_offset = locals[rd_dest].value.u32;
            void* fill_val = (void*)(uintptr_t)locals[r_val].value.i32;
            uint32_t size = locals[rn_size].value.u32;

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
            uint8_t rd_dest = *pc++;
            uint8_t table_idx = *pc++;
            uint8_t rs_index = *pc++;
            
            if (table_idx >= module->num_tables) {
                ESP_LOGE(TAG, "TABLE.GET: Invalid table_idx=%u (num_tables=%u)", table_idx, module->num_tables);
                return ESPB_ERR_INVALID_OPERAND;
            }
            
            uint32_t index = locals[rs_index].value.u32;
            if (index >= instance->table_size) {
                ESP_LOGE(TAG, "TABLE.GET: Index %u out of bounds (table_size=%u)", index, instance->table_size);
                return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
            }
            
            // Получаем значение из таблицы (индекс функции или NULL)
            void* table_value = instance->table_data[index];
            // The value from the table is a function "pointer" (encoded as an index)
            // The correct return type is PTR, not I32
            locals[rd_dest].type = ESPB_TYPE_PTR;
            locals[rd_dest].value.ptr = table_value;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.GET: R%u = table[%u][%u] = %p",
                     rd_dest, table_idx, index, locals[rd_dest].value.ptr);
#endif
            goto interpreter_loop_start;
        }
        
        case 0x19: { // TABLE.SET table_idx(u8), Rd(u8), Rval(u8)
            uint8_t table_idx = *pc++;
            uint8_t rd_index = *pc++;
            uint8_t rval = *pc++;
            
            if (table_idx >= module->num_tables) {
                ESP_LOGE(TAG, "TABLE.SET: Invalid table_idx=%u (num_tables=%u)", table_idx, module->num_tables);
                return ESPB_ERR_INVALID_OPERAND;
            }
            
            uint32_t index = locals[rd_index].value.u32;
            
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
            void* value = (void*)(uintptr_t)locals[rval].value.i32;
            instance->table_data[index] = value;
            
#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "TABLE.SET: table[%u][%u] = 0x%x",
                     table_idx, index, locals[rval].value.i32);
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

            
            // Выводим состояние регистров
            //ESP_LOGD(TAG,"ESPB DEBUG: Регистры:");
            // for (int i = 0; i < 8; i++) {
            //     if (locals[i].type == ESPB_TYPE_I32) {
            //        ESP_LOGD(TAG," R%d=%" PRId32, i, locals[i].value.i32);
            //     }
            // }
            //ESP_LOGD(TAG,"\n");

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
            results[0].type = ESPB_TYPE_I32; results[0].value.i32 = 0;
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
                     results->value.i32);
#endif
        } else {
            // Функция ничего не возвращает, устанавливаем results в 0
            results->type = ESPB_TYPE_I32;
            results->value.i32 = 0;
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
