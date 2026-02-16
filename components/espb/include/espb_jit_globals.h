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
#ifndef ESPB_JIT_GLOBALS_H
#define ESPB_JIT_GLOBALS_H

#include <stdint.h>
#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Writes result into v_regs[rd]
void espb_jit_ld_global_addr(EspbInstance* instance, uint16_t symbol_idx, Value* v_regs, uint16_t num_virtual_regs, uint8_t rd);
void espb_jit_ld_global(EspbInstance* instance, uint16_t global_idx, Value* v_regs, uint16_t num_virtual_regs, uint8_t rd);
void espb_jit_st_global(EspbInstance* instance, uint16_t global_idx, const Value* v_regs, uint16_t num_virtual_regs, uint8_t rs);

#ifdef __cplusplus
}
#endif

#endif
