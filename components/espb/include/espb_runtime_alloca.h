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
#ifndef ESPB_RUNTIME_ALLOCA_H
#define ESPB_RUNTIME_ALLOCA_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared implementation of ESPB opcode ALLOCA (0x8F).
 *
 * This helper contains no JIT-specific code and can be used by:
 * - interpreter (opcode handler)
 * - JIT backends (RISC-V today, Xtensa in future)
 */
EspbResult espb_runtime_alloca(EspbInstance *instance,
                              ExecutionContext *exec_ctx,
                              Value *regs,
                              uint16_t num_virtual_regs,
                              uint8_t rd,
                              uint8_t rs_size,
                              uint8_t align);

#ifdef __cplusplus
}
#endif

#endif // ESPB_RUNTIME_ALLOCA_H
