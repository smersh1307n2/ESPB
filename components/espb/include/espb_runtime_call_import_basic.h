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
#ifndef ESPB_RUNTIME_CALL_IMPORT_BASIC_H
#define ESPB_RUNTIME_CALL_IMPORT_BASIC_H

#include <stdint.h>
#include <stdbool.h>

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Basic CALL_IMPORT implementation shared between interpreter and JIT.
 *
 * This helper covers:
 * - resolving import function pointer
 * - preparing ffi_cif (including variadic via extended type info)
 * - packing arguments from VM registers
 * - ffi_call
 * - storing return value to regs[0]
 *
 * It intentionally does NOT handle:
 * - immeta marshalling (std/async wrappers)
 * - advanced callback mapping via cbmeta (can stay in interpreter for now)
 */
EspbResult espb_runtime_call_import_basic(EspbInstance *instance,
                                         uint16_t import_idx,
                                         Value *regs,
                                         uint16_t num_virtual_regs,
                                         bool has_variadic_info,
                                         uint8_t num_total_args,
                                         const EspbValueType *arg_types);

#ifdef __cplusplus
}
#endif

#endif // ESPB_RUNTIME_CALL_IMPORT_BASIC_H
