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
#include "espb_runtime_ffi_pack.h"

#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

#include "espb_runtime_ffi_types.h"

#include <string.h>

EspbResult espb_runtime_ffi_pack_args_from_regs(const Value *regs,
                                               uint16_t num_regs_allocated,
                                               uint16_t start_reg,
                                               uint32_t arg_count,
                                               const EspbValueType *arg_types_es,
                                               ffi_type **out_arg_types,
                                               void **out_arg_values,
                                               int8_t *tmp_i8,
                                               uint8_t *tmp_u8,
                                               int16_t *tmp_i16,
                                               uint16_t *tmp_u16,
                                               int64_t *tmp_i64,
                                               uint64_t *tmp_u64)
{
    if (!regs || !arg_types_es || !out_arg_types || !out_arg_values) return ESPB_ERR_INVALID_OPERAND;
    if (arg_count > ESPB_FFI_MAX_ARGS) return ESPB_ERR_INVALID_OPERAND;

    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t reg_idx = (uint32_t)start_reg + i;
        if (reg_idx >= num_regs_allocated) return ESPB_ERR_INVALID_REGISTER_INDEX;

        EspbValueType t = arg_types_es[i];
        ffi_type *ft = espb_runtime_type_to_ffi_type(t);
        if (!ft) return ESPB_ERR_INVALID_OPERAND;
        out_arg_types[i] = ft;

        const Value *v = &regs[reg_idx];
        switch (t) {
            case ESPB_TYPE_I8:
                if (!tmp_i8) return ESPB_ERR_INVALID_OPERAND;
                tmp_i8[i] = (int8_t)V_I32(*v);
                out_arg_values[i] = &tmp_i8[i];
                break;
            case ESPB_TYPE_U8:
                if (!tmp_u8) return ESPB_ERR_INVALID_OPERAND;
                tmp_u8[i] = (uint8_t)V_I32(*v);
                out_arg_values[i] = &tmp_u8[i];
                break;
            case ESPB_TYPE_I16:
                if (!tmp_i16) return ESPB_ERR_INVALID_OPERAND;
                tmp_i16[i] = (int16_t)V_I32(*v);
                out_arg_values[i] = &tmp_i16[i];
                break;
            case ESPB_TYPE_U16:
                if (!tmp_u16) return ESPB_ERR_INVALID_OPERAND;
                tmp_u16[i] = (uint16_t)V_I32(*v);
                out_arg_values[i] = &tmp_u16[i];
                break;
            case ESPB_TYPE_I32:
            case ESPB_TYPE_U32:
            case ESPB_TYPE_BOOL:
                out_arg_values[i] = (void*)&V_I32(*v);
                break;
            case ESPB_TYPE_PTR:
                out_arg_values[i] = (void*)&V_PTR(*v);
                break;
            case ESPB_TYPE_F32:
                out_arg_values[i] = (void*)&V_F32(*v);
                break;
            case ESPB_TYPE_F64:
                out_arg_values[i] = (void*)&V_F64(*v);
                break;
            case ESPB_TYPE_I64:
                if (!tmp_i64) return ESPB_ERR_INVALID_OPERAND;
                tmp_i64[i] = V_I64(*v);
                out_arg_values[i] = &tmp_i64[i];
                break;
            case ESPB_TYPE_U64:
                if (!tmp_u64) return ESPB_ERR_INVALID_OPERAND;
                tmp_u64[i] = (uint64_t)V_I64(*v);
                out_arg_values[i] = &tmp_u64[i];
                break;
            default:
                return ESPB_ERR_INVALID_OPERAND;
        }
    }

    return ESPB_OK;
}

