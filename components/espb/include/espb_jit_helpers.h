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
#ifndef ESPB_JIT_HELPERS_H
#define ESPB_JIT_HELPERS_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JIT helper for ALLOCA opcode (0x8F).
 * Allocates memory on heap with alignment, tracks via RuntimeFrame, frees on function exit.
 */
void espb_jit_alloca(EspbInstance *instance, Value *v_regs, uint8_t rd, uint8_t rs_size, uint8_t align);

// Extended ABI: num_regs_allocated must match actual length of v_regs array.
void espb_jit_alloca_ex(EspbInstance *instance, Value *v_regs, uint16_t num_regs_allocated,
                        uint8_t rd, uint8_t rs_size, uint8_t align);

#ifdef __cplusplus
}
#endif

#endif // ESPB_JIT_HELPERS_H
