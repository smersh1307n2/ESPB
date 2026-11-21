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
#include "espb_runtime_call_import_basic.h"

#include "ffi.h"
#include "espb_runtime_ffi_types.h"

#include <string.h>

EspbResult espb_runtime_call_import_basic(EspbInstance *instance,
                                         uint16_t import_idx,
                                         Value *regs,
                                         uint16_t num_virtual_regs,
                                         bool has_variadic_info,
                                         uint8_t num_total_args,
                                         const EspbValueType *arg_types)
{
    if (!instance || !instance->module || !regs) return ESPB_ERR_INVALID_OPERAND;
    const EspbModule *module = (const EspbModule*)instance->module;

    if (import_idx >= module->num_imports || module->imports[import_idx].kind != ESPB_IMPORT_KIND_FUNC) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    const EspbImportDesc *import_desc = &module->imports[import_idx];
    uint16_t sig_idx = import_desc->desc.func.type_idx;
    const EspbFuncSignature *native_sig = &module->signatures[sig_idx];

    uint32_t num_args = has_variadic_info ? (uint32_t)num_total_args : native_sig->num_params;
    uint32_t nfixedargs = native_sig->num_params;

#ifndef ESPB_CALL_IMPORT_MAX_ARGS
#define ESPB_CALL_IMPORT_MAX_ARGS 16
#endif

    if (num_args > ESPB_CALL_IMPORT_MAX_ARGS) return ESPB_ERR_INVALID_OPERAND;

    void *fptr = instance->resolved_import_funcs[import_idx];
    if (!fptr) return ESPB_ERR_IMPORT_RESOLUTION_FAILED;

    ffi_type *ffi_arg_types[ESPB_CALL_IMPORT_MAX_ARGS];
    void *ffi_arg_values[ESPB_CALL_IMPORT_MAX_ARGS];

    int64_t temp_i64[ESPB_CALL_IMPORT_MAX_ARGS];
    uint64_t temp_u64[ESPB_CALL_IMPORT_MAX_ARGS];

    for (uint32_t i = 0; i < num_args; i++) {
        EspbValueType t;
        if (has_variadic_info) {
            t = arg_types ? arg_types[i] : ESPB_TYPE_I32;
        } else {
            t = native_sig->param_types[i];
        }

        ffi_arg_types[i] = espb_runtime_type_to_ffi_type(t);
        if (!ffi_arg_types[i]) return ESPB_ERR_INVALID_OPERAND;

        if (i >= num_virtual_regs) return ESPB_ERR_INVALID_REGISTER_INDEX;

        switch (t) {
            case ESPB_TYPE_I8: case ESPB_TYPE_U8:
            case ESPB_TYPE_I16: case ESPB_TYPE_U16:
            case ESPB_TYPE_I32: case ESPB_TYPE_U32:
            case ESPB_TYPE_BOOL:
                ffi_arg_values[i] = &V_I32(regs[i]);
                break;
            case ESPB_TYPE_I64:
                temp_i64[i] = V_I64(regs[i]);
                ffi_arg_values[i] = &temp_i64[i];
                break;
            case ESPB_TYPE_U64:
                temp_u64[i] = (uint64_t)V_I64(regs[i]);
                ffi_arg_values[i] = &temp_u64[i];
                break;
            case ESPB_TYPE_F32:
                ffi_arg_values[i] = &V_F32(regs[i]);
                break;
            case ESPB_TYPE_F64:
                ffi_arg_values[i] = &V_F64(regs[i]);
                break;
            case ESPB_TYPE_PTR:
                ffi_arg_values[i] = &V_PTR(regs[i]);
                break;
            default:
                return ESPB_ERR_INVALID_OPERAND;
        }
    }

    EspbValueType ret_t = (native_sig->num_returns > 0) ? native_sig->return_types[0] : ESPB_TYPE_VOID;
    ffi_type *ffi_ret_type = espb_runtime_type_to_ffi_type(ret_t);
    if (!ffi_ret_type) return ESPB_ERR_INVALID_OPERAND;

    ffi_cif cif;
    ffi_status st;
    if (has_variadic_info) {
        st = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, nfixedargs, num_args, ffi_ret_type, ffi_arg_types);
    } else {
        st = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_args, ffi_ret_type, ffi_arg_types);
    }
    if (st != FFI_OK) return ESPB_ERR_INVALID_OPERAND;

    union { int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; float f32; double f64; void *p; } ret;
    memset(&ret, 0, sizeof(ret));

    ffi_call(&cif, FFI_FN(fptr), &ret, ffi_arg_values);

    if (native_sig->num_returns > 0) {
        switch (ret_t) {
            case ESPB_TYPE_I32: case ESPB_TYPE_BOOL: SET_TYPE(regs[0], ESPB_TYPE_I32); V_I32(regs[0]) = ret.i32; break;
            case ESPB_TYPE_U32: SET_TYPE(regs[0], ESPB_TYPE_I32); V_I32(regs[0]) = (int32_t)ret.u32; break;
            case ESPB_TYPE_I64: SET_TYPE(regs[0], ESPB_TYPE_I64); V_I64(regs[0]) = ret.i64; break;
            case ESPB_TYPE_U64: SET_TYPE(regs[0], ESPB_TYPE_I64); V_I64(regs[0]) = (int64_t)ret.u64; break;
            case ESPB_TYPE_F32: SET_TYPE(regs[0], ESPB_TYPE_F32); V_F32(regs[0]) = ret.f32; break;
            case ESPB_TYPE_F64: SET_TYPE(regs[0], ESPB_TYPE_F64); V_F64(regs[0]) = ret.f64; break;
            case ESPB_TYPE_PTR: SET_TYPE(regs[0], ESPB_TYPE_PTR); V_PTR(regs[0]) = ret.p; break;
            default: break;
        }
    }

    return ESPB_OK;
}
