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
#include "espb_runtime_alloca.h"

#include "espb_heap_manager.h"

#include <string.h>

EspbResult espb_runtime_alloca(EspbInstance *instance,
                              ExecutionContext *exec_ctx,
                              Value *regs,
                              uint16_t num_virtual_regs,
                              uint8_t rd,
                              uint8_t rs_size,
                              uint8_t align)
{
    if (!instance || !regs) return ESPB_ERR_INVALID_OPERAND;
    if (rd >= num_virtual_regs || rs_size >= num_virtual_regs) return ESPB_ERR_INVALID_REGISTER_INDEX;

    if (align == 0 || (align & (align - 1)) != 0) {
        align = 4;
    }

    uint32_t size_to_alloc = (uint32_t)V_I32(regs[rs_size]);
    if (size_to_alloc == 0 || size_to_alloc > 65536u) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    size_t required_alignment = (align > 8) ? align : 8;
    void *allocated_ptr = espb_heap_malloc_aligned(instance, size_to_alloc, required_alignment);
    if (!allocated_ptr) {
        return ESPB_ERR_OUT_OF_MEMORY;
    }

    memset(allocated_ptr, 0, size_to_alloc);

    // Safety: ensure pointer is within linear memory range
    if (instance->memory_data && instance->memory_size_bytes) {
        uintptr_t ptr_addr = (uintptr_t)allocated_ptr;
        uintptr_t mem_base = (uintptr_t)instance->memory_data;
        uintptr_t mem_end  = mem_base + (uintptr_t)instance->memory_size_bytes;
        if (ptr_addr < mem_base || ptr_addr >= mem_end) {
            espb_heap_free(instance, allocated_ptr);
            return ESPB_ERR_OUT_OF_MEMORY;
        }
    }

    // Track allocation for frame cleanup (interpreter path)
    if (exec_ctx && exec_ctx->call_stack && exec_ctx->call_stack_top > 0) {
        RuntimeFrame *frame = &exec_ctx->call_stack[exec_ctx->call_stack_top - 1];
        if (frame->alloca_count >= 32) {
            espb_heap_free(instance, allocated_ptr);
            return ESPB_ERR_OUT_OF_MEMORY;
        }
        frame->alloca_ptrs[frame->alloca_count++] = allocated_ptr;
        if (required_alignment > 4) {
            frame->has_custom_aligned = true;
        }
    }

    SET_TYPE(regs[rd], ESPB_TYPE_PTR);
    V_PTR(regs[rd]) = allocated_ptr;
    return ESPB_OK;
}
