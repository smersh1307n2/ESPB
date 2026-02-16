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
#ifndef ESPB_JIT_IMPORT_CALL_H
#define ESPB_JIT_IMPORT_CALL_H

#include <stdint.h>
#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CALL_IMPORT helper for JIT.
 *
 * Implements the same semantics as interpreter op_0x09 (FFI call):
 * - callbacks (cbmeta + auto_active)
 * - immeta marshalling (standard + async out wrappers)
 * - variadic/extended type info (0xAA)
 * - blocking imports shadow-stack save/restore
 */
void espb_jit_call_import(EspbInstance *instance,
                          uint16_t import_idx,
                          Value *v_regs,
                          uint16_t num_virtual_regs,
                          uint8_t has_variadic_info,
                          uint8_t num_total_args,
                          const uint8_t *arg_types_u8);

#ifdef __cplusplus
}
#endif

#endif // ESPB_JIT_IMPORT_CALL_H
