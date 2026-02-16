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
#include "espb_jit_helpers.h"

#include "espb_runtime_alloca.h"

// JIT helper for ALLOCA opcode (0x8F).
// Shared implementation lives in espb_runtime_alloca.c.
void espb_jit_alloca_ex(EspbInstance *instance, Value *v_regs, uint16_t num_regs_allocated,
                        uint8_t rd, uint8_t rs_size, uint8_t align)
{
    if (!instance || !v_regs) return;
    (void)espb_runtime_alloca(instance, NULL, v_regs, num_regs_allocated, rd, rs_size, align);
}

void espb_jit_alloca(EspbInstance *instance, Value *v_regs, uint8_t rd, uint8_t rs_size, uint8_t align) {
    // Backward-compatible wrapper
    espb_jit_alloca_ex(instance, v_regs, 256, rd, rs_size, align);
}

