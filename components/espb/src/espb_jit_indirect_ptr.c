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
#include "espb_jit_indirect_ptr.h"

#include "espb_api.h"
#include "espb_interpreter_runtime_oc.h" // init_execution_context, espb_call_function
#include "ffi.h"
#include "espb_runtime_ffi_call.h"
#include "espb_runtime_ffi_types.h"
#include "espb_runtime_ffi_pack.h"

#include <string.h>
#include <stdlib.h>

// comparator for bsearch
static int compare_func_ptr_map_entry_for_search(const void *key, const void *element) {
    uint32_t k = *(const uint32_t*)key;
    const EspbFuncPtrMapEntry* e = (const EspbFuncPtrMapEntry*)element;
    if (k < e->data_offset) return -1;
    if (k > e->data_offset) return 1;
    return 0;
}

void espb_jit_call_indirect_ptr(EspbInstance* instance,
                                void* target_ptr,
                                uint16_t type_idx,
                                Value* v_regs,
                                uint16_t num_virtual_regs,
                                uint16_t func_ptr_reg) {
    if (!instance || !instance->module || !v_regs || !target_ptr) return;

    EspbModule* module = (EspbModule*)instance->module;

    // Determine if pointer is inside ESPB memory or is a tagged DATA_OFFSET pointer
    uintptr_t mem_base = (uintptr_t)instance->memory_data;
    uintptr_t mem_end  = mem_base + instance->memory_size_bytes;

    uintptr_t raw_ptr = (uintptr_t)target_ptr;
    bool is_tagged_data_offset = ((raw_ptr & (uintptr_t)CALLBACK_FLAG_BIT) != 0);

    uint32_t data_offset = 0;
    bool is_in_data_segment = false;

    if (is_tagged_data_offset) {
        // Tagged pointer: lower 31 bits are offset into memory_data
        data_offset = (uint32_t)(raw_ptr & ~((uintptr_t)CALLBACK_FLAG_BIT));
        is_in_data_segment = (mem_base != 0) && ((uintptr_t)(mem_base + data_offset) < mem_end);
        target_ptr = (void*)(mem_base + data_offset);
    } else {
        is_in_data_segment = (mem_base != 0) && (raw_ptr >= mem_base) && (raw_ptr < mem_end);
        if (is_in_data_segment) {
            data_offset = (uint32_t)(raw_ptr - mem_base);
        }
    }

    if (is_in_data_segment && module->func_ptr_map && module->num_func_ptr_map_entries > 0) {
        EspbFuncPtrMapEntry* found = (EspbFuncPtrMapEntry*)bsearch(
            &data_offset,
            module->func_ptr_map,
            module->num_func_ptr_map_entries,
            sizeof(EspbFuncPtrMapEntry),
            compare_func_ptr_map_entry_for_search);

        if (found) {
            uint32_t local_idx = found->function_index;
            if (local_idx >= module->num_functions) return;

            uint16_t actual_sig_idx = module->function_signature_indices[local_idx];
            // For now: require exact match like interpreter fast path.
            // (Interpreter allows compatibility in some cases; implement later if needed.)
            if (actual_sig_idx != type_idx) {
                // Для JIT helper используем строгую проверку индекса сигнатуры.
                // (Совместимость сигнатур, как в интерпретаторе, можно добавить позже.)
                return;
            }

            // Call ESPB function by index
            EspbFuncSignature* sig = &module->signatures[actual_sig_idx];
            uint8_t num_args = sig->num_params;
            if (num_args > 8) num_args = 8;

            Value args[8];
            memset(args, 0, sizeof(args));
            for (uint8_t i = 0; i < num_args; ++i) {
                // Arguments are in R0, R1, R2, ...
                // Skip func_ptr_reg if it occupies an argument slot
                uint16_t src_reg = i;
                if (func_ptr_reg <= src_reg) {
                    src_reg++;
                }
                if (src_reg < num_virtual_regs) args[i] = v_regs[src_reg];
            }

            ExecutionContext* exec_ctx = init_execution_context();
            if (!exec_ctx) return;

            uint32_t global_idx = local_idx + module->num_imported_funcs;
            Value result;
            memset(&result, 0, sizeof(result));
            espb_call_function(instance, exec_ctx, global_idx, args, &result);
            if (sig->num_returns > 0) {
                v_regs[0] = result;
            }
            free_execution_context(exec_ctx);
            return;
        }

        // pointer inside data segment but not in map => invalid
        return;
    }

    // Native call via libffi using signature type_idx
    if (type_idx >= module->num_signatures) return;
    EspbFuncSignature* func_sig = &module->signatures[type_idx];
    if (func_sig->num_params > 32) return;

    ffi_type* ffi_arg_types[32];
    void* ffi_arg_values[32];

    int8_t  tmp_i8[ESPB_FFI_MAX_ARGS];
    uint8_t tmp_u8[ESPB_FFI_MAX_ARGS];
    int16_t tmp_i16[ESPB_FFI_MAX_ARGS];
    uint16_t tmp_u16[ESPB_FFI_MAX_ARGS];
    int64_t tmp_i64[ESPB_FFI_MAX_ARGS];
    uint64_t tmp_u64[ESPB_FFI_MAX_ARGS];

    // Build args skipping func_ptr_reg if it occupies an argument slot
    for (uint32_t i = 0; i < func_sig->num_params; i++) {
        uint32_t src_reg = i;
        if (func_ptr_reg <= src_reg) {
            src_reg++;
        }
        if (src_reg >= num_virtual_regs) return;

        EspbValueType t = func_sig->param_types[i];
        ffi_type *ft = espb_runtime_type_to_ffi_type(t);
        if (!ft) return;
        ffi_arg_types[i] = ft;

        const Value *v = &v_regs[src_reg];
        switch (t) {
            case ESPB_TYPE_I8:
                tmp_i8[i] = (int8_t)V_I32(*v);
                ffi_arg_values[i] = &tmp_i8[i];
                break;
            case ESPB_TYPE_U8:
                tmp_u8[i] = (uint8_t)V_I32(*v);
                ffi_arg_values[i] = &tmp_u8[i];
                break;
            case ESPB_TYPE_I16:
                tmp_i16[i] = (int16_t)V_I32(*v);
                ffi_arg_values[i] = &tmp_i16[i];
                break;
            case ESPB_TYPE_U16:
                tmp_u16[i] = (uint16_t)V_I32(*v);
                ffi_arg_values[i] = &tmp_u16[i];
                break;
            case ESPB_TYPE_I32:
            case ESPB_TYPE_U32:
            case ESPB_TYPE_BOOL:
                ffi_arg_values[i] = (void*)&V_I32(*v);
                break;
            case ESPB_TYPE_PTR:
                ffi_arg_values[i] = (void*)&V_PTR(*v);
                break;
            case ESPB_TYPE_F32:
                ffi_arg_values[i] = (void*)&V_F32(*v);
                break;
            case ESPB_TYPE_F64:
                ffi_arg_values[i] = (void*)&V_F64(*v);
                break;
            case ESPB_TYPE_I64:
                tmp_i64[i] = V_I64(*v);
                ffi_arg_values[i] = &tmp_i64[i];
                break;
            case ESPB_TYPE_U64:
                tmp_u64[i] = (uint64_t)V_I64(*v);
                ffi_arg_values[i] = &tmp_u64[i];
                break;
            default:
                return;
        }
    }

    ffi_type* ffi_ret_type = &ffi_type_void;
    if (func_sig->num_returns > 0) {
        ffi_ret_type = espb_runtime_type_to_ffi_type(func_sig->return_types[0]);
        if (!ffi_ret_type) return;
    }

    EspbValueType ret_es = (func_sig->num_returns > 0) ? func_sig->return_types[0] : ESPB_TYPE_VOID;
    (void)espb_runtime_ffi_call(target_ptr,
                               /*is_variadic=*/false,
                               /*nfixedargs=*/0,
                               /*nargs=*/(uint32_t)func_sig->num_params,
                               ffi_ret_type,
                               ffi_arg_types,
                               ffi_arg_values,
                               ret_es,
                               v_regs);
}

