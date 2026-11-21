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
#ifndef ESPB_RUNTIME_FFI_CALL_H
#define ESPB_RUNTIME_FFI_CALL_H

#include <stdbool.h>
#include <stdint.h>

#include "ffi.h"
#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared low-level libffi call helper.
 *
 * This helper is shared between interpreter and JIT backends.
 * It does not perform argument marshalling by itself; callers must prepare:
 * - arg_types[arg_count]
 * - arg_values[arg_count]
 *
 * It does:
 * - ffi_prep_cif / ffi_prep_cif_var
 * - ffi_call
 * - write return value to regs[0] (if ret_es_type != VOID)
 */
// Full helper (prepare + call + store to regs[0]) - convenient for JIT.
EspbResult espb_runtime_ffi_call(void *fptr,
                                bool is_variadic,
                                uint32_t nfixedargs,
                                uint32_t nargs,
                                ffi_type *ret_ffi_type,
                                ffi_type **arg_types,
                                void **arg_values,
                                EspbValueType ret_es_type,
                                Value *regs);

// Low-level helpers (for interpreter which may need prepared cif for wrappers).
void espb_runtime_ffi_call_prepared(ffi_cif *cif, void *fptr, void *ret_storage, void **arg_values);
void espb_runtime_store_ffi_ret(Value *regs, uint8_t ret_reg, EspbValueType ret_es_type, const void *ret_storage);

#ifdef __cplusplus
}
#endif

#endif // ESPB_RUNTIME_FFI_CALL_H
