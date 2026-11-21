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
#include "espb_jit_import_call.h"

#include "ffi.h"

#include "espb_callback_system.h" // FEATURE_CALLBACK_AUTO, espb_auto_create_callbacks_for_import
#include "espb_runtime_ffi_call.h"
#include "espb_runtime_ffi_types.h"
#include "espb_runtime_ffi_pack.h"

#include <stdio.h>
#include <string.h>

// Minimal, JIT-side implementation of CALL_IMPORT.
// Interpreter remains unchanged; this helper is only used from JIT-generated code.
// It relies on:
// - resolved_import_funcs[import_idx]
// - extended type info (0xAA) to correctly prepare variadic calls like printf

/* type_to_ffi moved to espb_runtime_ffi_types.c */
#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

void espb_jit_call_import(EspbInstance *instance,
                          uint16_t import_idx,
                          Value *v_regs,
                          uint16_t num_virtual_regs,
                          uint8_t has_variadic_info,
                          uint8_t num_total_args,
                          const uint8_t *arg_types_u8)
{
#if ESPB_JIT_DEBUG
    printf("[jit] CALL_IMPORT: idx=%u variadic=%u num_total_args=%u v_regs=%p\n",
           (unsigned)import_idx, (unsigned)has_variadic_info, (unsigned)num_total_args, (void*)v_regs);
    printf("[jit] CALL_IMPORT: arg_types_u8=%p (expecting blob addr if variadic)\n", (void*)arg_types_u8);
    if (has_variadic_info && arg_types_u8) {
        printf("[jit] CALL_IMPORT: arg_types from param: ");
        for (uint8_t i = 0; i < num_total_args && i < 4; i++) {
            printf("0x%02X ", arg_types_u8[i]);
        }
        printf("\n");
    }
    
    // Debug first few v_regs
    if (num_total_args > 0 && v_regs) {
        for (uint8_t dbg_i = 0; dbg_i < num_total_args && dbg_i < 4; ++dbg_i) {
            printf("  v_regs[%u]: i32=%d i64=%lld f32=%f f64=%f ptr=%p\n",
                   dbg_i, V_I32(v_regs[dbg_i]), (long long)V_I64(v_regs[dbg_i]),
                   V_F32(v_regs[dbg_i]), V_F64(v_regs[dbg_i]), V_PTR(v_regs[dbg_i]));
        }
    }
#endif
    (void)num_virtual_regs;

    if (!instance || !instance->module || !v_regs) return;

    const EspbModule *module = instance->module;
    if (import_idx >= module->num_imports) return;
    if (module->imports[import_idx].kind != ESPB_IMPORT_KIND_FUNC) return;

    void *fptr = instance->resolved_import_funcs[import_idx];
#if ESPB_JIT_DEBUG
    printf("[jit] CALL_IMPORT: resolved fptr=%p\n", fptr);
#endif
    if (!fptr) return;

    const EspbImportDesc *import_desc = &module->imports[import_idx];
    uint16_t sig_idx = import_desc->desc.func.type_idx;
    if (sig_idx >= module->num_signatures) return;
    const EspbFuncSignature *native_sig = &module->signatures[sig_idx];

    // Determine argument types
    // If extended info (0xAA) is present, use num_total_args explicitly.
    // Otherwise, for variadic imports (e.g. printf), deduce from first v_reg (argc pattern).
    uint32_t nfixedargs = native_sig->num_params;
    uint32_t num_args;
    
    if (has_variadic_info != 0) {
        // Extended info provided (0xAA)
        num_args = (uint32_t)num_total_args;
    } else {
        // Base CALL_IMPORT without 0xAA: use signature params
        // For variadic: treat as 1-arg call (format string only), like interpreter does.
        num_args = (uint32_t)native_sig->num_params;
        
        // Heuristic: if signature has 1 param (PTR) and it's printf-like, it's probably "format only"
        if (num_args == 1 && native_sig->num_params == 1 && native_sig->param_types[0] == ESPB_TYPE_PTR) {
            // This is typical for printf("literal") without additional args
            // num_args = 1 is correct
        }
    }
    
    if (num_args > 16) return;

    ffi_type *arg_types[16];
    void *arg_values[16];

    // Temporary storage for args that need adjustment/conversion.
    // For small-width integer args: ffi_type_sint8/sint16 expects exact width.
    int8_t  tmp_i8[16];
    uint8_t tmp_u8[16];
    int16_t tmp_i16[16];
    uint16_t tmp_u16[16];

    // For 64-bit integer args
    int64_t tmp_i64[16];
    uint64_t tmp_u64[16];

    // For pointers: sometimes ESPB/JIT stores them as offsets into linear memory.
    void *tmp_ptr[16];

    // Pack args for libffi (handles variadic workaround + ptr offset->host translation)
    if (espb_runtime_ffi_pack_args_for_import(instance,
                                              v_regs,
                                              num_virtual_regs,
                                              num_args,
                                              has_variadic_info != 0,
                                              /*apply_varargs_fp_promotion=*/true,
                                              arg_types_u8,
                                              native_sig->param_types,
                                              arg_types,
                                              arg_values,
                                              tmp_i8,
                                              tmp_u8,
                                              tmp_i16,
                                              tmp_u16,
                                              tmp_i64,
                                              tmp_u64,
                                              tmp_ptr) != ESPB_OK) {
        return;
    }

    // Return type
    EspbValueType ret_es = (native_sig->num_returns > 0) ? native_sig->return_types[0] : ESPB_TYPE_VOID;
    ffi_type *ret_type = espb_runtime_type_to_ffi_type(ret_es);
    if (!ret_type) ret_type = &ffi_type_void;

#if ESPB_JIT_DEBUG
    printf("[jit] CALL_IMPORT: ffi_call -> %p\n", fptr);
#endif

    // Auto-create/patch callback arguments (same as interpreter CALL_IMPORT fast path)
    if ((((EspbModule*)instance->module)->header.features & FEATURE_CALLBACK_AUTO) != 0) {
        espb_auto_create_callbacks_for_import(instance, import_idx, arg_values, num_args);
    }

    (void)espb_runtime_ffi_call(fptr,
                               has_variadic_info != 0,
                               (uint32_t)nfixedargs,
                               (uint32_t)num_args,
                               ret_type,
                               arg_types,
                               arg_values,
                               ret_es,
                               v_regs);

#if ESPB_JIT_DEBUG
    printf("[jit] CALL_IMPORT: ffi_call returned\n");
#endif
}
