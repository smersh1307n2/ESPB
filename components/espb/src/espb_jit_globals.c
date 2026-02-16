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
#include "espb_jit_globals.h"

#include <string.h>
#include <stdint.h>

#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

static inline uint8_t espb_value_size(EspbValueType t) {
    switch (t) {
        case ESPB_TYPE_I8:  case ESPB_TYPE_U8:  return 1;
        case ESPB_TYPE_I16: case ESPB_TYPE_U16: return 2;
        case ESPB_TYPE_I32: case ESPB_TYPE_U32: case ESPB_TYPE_F32: case ESPB_TYPE_BOOL: case ESPB_TYPE_PTR: return 4;
        case ESPB_TYPE_I64: case ESPB_TYPE_U64: case ESPB_TYPE_F64: return 8;
        default: return 8;
    }
}

void espb_jit_ld_global_addr(EspbInstance* instance, uint16_t symbol_idx, Value* v_regs, uint16_t num_virtual_regs, uint8_t rd) {
#if ESPB_JIT_DEBUG
    // Debug: print return address to understand where we're called from
    void* ret_addr = __builtin_return_address(0);
    printf("[jit] LD_GLOBAL_ADDR: sym=%u rd=%u num_vregs=%u instance=%p v_regs=%p ret_addr=%p\n", 
           (unsigned)symbol_idx, (unsigned)rd, (unsigned)num_virtual_regs,
           (void*)instance, (void*)v_regs, ret_addr);
#endif
    if (!instance || !instance->module || !v_regs) return;
    if (rd >= num_virtual_regs) return;

    EspbModule* module = (EspbModule*)instance->module;
    uintptr_t addr = 0;

    // Function pointer flag: high bit set
    if (symbol_idx & 0x8000) {
        uint32_t func_idx = (uint32_t)(symbol_idx & 0x7FFF);
        if (module->func_ptr_map_by_index && func_idx < module->func_ptr_map_by_index_size && instance->memory_data) {
            uint32_t data_offset = module->func_ptr_map_by_index[func_idx];
            if (data_offset != UINT32_MAX) {
                addr = (uintptr_t)(instance->memory_data + data_offset);
            } else {
                return;
            }
        } else {
            return;
        }
    } else {
        if (symbol_idx >= module->num_globals) return;
        const EspbGlobalDesc* g = &module->globals[symbol_idx];
        if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
            if (!instance->memory_data) return;
            addr = (uintptr_t)(instance->memory_data + g->initializer.data_section_offset);
        } else if (g->init_kind == ESPB_INIT_KIND_CONST || g->init_kind == ESPB_INIT_KIND_ZERO) {
            if (!instance->globals_data || !instance->global_offsets) return;
            addr = (uintptr_t)(instance->globals_data + instance->global_offsets[symbol_idx]);
        } else {
            return;
        }
    }

#if ESPB_JIT_DEBUG
    if (addr != 0) {
        const char *s = (const char *)addr;
        printf("[jit] LD_GLOBAL_ADDR: sym=%u addr=%p str=%.40s\n",
               (unsigned)symbol_idx, (void*)addr, s);
    }
#endif

    memset(&v_regs[rd], 0, sizeof(Value));
    SET_TYPE(v_regs[rd], ESPB_TYPE_PTR);
    V_PTR(v_regs[rd]) = (void*)addr;
}

void espb_jit_ld_global(EspbInstance* instance, uint16_t global_idx, Value* v_regs, uint16_t num_virtual_regs, uint8_t rd) {
    if (!instance || !instance->module || !v_regs) return;
    if (rd >= num_virtual_regs) return;

    EspbModule* module = (EspbModule*)instance->module;
    if (global_idx >= module->num_globals) return;

    const EspbGlobalDesc* g = &module->globals[global_idx];
    uint8_t size = espb_value_size(g->type);

    uint8_t* base = NULL;
    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
        if (!instance->memory_data) return;
        base = instance->memory_data + g->initializer.data_section_offset;
        if (g->type == ESPB_TYPE_PTR) {
            // Return address itself
            uintptr_t addr = (uintptr_t)base;
            memset(&v_regs[rd], 0, sizeof(Value));
            SET_TYPE(v_regs[rd], ESPB_TYPE_PTR);
            V_PTR(v_regs[rd]) = (void*)addr;
            return;
        }
    } else {
        if (!instance->globals_data || !instance->global_offsets) return;
        base = instance->globals_data + instance->global_offsets[global_idx];
    }

    memset(&v_regs[rd], 0, sizeof(Value));
    memcpy(&V_RAW(v_regs[rd]), base, size);
    SET_TYPE(v_regs[rd], g->type);
}

void espb_jit_st_global(EspbInstance* instance, uint16_t global_idx, const Value* v_regs, uint16_t num_virtual_regs, uint8_t rs) {
    if (!instance || !instance->module || !v_regs) return;
    if (rs >= num_virtual_regs) return;

    EspbModule* module = (EspbModule*)instance->module;
    if (global_idx >= module->num_globals) return;

    const EspbGlobalDesc* g = &module->globals[global_idx];
    if (!g->mutability) return;

    uint8_t* target_addr = NULL;
    if (g->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
        if (!instance->memory_data) return;
        target_addr = instance->memory_data + g->initializer.data_section_offset;
    } else {
        if (!instance->globals_data || !instance->global_offsets) return;
        target_addr = instance->globals_data + instance->global_offsets[global_idx];
    }

    uint8_t size = espb_value_size(g->type);
    memcpy(target_addr, &V_RAW(v_regs[rs]), size);
}
