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
#ifndef ESPB_JIT_INDIRECT_PTR_H
#define ESPB_JIT_INDIRECT_PTR_H

#include <stdint.h>
#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * JIT helper для CALL_INDIRECT_PTR (опкод 0x0D)
 * Вызывает функцию по указателю (target_ptr), с поддержкой func_ptr_map lookup.
 */
void espb_jit_call_indirect_ptr(EspbInstance* instance,
                                void* target_ptr,
                                uint16_t type_idx,
                                Value* v_regs,
                                uint16_t num_virtual_regs,
                                uint16_t func_ptr_reg);

/**
 * JIT helper для CALL_INDIRECT (опкод 0x0B)
 * Вызывает функцию по индексу из регистра func_idx_reg.
 * Поддерживает:
 * - Прямой local_func_idx (если < num_functions)
 * - Указатель/offset в data segment (ищется в func_ptr_map)
 */
void espb_jit_call_indirect(EspbInstance* instance,
                            uint32_t func_idx_or_ptr,
                            uint16_t type_idx,
                            Value* v_regs,
                            uint16_t num_virtual_regs,
                            uint8_t func_idx_reg);

#ifdef __cplusplus
}
#endif

#endif
