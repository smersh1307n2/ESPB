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
#include "espb_runtime_ffi_call.h"

#include <string.h>

EspbResult espb_runtime_ffi_call(void *fptr,
                                bool is_variadic,
                                uint32_t nfixedargs,
                                uint32_t nargs,
                                ffi_type *ret_ffi_type,
                                ffi_type **arg_types,
                                void **arg_values,
                                EspbValueType ret_es_type,
                                Value *regs)
{
    if (!fptr || !ret_ffi_type || (!regs && ret_es_type != ESPB_TYPE_VOID)) return ESPB_ERR_INVALID_OPERAND;

    ffi_cif cif;
    ffi_status st;
    if (is_variadic) {
        st = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, nfixedargs, nargs, ret_ffi_type, arg_types);
    } else {
        st = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ret_ffi_type, arg_types);
    }
    if (st != FFI_OK) return ESPB_ERR_INVALID_OPERAND;

    union { int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; float f32; double f64; void *p; } ret;
    memset(&ret, 0, sizeof(ret));

    espb_runtime_ffi_call_prepared(&cif, fptr, &ret, arg_values);

    if (ret_es_type != ESPB_TYPE_VOID) {
        espb_runtime_store_ffi_ret(regs, 0, ret_es_type, &ret);
    }

    return ESPB_OK;
}

void espb_runtime_ffi_call_prepared(ffi_cif *cif, void *fptr, void *ret_storage, void **arg_values) {
    ffi_call(cif, FFI_FN(fptr), ret_storage, arg_values);
}

void espb_runtime_store_ffi_ret(Value *regs, uint8_t ret_reg, EspbValueType ret_es_type, const void *ret_storage) {
    if (!regs) return;
    const union { int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; float f32; double f64; void *p; } *ret =
        (const void*)ret_storage;

    switch (ret_es_type) {
        case ESPB_TYPE_I32:
        case ESPB_TYPE_BOOL:
        case ESPB_TYPE_I8:
        case ESPB_TYPE_U8:
        case ESPB_TYPE_I16:
        case ESPB_TYPE_U16:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_I32);
            V_I32(regs[ret_reg]) = ret->i32;
            break;
        case ESPB_TYPE_U32:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_I32);
            V_I32(regs[ret_reg]) = (int32_t)ret->u32;
            break;
        case ESPB_TYPE_I64:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_I64);
            V_I64(regs[ret_reg]) = ret->i64;
            break;
        case ESPB_TYPE_U64:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_I64);
            V_I64(regs[ret_reg]) = (int64_t)ret->u64;
            break;
        case ESPB_TYPE_F32:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_F32);
            V_F32(regs[ret_reg]) = ret->f32;
            break;
        case ESPB_TYPE_F64:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_F64);
            V_F64(regs[ret_reg]) = ret->f64;
            break;
        case ESPB_TYPE_PTR:
            SET_TYPE(regs[ret_reg], ESPB_TYPE_PTR);
            V_PTR(regs[ret_reg]) = ret->p;
            break;
        default:
            break;
    }
}