EspbResult espb_runtime_ffi_pack_args_for_import(EspbInstance *instance,
                                                const Value *regs,
                                                uint16_t num_regs_allocated,
                                                uint32_t arg_count,
                                                bool has_variadic_info,
                                                bool apply_varargs_fp_promotion,
                                                const uint8_t *raw_types_u8,
                                                const EspbValueType *fixed_param_types,
                                                ffi_type **out_arg_types,
                                                void **out_arg_values,
                                                int8_t *tmp_i8,
                                                uint8_t *tmp_u8,
                                                int16_t *tmp_i16,
                                                uint16_t *tmp_u16,
                                                int64_t *tmp_i64,
                                                uint64_t *tmp_u64,
                                                void **tmp_ptr)
{
#if ESPB_JIT_DEBUG
    printf("[ffi_pack] ENTER: arg_count=%u has_variadic_info=%d raw_types_u8=%p\n", 
           arg_count, has_variadic_info, (void*)raw_types_u8);
    if (has_variadic_info && raw_types_u8) {
        printf("[ffi_pack]   arg_types: ");
        for (uint32_t i = 0; i < arg_count && i < 8; i++) {
            printf("0x%02X ", raw_types_u8[i]);
        }
        printf("\n");
    }
#endif
    
    if (!instance || !regs || !out_arg_types || !out_arg_values) {
#if ESPB_JIT_DEBUG
        printf("[ffi_pack] ERROR: NULL params check failed\n");
#endif
        return ESPB_ERR_INVALID_OPERAND;
    }
    if (arg_count > ESPB_FFI_MAX_ARGS) return ESPB_ERR_INVALID_OPERAND;

    for (uint32_t i = 0; i < arg_count; i++) {
        if (i >= num_regs_allocated) return ESPB_ERR_INVALID_REGISTER_INDEX;

        EspbValueType raw_t;
        if (has_variadic_info) {
            if (!raw_types_u8) return ESPB_ERR_INVALID_OPERAND;
            raw_t = (EspbValueType)raw_types_u8[i];
        } else {
            if (!fixed_param_types) return ESPB_ERR_INVALID_OPERAND;
            raw_t = fixed_param_types[i];
        }

        // Optional varargs FP promotion (RISC-V ABI workaround)
        EspbValueType t = raw_t;
        if (has_variadic_info && apply_varargs_fp_promotion) {
            if (t == ESPB_TYPE_F64) t = ESPB_TYPE_U64;
            else if (t == ESPB_TYPE_F32) t = ESPB_TYPE_U32;
        }

        // Pointer translation
        if (t == ESPB_TYPE_PTR) {
            if (!tmp_ptr) return ESPB_ERR_INVALID_OPERAND;
            out_arg_types[i] = espb_runtime_type_to_ffi_type(ESPB_TYPE_PTR);
            if (!out_arg_types[i]) return ESPB_ERR_INVALID_OPERAND;

            // Interpreter model: Value stores PTR as a host pointer already.
            // - LDC.PTR.IMM / ALLOCA / heap produce host pointers (mem_base + offset)
            // - INTTOPTR produces raw host pointers
            // Therefore JIT must pass PTR as-is and must NOT translate small values as offsets.
            void *raw = V_PTR(regs[i]);
            tmp_ptr[i] = raw;
            out_arg_values[i] = &tmp_ptr[i];
            continue;
        }

        ffi_type *ft = espb_runtime_type_to_ffi_type(t);
        if (!ft) return ESPB_ERR_INVALID_OPERAND;
        out_arg_types[i] = ft;

        // Pack scalars
        switch (t) {
            case ESPB_TYPE_I8:
                if (!tmp_i8) return ESPB_ERR_INVALID_OPERAND;
                tmp_i8[i] = (int8_t)V_I32(regs[i]);
                out_arg_values[i] = &tmp_i8[i];
                break;
            case ESPB_TYPE_U8:
                if (!tmp_u8) return ESPB_ERR_INVALID_OPERAND;
                tmp_u8[i] = (uint8_t)V_I32(regs[i]);
                out_arg_values[i] = &tmp_u8[i];
                break;
            case ESPB_TYPE_I16:
                if (!tmp_i16) return ESPB_ERR_INVALID_OPERAND;
                tmp_i16[i] = (int16_t)V_I32(regs[i]);
                out_arg_values[i] = &tmp_i16[i];
                break;
            case ESPB_TYPE_U16:
                if (!tmp_u16) return ESPB_ERR_INVALID_OPERAND;
                tmp_u16[i] = (uint16_t)V_I32(regs[i]);
                out_arg_values[i] = &tmp_u16[i];
                break;
            case ESPB_TYPE_I32:
            case ESPB_TYPE_U32:
            case ESPB_TYPE_BOOL:
                out_arg_values[i] = (void*)&V_I32(regs[i]);
                break;
            case ESPB_TYPE_I64:
                if (!tmp_i64) return ESPB_ERR_INVALID_OPERAND;
                tmp_i64[i] = V_I64(regs[i]);
                out_arg_values[i] = &tmp_i64[i];
                break;
            case ESPB_TYPE_U64:
                if (!tmp_u64) return ESPB_ERR_INVALID_OPERAND;
                tmp_u64[i] = (uint64_t)V_I64(regs[i]);
                out_arg_values[i] = &tmp_u64[i];
                break;
            case ESPB_TYPE_F32:
                out_arg_values[i] = (void*)&V_F32(regs[i]);
                break;
            case ESPB_TYPE_F64:
                out_arg_values[i] = (void*)&V_F64(regs[i]);
                break;
            default:
                return ESPB_ERR_INVALID_OPERAND;
        }
    }

    return ESPB_OK;
}
