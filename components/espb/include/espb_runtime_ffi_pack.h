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
#ifndef ESPB_RUNTIME_FFI_PACK_H
#define ESPB_RUNTIME_FFI_PACK_H

#include <stdint.h>
#include <stdbool.h>

#include "espb_interpreter_common_types.h"
#include "ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESPB_FFI_MAX_ARGS
#define ESPB_FFI_MAX_ARGS 32
#endif

/**
 * @brief Pack arguments from VM registers into libffi arg_types/arg_values arrays.
 *
 * The caller provides:
 * - regs: VM registers array
 * - num_regs_allocated: length of regs array
 * - start_reg: first register index to read arguments from
 * - arg_count: number of arguments
 * - arg_types_es: ESPB types for each argument
 *
 * The function fills:
 * - out_arg_types[i]
 * - out_arg_values[i]
 *
 * Notes:
 * - For 32-bit values we pass &V_I32/&V_F32 directly.
 * - For 64-bit values we copy to per-call scratch arrays (out_tmp_i64/out_tmp_u64)
 *   to guarantee proper storage/alignment.
 */
// Generic packer (used e.g. for CALL_INDIRECT_PTR native calls)
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
                                               uint64_t *tmp_u64);

#ifdef __cplusplus
}
#endif


/**
 * @brief Packer for JIT CALL_IMPORT semantics.
 *
 * Handles:
 * - variadic arg types via raw_types_u8
 * - pointer offset->host translation (same heuristic as JIT import call)
 *
 * Expects arguments to start at R0.
 */
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
                                                void **tmp_ptr);

#endif // ESPB_RUNTIME_FFI_PACK_H