// =============================================================================
// espb_jit_call_indirect - JIT helper для CALL_INDIRECT (опкод 0x0B)
// =============================================================================
// Логика аналогична интерпретатору op_0x0B:
// 1. Если func_idx_or_ptr < num_functions - это прямой local_func_idx
// 2. Иначе - это указатель/offset, ищем в func_ptr_map
// =============================================================================
void espb_jit_call_indirect(EspbInstance* instance,
                            uint32_t func_idx_or_ptr,
                            uint16_t type_idx,
                            Value* v_regs,
                            uint16_t num_virtual_regs,
                            uint8_t func_idx_reg) {
    if (!instance || !instance->module || !v_regs) return;

    EspbModule* module = (EspbModule*)instance->module;
    uint32_t local_func_idx = func_idx_or_ptr;
    
    // Если значение >= num_functions, это может быть указатель/offset в data segment
    if (local_func_idx >= module->num_functions) {
        uintptr_t mem_base = (uintptr_t)instance->memory_data;
        uintptr_t mem_end = mem_base + instance->memory_size_bytes;
        uintptr_t ptr_val = (uintptr_t)func_idx_or_ptr;
        uint32_t data_offset = 0;
        bool found_offset = false;
        
        if (ptr_val >= mem_base && ptr_val < mem_end) {
            // Сценарий 1: ptr_val - это абсолютный указатель в memory_data
            data_offset = (uint32_t)(ptr_val - mem_base);
            found_offset = true;
        } else if (ptr_val > 0 && ptr_val < instance->memory_size_bytes) {
            // Сценарий 2: ptr_val - это offset в data segment (относительный от начала)
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
                local_func_idx = found_entry->function_index;
            } else {
                // Не найдено в func_ptr_map - невалидный вызов
                return;
            }
        } else {
            // Невалидный указатель или пустая func_ptr_map
            return;
        }
    }
    
    // Проверяем валидность local_func_idx после возможного разрешения через func_ptr_map
    if (local_func_idx >= module->num_functions) {
        return;
    }
    
    // Проверка сигнатуры
    uint16_t actual_sig_idx = module->function_signature_indices[local_func_idx];
    if (actual_sig_idx != type_idx) {
        // Строгая проверка сигнатуры (как в интерпретаторе можно добавить совместимость позже)
        return;
    }
    
    // Получаем сигнатуру и вызываем функцию
    EspbFuncSignature* sig = &module->signatures[actual_sig_idx];
    uint8_t num_args = sig->num_params;
    if (num_args > 8) num_args = 8;
    
    // Копируем аргументы из v_regs (R0, R1, R2, ...)
    // Аргументы уже находятся в правильных регистрах для CALL_INDIRECT
    Value args[8];
    memset(args, 0, sizeof(args));
    for (uint8_t i = 0; i < num_args; ++i) {
        if (i < num_virtual_regs) {
            args[i] = v_regs[i];
        }
    }
    
    // Создаем контекст выполнения и вызываем функцию
    ExecutionContext* exec_ctx = init_execution_context();
    if (!exec_ctx) return;
    
    uint32_t global_func_idx = local_func_idx + module->num_imported_funcs;
    Value result;
    memset(&result, 0, sizeof(result));
    espb_call_function(instance, exec_ctx, global_func_idx, args, &result);
    
    // Сохраняем результат в v_regs[0]
    if (sig->num_returns > 0) {
        v_regs[0] = result;
    }
    
    free_execution_context(exec_ctx);
}
