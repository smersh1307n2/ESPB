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
// Xtensa JIT with inline code generation (no ops-trampoline)

// Verbose per-opcode diagnostics for Xtensa inline JIT.
// Must be disabled by default because it pollutes application output/tests.
#ifndef ESPB_JIT_DEBUG_OPTOCODES
#define ESPB_JIT_DEBUG_OPTOCODES 0
#endif

// General Xtensa JIT debug logging (for verbose build-time logs).
#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

#if ESPB_JIT_DEBUG
#define JIT_LOGI ESP_LOGI
#define JIT_LOGW ESP_LOGW
#define JIT_LOGD ESP_LOGD
#else
#define JIT_LOGI(tag, fmt, ...) ((void)0)
#define JIT_LOGW(tag, fmt, ...) ((void)0)
#define JIT_LOGD(tag, fmt, ...) ((void)0)
#endif

// Print which ESPB opcodes are used by the function ("[opc-prof]" histogram).
// Toggle locally while implementing missing opcodes.
#ifndef ESPB_JIT_DUMP_USED_OPCODES
#define ESPB_JIT_DUMP_USED_OPCODES 0
#endif

// Based on RISC-V JIT architecture

#include "espb_jit.h"
#include "espb_interpreter_common_types.h"
#include "espb_jit_dispatcher.h"
#include "espb_jit_import_call.h"
#include "espb_jit_globals.h"
#include "espb_jit_helpers.h"
#include "espb_jit_indirect_ptr.h"
#include "espb_runtime_alloca.h"
#include "espb_heap_manager.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "espb_exec_memory.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char* TAG = "espb_jit_xtensa_inline";

// ===== Debug helpers (called from JIT code) =====
// ===== C helpers for complex ops (called from JIT code) =====
// Keep these helpers in C to avoid relying on uncalibrated Xtensa encodings for address arithmetic
// and unaligned 64-bit stores.
__attribute__((noinline))
static void espb_jit_xtensa_store_i64(Value* v_regs, uint8_t rs, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x76 (STORE.I64):
    //   *( (uint8_t*)V_PTR(v_regs[ra]) + offset ) = V_I64(v_regs[rs])
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    int64_t v = V_I64(v_regs[rs]);
    memcpy((uint8_t*)base + (intptr_t)offset, &v, sizeof(v));
}

__attribute__((noinline))
static void espb_jit_xtensa_store_i32(Value* v_regs, uint8_t rs, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x74 (STORE.I32):
    //   *( (uint8_t*)V_PTR(v_regs[ra]) + offset ) = V_I32(v_regs[rs])
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    int32_t v = V_I32(v_regs[rs]);
    memcpy((uint8_t*)base + (intptr_t)offset, &v, sizeof(v));
}

__attribute__((noinline))
static void espb_jit_xtensa_store_i16(Value* v_regs, uint8_t rs, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x72/0x73 (STORE.I16/U16):
    //   *( (uint8_t*)V_PTR(v_regs[ra]) + offset ) = (uint16_t)V_I32(v_regs[rs])
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint16_t v = (uint16_t)V_I32(v_regs[rs]);
    memcpy((uint8_t*)base + (intptr_t)offset, &v, sizeof(v));
}

__attribute__((noinline))
static void espb_jit_xtensa_store_i8(Value* v_regs, uint8_t rs, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x70/0x71 (STORE.I8/U8):
    //   *( (uint8_t*)V_PTR(v_regs[ra]) + offset ) = (uint8_t)V_I32(v_regs[rs])
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint8_t v = (uint8_t)V_I32(v_regs[rs]);
    memcpy((uint8_t*)base + (intptr_t)offset, &v, sizeof(v));
}

__attribute__((noinline))
static void espb_jit_xtensa_store_bool(Value* v_regs, uint8_t rs, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x7B (STORE.BOOL):
    //   *( (uint8_t*)V_PTR(v_regs[ra]) + offset ) = (V_I32(v_regs[rs]) != 0)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint8_t v = (V_I32(v_regs[rs]) != 0) ? 1u : 0u;
    memcpy((uint8_t*)base + (intptr_t)offset, &v, sizeof(v));
}

__attribute__((noinline))
static void espb_jit_xtensa_load_i32(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x84 (LOAD.I32):
    //   v_regs[rd] = *(int32_t*)(V_PTR(v_regs[ra]) + offset)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    int32_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = v;
}

__attribute__((noinline))
static void espb_jit_xtensa_load_i64(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x85 (LOAD.I64):
    //   v_regs[rd] = *(int64_t*)(V_PTR(v_regs[ra]) + offset)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    int64_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I64);
    V_I64(v_regs[rd]) = v;
}

// Helper: free pointer directly (ptr passed from JIT)
// Wrapper needed to add NULL checks before calling espb_heap_free
__attribute__((noinline))
static void jit_helper_heap_free(EspbInstance* instance, void* ptr) {
    if (!instance || !ptr) return;
    espb_heap_free(instance, ptr);
}

static void espb_jit_xtensa_load_i8_s(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x80 (LOAD.I8S)
    // v_regs[ra] contains a POINTER (not offset), so we use it directly
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address (RAM/ROM range)
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) {
        // Invalid pointer - possibly data value used as pointer, skip load
        return;
    }
    int8_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = (int32_t)v;
}

__attribute__((noinline))
static void espb_jit_xtensa_load_i8_u(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x81 (LOAD.I8U)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint8_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = (uint32_t)v;
}

__attribute__((noinline))
static void espb_jit_xtensa_load_i16_s(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x82 (LOAD.I16S)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address (RAM/ROM range)
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) {
        return;
    }
    int16_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = (int32_t)v;
}

__attribute__((noinline))
static void espb_jit_xtensa_load_i16_u(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x83 (LOAD.I16U)
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint16_t v;
    memcpy(&v, (uint8_t*)base + (intptr_t)offset, sizeof(v));
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = (uint32_t)v;
}

__attribute__((noinline))
static void espb_jit_xtensa_load_bool(Value* v_regs, uint8_t rd, uint8_t ra, int32_t offset) {
    // Semantics match interpreter op_0x89 (LOAD.BOOL)
    // Loads a byte and normalizes to 0 or 1
    if (!v_regs) return;
    void* base = V_PTR(v_regs[ra]);
    if (!base) return;
    // Safety check: validate pointer looks like a valid ESP32 address
    if ((uintptr_t)base < 0x3F000000 || (uintptr_t)base > 0x60100000) return;
    uint8_t raw_val;
    memcpy(&raw_val, (uint8_t*)base + (intptr_t)offset, sizeof(raw_val));
    // Normalize: any non-zero value becomes 1
    int32_t bool_val = (raw_val != 0) ? 1 : 0;
    SET_TYPE(v_regs[rd], ESPB_TYPE_BOOL);
    V_I32(v_regs[rd]) = bool_val;
}

__attribute__((noinline))
static void espb_jit_xtensa_sext_i8_i32(Value* v_regs, uint8_t rd, uint8_t rs) {
    if (!v_regs) return;
    SET_TYPE(v_regs[rd], ESPB_TYPE_I32);
    V_I32(v_regs[rd]) = (int8_t)V_I32(v_regs[rs]);
}

// ===== Atomic operation helpers for Xtensa JIT =====
// Use wrapper functions to ensure correct ABI for JIT calls

// I32 Atomics - thin wrappers for correct ABI
__attribute__((noinline))
static uint32_t jit_xtensa_atomic_fetch_add_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_add((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_fetch_sub_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_sub((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_fetch_and_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_and((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_fetch_or_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_or((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_fetch_xor_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_xor((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_exchange_4(volatile void* ptr, uint32_t val) {
    return __atomic_exchange_n((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static bool jit_xtensa_atomic_compare_exchange_4(volatile void* ptr, uint32_t* expected, uint32_t desired) {
    return __atomic_compare_exchange_n((volatile uint32_t*)ptr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint32_t jit_xtensa_atomic_load_4(volatile void* ptr) {
    return __atomic_load_n((volatile uint32_t*)ptr, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static void jit_xtensa_atomic_store_4(volatile void* ptr, uint32_t val) {
    __atomic_store_n((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

// I64 Atomics - use wrappers to ensure correct ABI
__attribute__((noinline))
static uint64_t jit_xtensa_atomic_fetch_add_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_add((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_fetch_sub_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_sub((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_fetch_and_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_and((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_fetch_or_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_or((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_fetch_xor_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_xor((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_exchange_8(volatile void* ptr, uint64_t val) {
    return __atomic_exchange_n((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static bool jit_xtensa_atomic_compare_exchange_8(volatile void* ptr, uint64_t* expected, uint64_t desired) {
    return __atomic_compare_exchange_n((volatile uint64_t*)ptr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static uint64_t jit_xtensa_atomic_load_8(volatile void* ptr) {
    return __atomic_load_n((volatile uint64_t*)ptr, __ATOMIC_SEQ_CST);
}

__attribute__((noinline))
static void jit_xtensa_atomic_store_8(volatile void* ptr, uint64_t val) {
    __atomic_store_n((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

// Atomic fence
__attribute__((noinline))
static void jit_helper_atomic_fence(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// ===== Extended opcode helpers (shared with RISC-V semantics) =====
__attribute__((noinline))
static EspbResult jit_helper_memory_init(EspbInstance *instance, uint32_t data_seg_idx,
                                         uint32_t dest_addr, uint32_t src_offset, uint32_t size) {
    if (!instance || !instance->module) return ESPB_ERR_INVALID_STATE;

    const EspbModule *module = instance->module;
    if (data_seg_idx >= module->num_data_segments) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    const EspbDataSegment *segment = &module->data_segments[data_seg_idx];

    if ((uint64_t)dest_addr + size > instance->memory_size_bytes ||
        (uint64_t)src_offset + size > segment->data_size) {
        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
    }

    memcpy(instance->memory_data + dest_addr, segment->data + src_offset, size);
    return ESPB_OK;
}

__attribute__((noinline))
static EspbResult jit_helper_data_drop(EspbInstance *instance, uint32_t data_seg_idx) {
    if (!instance || !instance->module) return ESPB_ERR_INVALID_STATE;

    const EspbModule *module = instance->module;
    if (data_seg_idx >= module->num_data_segments) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    EspbDataSegment *segment = (EspbDataSegment*)&module->data_segments[data_seg_idx];
    segment->data_size = 0;
    return ESPB_OK;
}

__attribute__((noinline))
static uint32_t jit_helper_table_size(EspbInstance* instance) {
    if (!instance) return 0;
    return instance->table_size;
}

__attribute__((noinline))
static uint32_t jit_helper_table_get(EspbInstance* instance, uint32_t table_idx, uint32_t index) {
    (void)table_idx;
    if (!instance || !instance->table_data || index >= instance->table_size) return 0;
    return (uint32_t)(uintptr_t)instance->table_data[index];
}

__attribute__((noinline))
static void jit_helper_table_init(EspbInstance* instance, uint32_t table_idx, uint32_t elem_seg_idx,
                                  uint32_t dst_index, uint32_t src_offset, uint32_t count) {
    if (!instance || !instance->module) return;
    const EspbModule* module = instance->module;

    if (table_idx >= module->num_tables) return;
    if (elem_seg_idx >= module->num_element_segments) return;

    EspbElementSegment* segment = &module->element_segments[elem_seg_idx];
    if ((uint64_t)src_offset + (uint64_t)count > (uint64_t)segment->num_elements) return;

    uint32_t required_size = dst_index + count;
    if (required_size > instance->table_size) {
        if (required_size > instance->table_max_size) return;
        void** new_table = (void**)realloc(instance->table_data, required_size * sizeof(void*));
        if (!new_table) return;
        for (uint32_t i = instance->table_size; i < required_size; ++i) {
            new_table[i] = NULL;
        }
        instance->table_data = new_table;
        instance->table_size = required_size;
    }

    for (uint32_t i = 0; i < count; ++i) {
        instance->table_data[dst_index + i] = (void*)(uintptr_t)segment->function_indices[src_offset + i];
    }
}

__attribute__((noinline))
static void jit_helper_table_copy(EspbInstance* instance, uint32_t dst_table_idx, uint32_t src_table_idx,
                                  uint32_t dst_offset, uint32_t src_offset, uint32_t count) {
    (void)dst_table_idx;
    (void)src_table_idx;

    if (!instance || !instance->table_data || count == 0) return;

    uint32_t dst_required = dst_offset + count;
    uint32_t src_required = src_offset + count;
    uint32_t required_size = (dst_required > src_required) ? dst_required : src_required;

    if (required_size > instance->table_size) {
        if (required_size > instance->table_max_size) return;
        void** new_table = (void**)realloc(instance->table_data, required_size * sizeof(void*));
        if (!new_table) return;
        for (uint32_t i = instance->table_size; i < required_size; i++) {
            new_table[i] = NULL;
        }
        instance->table_data = new_table;
        instance->table_size = required_size;
    }

    if (dst_offset <= src_offset) {
        for (uint32_t i = 0; i < count; i++) {
            if (src_offset + i < instance->table_size && dst_offset + i < instance->table_size) {
                instance->table_data[dst_offset + i] = instance->table_data[src_offset + i];
            }
        }
    } else {
        for (uint32_t i = count; i > 0; i--) {
            if (src_offset + i - 1 < instance->table_size && dst_offset + i - 1 < instance->table_size) {
                instance->table_data[dst_offset + i - 1] = instance->table_data[src_offset + i - 1];
            }
        }
    }
}

__attribute__((noinline))
static void jit_helper_table_fill(EspbInstance* instance, uint32_t table_idx, uint32_t start_index,
                                  uint32_t fill_value, uint32_t count) {
    (void)table_idx;
    if (!instance || !instance->table_data) return;

    uint32_t required_size = start_index + count;
    if (required_size > instance->table_size) {
        if (required_size > instance->table_max_size) return;
        void** new_table = (void**)realloc(instance->table_data, required_size * sizeof(void*));
        if (!new_table) return;
        for (uint32_t i = instance->table_size; i < required_size; i++) {
            new_table[i] = NULL;
        }
        instance->table_data = new_table;
        instance->table_size = required_size;
    }

    void* value = (void*)(uintptr_t)fill_value;
    for (uint32_t i = 0; i < count; i++) {
        if (start_index + i < instance->table_size) {
            instance->table_data[start_index + i] = value;
        }
    }
}

__attribute__((noinline))
static void jit_helper_table_set(EspbInstance* instance, uint32_t table_idx, uint32_t index, uint32_t value) {
    (void)table_idx;

    if (!instance || !instance->table_data) return;
    if (index >= instance->table_size) {
        uint32_t required_size = index + 1;
        if (required_size > instance->table_max_size) return;
        void** new_table = (void**)realloc(instance->table_data, required_size * sizeof(void*));
        if (!new_table) return;
        for (uint32_t i = instance->table_size; i < required_size; i++) {
            new_table[i] = NULL;
        }
        instance->table_data = new_table;
        instance->table_size = required_size;
    }
    instance->table_data[index] = (void*)(uintptr_t)value;
}

// Helper for I32 comparisons (0xC1-0xC9, 0xC0 is inlined)
__attribute__((noinline))
static void espb_jit_xtensa_cmp_i32(Value* v_regs, uint8_t opcode, uint8_t rd, uint8_t r1, uint8_t r2) {
    if (!v_regs) return;
    int32_t val1 = V_I32(v_regs[r1]);
    int32_t val2 = V_I32(v_regs[r2]);
    bool cmp_res = false;
    switch(opcode) {
        case 0xC0: cmp_res = (val1 == val2); break;
        case 0xC1: cmp_res = (val1 != val2); break;
        case 0xC2: cmp_res = (val1 < val2); break;   // LT signed
        case 0xC3: cmp_res = (val1 > val2); break;   // GT signed
        case 0xC4: cmp_res = (val1 <= val2); break;  // LE signed
        case 0xC5: cmp_res = (val1 >= val2); break;  // GE signed
        case 0xC6: cmp_res = ((uint32_t)val1 < (uint32_t)val2); break;   // LT unsigned
        case 0xC7: cmp_res = ((uint32_t)val1 > (uint32_t)val2); break;   // GT unsigned
        case 0xC8: cmp_res = ((uint32_t)val1 <= (uint32_t)val2); break;  // LE unsigned
        case 0xC9: cmp_res = ((uint32_t)val1 >= (uint32_t)val2); break;  // GE unsigned
    }
    SET_TYPE(v_regs[rd], ESPB_TYPE_BOOL);
    V_I32(v_regs[rd]) = cmp_res ? 1 : 0;
}

// Helper for I64 comparisons (0xCA-0xD3)
__attribute__((noinline))
static void espb_jit_xtensa_cmp_i64(Value* v_regs, uint8_t opcode, uint8_t rd, uint8_t r1, uint8_t r2) {
    if (!v_regs) return;
    int64_t val1 = V_I64(v_regs[r1]);
    int64_t val2 = V_I64(v_regs[r2]);
    bool cmp_res = false;
    switch(opcode) {
        case 0xCA: cmp_res = (val1 == val2); break;
        case 0xCB: cmp_res = (val1 != val2); break;
        case 0xCC: cmp_res = (val1 < val2); break;
        case 0xCD: cmp_res = (val1 > val2); break;
        case 0xCE: cmp_res = (val1 <= val2); break;
        case 0xCF: cmp_res = (val1 >= val2); break;
        case 0xD0: cmp_res = ((uint64_t)val1 < (uint64_t)val2); break;
        case 0xD1: cmp_res = ((uint64_t)val1 > (uint64_t)val2); break;
        case 0xD2: cmp_res = ((uint64_t)val1 <= (uint64_t)val2); break;
        case 0xD3: cmp_res = ((uint64_t)val1 >= (uint64_t)val2); break;
    }
    SET_TYPE(v_regs[rd], ESPB_TYPE_BOOL);
    V_I32(v_regs[rd]) = cmp_res ? 1 : 0;
}

// ===== F32/F64 comparison helpers (operate on raw bits) =====
__attribute__((noinline))
static uint32_t jit_helper_cmp_eq_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a == b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ne_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a != b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_lt_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a < b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gt_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a > b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_le_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a <= b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ge_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a >= b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_eq_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a == b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ne_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a != b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_lt_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a < b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gt_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a > b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_le_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a <= b) ? 1u : 0u;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ge_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a >= b) ? 1u : 0u;
}

// Helper for SELECT operations (0xBE, 0xBF, 0xD4, 0xD5, 0xD6)
// Rd = Rcond ? Rtrue : Rfalse
__attribute__((noinline))
static void espb_jit_xtensa_select(Value* v_regs, uint8_t rd, uint8_t r_cond, uint8_t r_true, uint8_t r_false) {
    if (!v_regs) return;
    bool condition = (V_I32(v_regs[r_cond]) != 0);
    v_regs[rd] = condition ? v_regs[r_true] : v_regs[r_false];
}

// Helper for SUB.I64.IMM8 (0x51)
// Using helper avoids subtle Xtensa windowed-ABI issues with 64-bit args/returns.
__attribute__((noinline))
static void espb_jit_xtensa_sub_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, int8_t imm8) {
    if (!v_regs) return;
    int64_t val = V_I64(v_regs[r1]);
    int64_t subtrahend = (int64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_I64);
    V_I64(v_regs[rd]) = val - subtrahend;
}

// Helper for MUL.I64.IMM8 (0x52)
// Using helper avoids subtle Xtensa windowed-ABI issues with 64-bit args/returns.
__attribute__((noinline))
static void espb_jit_xtensa_mul_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, int8_t imm8) {
    if (!v_regs) return;
    int64_t val = V_I64(v_regs[r1]);
    int64_t multiplier = (int64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_I64);
    V_I64(v_regs[rd]) = val * multiplier;
}

// Helpers for I64 IMM8 div/rem ops (0x53-0x56)
// Using helpers avoids subtle Xtensa windowed-ABI issues with 64-bit args/returns.
__attribute__((noinline))
static void espb_jit_xtensa_divs_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, int8_t imm8) {
    if (!v_regs) return;
    int64_t dividend = V_I64(v_regs[r1]);
    int64_t divisor = (int64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_I64);
    V_I64(v_regs[rd]) = (divisor == 0) ? 0 : (dividend / divisor);
}

__attribute__((noinline))
static void espb_jit_xtensa_divu_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, uint8_t imm8) {
    if (!v_regs) return;
    uint64_t dividend = (uint64_t)V_I64(v_regs[r1]);
    uint64_t divisor = (uint64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_U64);
    V_I64(v_regs[rd]) = (divisor == 0) ? 0 : (int64_t)(dividend / divisor);
}

__attribute__((noinline))
static void espb_jit_xtensa_rems_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, int8_t imm8) {
    if (!v_regs) return;
    int64_t dividend = V_I64(v_regs[r1]);
    int64_t divisor = (int64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_I64);
    V_I64(v_regs[rd]) = (divisor == 0) ? 0 : (dividend % divisor);
}

__attribute__((noinline))
static void espb_jit_xtensa_remu_i64_imm8(Value* v_regs, uint8_t rd, uint8_t r1, uint8_t imm8) {
    if (!v_regs) return;
    uint64_t dividend = (uint64_t)V_I64(v_regs[r1]);
    uint64_t divisor = (uint64_t)imm8;
    SET_TYPE(v_regs[rd], ESPB_TYPE_U64);
    V_I64(v_regs[rd]) = (divisor == 0) ? 0 : (int64_t)(dividend % divisor);
}

// Forward declaration for ExecutionContext functions
extern ExecutionContext* init_execution_context(void);
extern EspbResult espb_execute_function_jit_only(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);
extern EspbResult espb_execute_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);

// Helper for CALL opcode (0x0A) - call local function
// Signature: void jit_call_espb_function_xtensa(EspbInstance* instance, uint32_t local_func_idx, Value* v_regs)
__attribute__((noinline))
static void jit_call_espb_function_xtensa(EspbInstance* instance, uint32_t local_func_idx, Value* v_regs) {
    if (!instance || !v_regs) {
        return;
    }
    
    static __thread ExecutionContext* temp_exec_ctx = NULL;
    if (!temp_exec_ctx) {
        temp_exec_ctx = init_execution_context();
        if (!temp_exec_ctx) {
            ESP_LOGE(TAG, "Failed to create ExecutionContext for CALL");
            return;
        }
    }
    
    const EspbModule* module = instance->module;
    if (local_func_idx >= module->num_functions) return;
    
    uint32_t num_imported_funcs = module->num_imported_funcs;
    uint32_t global_func_idx = num_imported_funcs + local_func_idx;
    
    uint32_t sig_idx = module->function_signature_indices[local_func_idx];
    EspbFunctionBody* callee_body = &module->function_bodies[local_func_idx];
    const EspbFuncSignature* sig = &module->signatures[sig_idx];
    uint8_t num_args = sig->num_params;
    
    // FAST PATH: if HOT function is JIT-compiled, call directly
    if (callee_body->is_jit_compiled && callee_body->jit_code != NULL) {
        // ESP_LOGI(TAG, "[CALL] FAST PATH: func_idx=%u is JIT-compiled", (unsigned)local_func_idx);
        typedef void (*JitFunc)(EspbInstance*, Value*);
        JitFunc jit_func = (JitFunc)callee_body->jit_code;
        
        uint16_t needed_regs = callee_body->header.num_virtual_regs;
        if (needed_regs == 0 || needed_regs > 256) needed_regs = 256;
        
        Value *callee_regs = (Value*)alloca((size_t)needed_regs * sizeof(Value));
        if (!callee_regs) goto slow_path;
        
        uint16_t max_used = (uint16_t)callee_body->header.max_reg_used + 1;
        uint16_t zero_regs = needed_regs;
        if (max_used > 0 && max_used < zero_regs) zero_regs = max_used;
        if (zero_regs < num_args) zero_regs = num_args;
        if (zero_regs == 0) zero_regs = 1;
        
        // For hot calls (like fibonacci_iterative): the first num_args registers are overwritten
        // by the arg copy below, so we only need to clear the tail.
        if (zero_regs > num_args) {
            memset(callee_regs + num_args, 0, (size_t)(zero_regs - num_args) * sizeof(Value));
        }

        // Copy arguments from caller's v_regs to callee_regs
        for (uint8_t i = 0; i < num_args; i++) {
            callee_regs[i] = v_regs[i];
        }
        
        jit_func(instance, callee_regs);
        
        // Copy return value back
        if (sig->num_returns > 0) {
            // ESP_LOGI(TAG, "[CALL] FAST callee_regs[0].i64=%lld -> v_regs[0]", (long long)V_I64(callee_regs[0]));
            v_regs[0] = callee_regs[0];
        }
        return;
    }
    
slow_path:
    // SLOW PATH: function not JIT-compiled, use dispatcher (respects HOT flag)
    ;
    Value args[8];
    for (uint8_t i = 0; i < num_args && i < 8; i++) {
        args[i] = v_regs[i];
    }
    
    Value result;
    // Use espb_execute_function which respects HOT flag - non-HOT functions 
    // will be executed via interpreter, HOT functions will be JIT-compiled
    EspbResult call_res = espb_execute_function(instance, temp_exec_ctx, global_func_idx, args, &result);
    if (call_res != ESPB_OK) {
        memset(&result, 0, sizeof(result));
    }
    
    if (sig->num_returns > 0) {
        v_regs[0] = result;
    }
}

// ===== I64 arithmetic helpers (called from JIT code) =====
__attribute__((noinline))
static uint64_t jit_helper_divu64(uint64_t a, uint64_t b) {
    if (b == 0) return 0;  // Division by zero returns 0
    return a / b;
}

__attribute__((noinline))
static int64_t jit_helper_divs64(int64_t a, int64_t b) {
    if (b == 0) return 0;  // Division by zero returns 0
    if (a == INT64_MIN && b == -1) return INT64_MIN;  // Overflow case
    return a / b;
}

__attribute__((noinline))
static uint64_t jit_helper_remu64(uint64_t a, uint64_t b) {
    if (b == 0) return 0;  // Modulo by zero returns 0
    return a % b;
}

__attribute__((noinline))
static int64_t jit_helper_rems64(int64_t a, int64_t b) {
    if (b == 0) return 0;  // Modulo by zero returns 0
    if (a == INT64_MIN && b == -1) return 0;  // Overflow case
    return a % b;
}

__attribute__((noinline))
static uint64_t jit_helper_mulu64(uint64_t a, uint64_t b) {
    return a * b;
}

__attribute__((noinline))
static uint64_t jit_helper_addu64(uint64_t a, uint64_t b) {
    return a + b;
}

// NOTE: SUB.I64 (0x31) is now generated inline in the Xtensa JIT fast-path.
// Keep this helper for reference/fallback during bring-up, but it is not used by default.
__attribute__((noinline, unused))
static uint64_t jit_helper_subu64(uint64_t a, uint64_t b) {
    return a - b;
}

// ===== I32 division and remainder helpers (called from JIT code) =====
__attribute__((noinline))
static uint32_t jit_helper_divs32(uint32_t a, uint32_t b) {
    // Signed division
    if (b == 0) return 0;  // Match interpreter behavior on div-by-zero
    return (uint32_t)((int32_t)a / (int32_t)b);
}

__attribute__((noinline))
static uint32_t jit_helper_divu32(uint32_t a, uint32_t b) {
    // Unsigned division
    if (b == 0) return 0;  // Match interpreter behavior on div-by-zero
    return a / b;
}

__attribute__((noinline))
static uint32_t jit_helper_rems32(uint32_t a, uint32_t b) {
    // Signed remainder
    if (b == 0) return 0;  // Match interpreter behavior on div-by-zero
    return (uint32_t)((int32_t)a % (int32_t)b);
}

__attribute__((noinline))
static uint32_t jit_helper_remu32(uint32_t a, uint32_t b) {
    // Unsigned remainder
    if (b == 0) return 0;  // Match interpreter behavior on div-by-zero
    return a % b;
}

// ===== I64 shift helpers (called from JIT code) =====
// NOTE: Second argument is uint64_t to match the ABI of other i64 helpers,
// but only the low 6 bits are used for the shift amount.
__attribute__((noinline))
static int64_t jit_helper_shr64(int64_t a, uint64_t shift) {
    // Arithmetic shift right (sign-extending)
    return a >> (shift & 63);
}

__attribute__((noinline))
static uint64_t jit_helper_ushr64(uint64_t a, uint64_t shift) {
    // Logical shift right (zero-extending)
    return a >> (shift & 63);
}

__attribute__((noinline))
static int64_t jit_helper_shl64(int64_t a, uint64_t shift) {
    // Shift left. Do it via uint64_t to avoid UB on signed left shift.
    uint64_t u = (uint64_t)a;
    u <<= (shift & 63);
    return (int64_t)u;
}

// ===== F32<->F64 helpers (bits-preserving ABI) =====
__attribute__((noinline))
static uint64_t jit_helper_fpromote_f32_to_f64_bits(uint32_t f32_bits) {
    float f;
    memcpy(&f, &f32_bits, sizeof(f));
    double d = (double)f;
    uint64_t out;
    memcpy(&out, &d, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fpround_f64_to_f32_bits(uint64_t f64_bits) {
    double d;
    memcpy(&d, &f64_bits, sizeof(d));
    float f = (float)d;
    uint32_t out;
    memcpy(&out, &f, sizeof(out));
    return out;
}

// ===== F32 arithmetic helpers (operate on raw IEEE754 bits) =====
__attribute__((noinline))
static uint32_t jit_helper_fadd_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = a + b;
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fsub_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = a - b;
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fmul_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = a * b;
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fdiv_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = a / b;
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fmin_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = fminf(a, b);
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fmax_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b, r;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    r = fmaxf(a, b);
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fabs_f32_bits(uint32_t a_bits) {
    float a, r;
    memcpy(&a, &a_bits, sizeof(a));
    r = fabsf(a);
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

__attribute__((noinline))
static uint32_t jit_helper_fsqrt_f32_bits(uint32_t a_bits) {
    float a, r;
    memcpy(&a, &a_bits, sizeof(a));
    r = sqrtf(a);
    uint32_t out;
    memcpy(&out, &r, sizeof(out));
    return out;
}

// ===== CVT helpers (conversion between int and float types) =====
__attribute__((noinline))
static uint64_t jit_helper_cvt_u32_f64_bits(uint32_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

__attribute__((noinline))
static uint64_t jit_helper_cvt_u64_f64_bits(uint64_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

__attribute__((noinline))
static uint64_t jit_helper_cvt_i64_f64_bits(uint64_t val) {
    double d = (double)(int64_t)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

__attribute__((noinline))
static uint64_t jit_helper_cvt_i32_f64_bits(int32_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

__attribute__((noinline))
static uint64_t jit_helper_cvt_f64_u64(uint64_t f64_bits) {
    double d;
    memcpy(&d, &f64_bits, sizeof(d));
    if (d < 0.0 || d != d) return 0;  // Negative or NaN
    if (d >= (double)UINT64_MAX) return UINT64_MAX;
    return (uint64_t)d;
}

__attribute__((noinline))
static uint32_t jit_helper_cvt_f64_u32(uint64_t f64_bits) {
    double d;
    memcpy(&d, &f64_bits, sizeof(d));
    if (d < 0.0 || d != d) return 0u;
    if (d >= (double)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)d;
}

__attribute__((noinline))
static int64_t jit_helper_cvt_f64_i64(uint64_t f64_bits) {
    double d;
    memcpy(&d, &f64_bits, sizeof(d));
    if (d != d) return 0;  // NaN
    if (d >= (double)INT64_MAX) return INT64_MAX;
    if (d <= (double)INT64_MIN) return INT64_MIN;
    return (int64_t)d;
}

// U32 -> F32: returns float bits as uint32_t
__attribute__((noinline))
static uint32_t jit_helper_cvt_u32_f32_bits(uint32_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// I32 -> F32: returns float bits as uint32_t
__attribute__((noinline))
static uint32_t jit_helper_cvt_i32_f32_bits(int32_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// I64 -> F32: returns float bits as uint32_t
__attribute__((noinline))
static uint32_t jit_helper_cvt_i64_f32_bits(int64_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// U64 -> F32: returns float bits as uint32_t
__attribute__((noinline))
static uint32_t jit_helper_cvt_u64_f32_bits(uint64_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// F32 (raw bits) -> U32
__attribute__((noinline))
static uint32_t jit_helper_cvt_f32_u32_bits(uint32_t f32_bits) {
    float f;
    memcpy(&f, &f32_bits, sizeof(f));
    if (f < 0.0f || f != f) return 0u;
    if (f >= (float)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)f;
}

// F32 (raw bits) -> U64
__attribute__((noinline))
static uint64_t jit_helper_cvt_f32_u64_bits(uint32_t f32_bits) {
    float f;
    memcpy(&f, &f32_bits, sizeof(f));
    if (f < 0.0f || f != f) return 0u;
    if (f >= (float)UINT64_MAX) return UINT64_MAX;
    return (uint64_t)f;
}

// F32 (raw bits) -> I32
__attribute__((noinline))
static int32_t jit_helper_cvt_f32_i32_bits(uint32_t f32_bits) {
    float f;
    memcpy(&f, &f32_bits, sizeof(f));
    if (f != f) return 0;
    if (f >= (float)INT32_MAX) return INT32_MAX;
    if (f <= (float)INT32_MIN) return INT32_MIN;
    return (int32_t)f;
}

// F32 (raw bits) -> I64
__attribute__((noinline))
static int64_t jit_helper_cvt_f32_i64_bits(uint32_t f32_bits) {
    float f;
    memcpy(&f, &f32_bits, sizeof(f));
    if (f != f) return 0;
    if (f >= (float)INT64_MAX) return INT64_MAX;
    if (f <= (float)INT64_MIN) return INT64_MIN;
    return (int64_t)f;
}

// ===== F64 arithmetic helpers (operate on raw IEEE-754 bits) =====
// We pass/return u64 in integer registers to avoid hard-float ABI issues on Xtensa.
__attribute__((noinline))
static uint64_t jit_helper_fadd_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = a + b;
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fsub_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = a - b;
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fmul_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = a * b;
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fdiv_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = a / b;
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fabs_f64_bits(uint64_t a_bits) {
    // Use fabs() to force proper ABI handling like fsqrt
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    double r = fabs(a);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
    return r_bits;
}

// Helper that does fabs AND stores result directly to v_regs
__attribute__((noinline))
static void jit_helper_fabs_f64_store(uint64_t* v_regs, uint8_t rd, uint8_t rs) {
    uint64_t a_bits = v_regs[rs];
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    double r = fabs(a);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
#if ESPB_JIT_DEBUG_OPTOCODES
    ESP_LOGI(TAG, "[ABS.F64] v_regs=%p rd=%d rs=%d in=%.6f out=%.6f", v_regs, rd, rs, a, r);
#endif
    v_regs[rd] = r_bits;
#if ESPB_JIT_DEBUG_OPTOCODES
    ESP_LOGI(TAG, "[ABS.F64] v_regs[%d]=0x%08X%08X (%.6f)", rd, (unsigned)(v_regs[rd]>>32), (unsigned)v_regs[rd], r);
#endif
}

// Debug helper to check v_regs[rd] value - called from JIT after restore
__attribute__((noinline))
static void jit_debug_check_vreg(uint64_t* v_regs, uint8_t rd) {
    uint64_t val = v_regs[rd];
    double d;
    memcpy(&d, &val, sizeof(d));
#if ESPB_JIT_DEBUG_OPTOCODES
    ESP_LOGI(TAG, "[DEBUG] AFTER_RESTORE v_regs=%p v_regs[%d]=0x%08X%08X (%.6f)", 
             v_regs, rd, (unsigned)(val>>32), (unsigned)val, d);
#else
    (void)v_regs;
    (void)rd;
    (void)val;
    (void)d;
#endif
}

// Helper that does fmin AND stores result directly to v_regs
__attribute__((noinline))
static void jit_helper_fmin_f64_store(uint64_t* v_regs, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    uint64_t a_bits = v_regs[rs1];
    uint64_t b_bits = v_regs[rs2];
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmin(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
#if ESPB_JIT_DEBUG_OPTOCODES
    ESP_LOGI(TAG, "[MIN.F64] v_regs=%p rd=%d rs1=%d rs2=%d in1=%.6f in2=%.6f out=%.6f", v_regs, rd, rs1, rs2, a, b, r);
#endif
    v_regs[rd] = r_bits;
}

// Helper that does fmax AND stores result directly to v_regs
__attribute__((noinline))
static void jit_helper_fmax_f64_store(uint64_t* v_regs, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    uint64_t a_bits = v_regs[rs1];
    uint64_t b_bits = v_regs[rs2];
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmax(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
#if ESPB_JIT_DEBUG_OPTOCODES
    ESP_LOGI(TAG, "[MAX.F64] v_regs=%p rd=%d rs1=%d rs2=%d in1=%.6f in2=%.6f out=%.6f", v_regs, rd, rs1, rs2, a, b, r);
#endif
    v_regs[rd] = r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fsqrt_f64_bits(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    double r = sqrt(a);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fmin_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    // Use fmin() for proper ABI handling
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmin(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fmax_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    // Use fmax() for proper ABI handling
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmax(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r_bits));
    return r_bits;
}

__attribute__((noinline))
static int32_t jit_helper_cvt_f64_i32_bits(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    return (int32_t)a;
}

// ===== Xtensa Register Allocation =====
// s1 (a9): instance pointer
// s2 (a10): v_regs base pointer  
// Callee-saved: a12-a15 (if needed)
// Caller-saved: a2-a7 (arguments), a8 (scratch), a11 (scratch)

// ===== JIT Context =====
typedef struct {
    uint8_t* buffer;
    size_t capacity;   // bytes
    size_t offset;     // logical byte offset
    bool error;

    // Physical write buffer for IRAM (word-only writes)
    uint32_t word_buf;
    uint8_t word_fill; // 0..3 bytes filled in word_buf
    
    // For updating bc_to_native after literal pool flush
    uint32_t* bc_to_native;
    size_t current_bc_off;
    size_t code_size;
} XtensaJitContext;

// ===== Low-level Xtensa Emitters =====

// Forward declarations (some emitters call others defined later)
static void emit_mov_n(XtensaJitContext* ctx, uint8_t ar, uint8_t as);
static void emit_addi(XtensaJitContext* ctx, uint8_t ar, uint8_t as, int8_t imm);

// Forward declaration (emit_u8 calls emit_flush_words)
static inline void emit_flush_words(XtensaJitContext* ctx);

static inline void emit_u8(XtensaJitContext* ctx, uint8_t byte) {
    if (ctx->error) return;
    if (ctx->offset >= ctx->capacity) {
        ctx->error = true;
        return;
    }

    // IRAM requires 32-bit aligned writes. We accumulate bytes into a word buffer
    // and commit when we have a full word at a word-aligned boundary.
    //
    // Key insight: byte position within word is determined by (offset % 4), NOT by word_fill!
    // word_fill tracks how many bytes we've accumulated since last commit.
    
    uint32_t byte_pos_in_word = ctx->offset & 3u;  // 0, 1, 2, or 3
    
    // If starting a new word (byte_pos == 0), ensure word_buf is clean
    if (byte_pos_in_word == 0 && ctx->word_fill != 0) {
        // We have leftover bytes from previous non-aligned sequence - this shouldn't happen
        // if alignment is done correctly, but let's handle it by flushing first.
        emit_flush_words(ctx);
        if (ctx->error) return;
    }
    
    // Place byte at correct position in word buffer
    ctx->word_buf |= ((uint32_t)byte) << (8u * byte_pos_in_word);
    ctx->word_fill++;
    ctx->offset++;

    // Commit when we complete a word (offset is now at next word boundary)
    if ((ctx->offset & 3u) == 0) {
        size_t word_start = ctx->offset - 4;
        if (word_start + 4 > ctx->capacity) {
            ctx->error = true;
            return;
        }
        // CRITICAL FIX: If we didn't fill all 4 bytes of the word (word_fill < 4),
        // we must use RMW merge to preserve bytes that were written by emit_flush_words
        // or store_u8_exec earlier. This happens when emit_flush_words is called mid-word
        // and then the next opcode continues emitting into the same word.
        if (ctx->word_fill < 4) {
            // RMW merge: preserve bytes we didn't write
            uint32_t existing = *(uint32_t*)(ctx->buffer + word_start);
            // Calculate which byte positions we filled (from offset - word_fill to offset - 1)
            uint32_t first_byte_pos = (ctx->offset - ctx->word_fill) & 3u;
            uint32_t mask = 0;
            for (uint32_t i = 0; i < (uint32_t)ctx->word_fill; i++) {
                mask |= (0xFFu << (8u * (first_byte_pos + i)));
            }
            uint32_t merged = (existing & ~mask) | (ctx->word_buf & mask);
            *(uint32_t*)(ctx->buffer + word_start) = merged;
        } else {
            // Full word write - we wrote all 4 bytes
            *(uint32_t*)(ctx->buffer + word_start) = ctx->word_buf;
        }
        ctx->word_buf = 0;
        ctx->word_fill = 0;
    }
}

static inline void emit_flush_words(XtensaJitContext* ctx) {
    // Commit a partially filled word WITHOUT changing logical ctx->offset.
    // This is used before patching operations that need to read-modify-write existing bytes.
    if (ctx->error) return;
    if (ctx->word_fill == 0) return;

    // word_start is the aligned address where our current word buffer should be written
    size_t word_start = ctx->offset & ~3u;
    
    // If offset is at word boundary, the bytes are from the previous word
    if ((ctx->offset & 3u) == 0) {
        word_start = ctx->offset - 4;
    }
    
    if (word_start + 4 > ctx->capacity) {
        ctx->error = true;
        return;
    }

    // Read existing word, merge our bytes, write back
    // This handles the case where we're writing to a partially-filled word
    uint32_t existing = *(uint32_t*)(ctx->buffer + word_start);
    
    // Create mask for bytes we've written (based on positions we filled)
    // Our bytes start at position (word_start) and we have word_fill bytes
    uint32_t first_byte_pos = (ctx->offset - ctx->word_fill) & 3u;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < (uint32_t)ctx->word_fill; i++) {
        mask |= (0xFFu << (8u * (first_byte_pos + i)));
    }
    
    // Merge: keep existing bytes where we didn't write, use our bytes where we did
    uint32_t merged = (existing & ~mask) | (ctx->word_buf & mask);
    
    *(uint32_t*)(ctx->buffer + word_start) = merged;
    ctx->word_buf = 0;
    ctx->word_fill = 0;
}
static inline void emit_u16(XtensaJitContext* ctx, uint16_t val) {
    emit_u8(ctx, val & 0xFF);
    emit_u8(ctx, (val >> 8) & 0xFF);
}

static inline void emit_u24(XtensaJitContext* ctx, uint32_t val) {
    emit_u8(ctx, val & 0xFF);
    emit_u8(ctx, (val >> 8) & 0xFF);
    emit_u8(ctx, (val >> 16) & 0xFF);
}

// ===== Safe patching stores for EXEC memory (no byte stores) =====
static inline void store_u8_exec(uint8_t* buf, uint32_t pos, uint8_t v) {
    uint32_t word_pos = pos & ~3u;
    uint32_t shift = (pos & 3u) * 8u;
    uint32_t w = *(uint32_t*)(buf + word_pos);
    w = (w & ~(0xFFu << shift)) | ((uint32_t)v << shift);
    *(uint32_t*)(buf + word_pos) = w;
}

static inline void store_u16_exec(uint8_t* buf, uint32_t pos, uint16_t v) {
    // Little-endian 16-bit store via two byte RMWs.
    store_u8_exec(buf, pos + 0, (uint8_t)(v & 0xFF));
    store_u8_exec(buf, pos + 1, (uint8_t)((v >> 8) & 0xFF));
}

// MOVI.N aR, imm4s (narrow, -1..15)
static void emit_movi_n(XtensaJitContext* ctx, uint8_t ar, int8_t imm) {
    // Verified by objdump:
    //   movi.n a8,0  => word 0x080C (bytes 0C 08)
    //   movi.n a11,6 => word 0x6B0C (bytes 0C 6B)
    // Encoding: low byte = 0x0C, high byte = (imm4<<4) | reg
    // => ins = 0x000C | (reg<<8) | (imm4<<12)
    uint8_t imm4 = (uint8_t)imm & 0xF;
    uint16_t ins = (uint16_t)(0x000C | ((ar & 0xF) << 8) | ((imm4 & 0xF) << 12));
    emit_u16(ctx, ins);
}

// MOVI aR, imm12s (full, -2048..2047)
static void emit_movi(XtensaJitContext* ctx, uint8_t ar, int16_t imm) {
    // Verified by objdump (memory byte order):
    //   movi a12, -128 => bytes C2 AF 80
    //   movi a12, 127  => bytes C2 A0 7F
    // Encoding (memory order):
    //   byte0 = (ar << 4) | 0x02
    //   byte1 = 0xA0 | imm[11:8]
    //   byte2 = imm[7:0]
    emit_u8(ctx, (uint8_t)(((ar & 0xF) << 4) | 0x02));     // byte0: ar/opcode
    emit_u8(ctx, (uint8_t)(0xA0 | ((imm >> 8) & 0x0F)));   // byte1: high 4 bits of imm
    emit_u8(ctx, (uint8_t)(imm & 0xFF));                   // byte2: low 8 bits of imm
}

// MOVEQZ ar, as, at - Move if equal to zero
// if (at == 0) ar = as
// Encoding: RRR format - op0=0, op1=3, op2=8
// Byte order: byte0 = (at<<4)|op0, byte1 = (as<<4)|(ar), byte2 = (op2<<4)|op1
static void emit_moveqz(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t at) {
    // MOVEQZ encoding: op0=0, op1=3, op2=8
    // From Xtensa ISA: MOVEQZ ar, as, at
    //   byte0 = (at << 4) | 0x00
    //   byte1 = (as << 4) | ar
    //   byte2 = 0x83  (op2=8, op1=3)
    emit_u8(ctx, (uint8_t)((at & 0xF) << 4));              // byte0
    emit_u8(ctx, (uint8_t)(((as & 0xF) << 4) | (ar & 0xF)));  // byte1
    emit_u8(ctx, 0x83);                                    // byte2: op2=8, op1=3
}

// L32I aT, aS, offset (load 32-bit, offset in words 0-1020)
// NOTE: Xtensa immediate field is 8-bit in words. For larger offsets we synthesize address arithmetic
// into a scratch register (a7) and then use l32i with offset 0.
static void emit_l32i_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    // Narrow form verified:
    //   l32i.n a8, a11, 0 => word 0x0B88 (bytes 88 0B)
    // Encoding: low byte encodes op+dest (a8), high byte = (offw<<4) | base
    // NOTE: we currently only support dest=a8 in narrow form.
    if (at == 8 && (offset_bytes % 4) == 0 && offset_bytes <= 60) {
        uint8_t offw = (uint8_t)(offset_bytes / 4);
        emit_u8(ctx, 0x88);
        emit_u8(ctx, (uint8_t)(((offw & 0xF) << 4) | (as & 0xF)));
        return;
    }

    // Full 24-bit form verified by objdump:
    //   l32i a8, a11, 64  => 102b82 (bytes 82 2B 10)
    //   l32i a8, a11, 128 => 202b82 (bytes 82 2B 20)
    //   l32i a9, a11, 64  => 102b92 (bytes 92 2B 10)
    //   l32i a8, a10, 64  => 102a82 (bytes 82 2A 10)
    if ((offset_bytes % 4) == 0) {
        uint32_t offw = (uint32_t)(offset_bytes / 4);
        if (offw <= 0xFFu) {
            // Encoding (LE bytes):
            //   b0 = (at<<4) | 0x2
            //   b1 = (0x2<<4) | as
            //   b2 = offw
            emit_u8(ctx, (uint8_t)(((at & 0xF) << 4) | 0x2u));
            emit_u8(ctx, (uint8_t)(0x20u | (as & 0xFu)));
            emit_u8(ctx, (uint8_t)offw);
            return;
        }
    }

    ESP_LOGE(TAG, "emit_l32i_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_l32i(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;

    if ((offset_bytes % 4) == 0) {
        uint32_t offw = (uint32_t)(offset_bytes / 4);
        if (offw <= 0xFFu) {
            emit_l32i_raw(ctx, at, as, offset_bytes);
            return;
        }

        // Large offset: use a7 as scratch address
        // Requirements: a7 must not conflict with src/base/dst regs.
        if (at == 7 || as == 7) {
            ESP_LOGE(TAG, "emit_l32i: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
            ctx->error = true;
            return;
        }

        emit_mov_n(ctx, 7, as); // a7 = base

        // Add offset in chunks of +/-127 bytes using ADDI (imm8s)
        int32_t rem = (int32_t)offset_bytes;
        while (rem > 127) {
            emit_addi(ctx, 7, 7, 127);
            rem -= 127;
        }
        while (rem < -128) {
            emit_addi(ctx, 7, 7, -128);
            rem += 128;
        }
        if (rem != 0) {
            emit_addi(ctx, 7, 7, (int8_t)rem);
        }

        emit_l32i_raw(ctx, at, 7, 0);
        return;
    }

    ESP_LOGE(TAG, "emit_l32i: unsupported unaligned offset at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

// S32I aT, aS, offset (store 32-bit, offset in words 0-1020)
static void emit_s32i_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    // Narrow form verified:
    //   s32i.n a8, a1, 0  => word 0x0189 (bytes 89 01)
    //   s32i.n a8, a1,16  => word 0x4189 (bytes 89 41)
    // Encoding: low byte 0x89 for src=a8, high byte = (offw<<4) | base
    // NOTE: we currently only support src=a8 in narrow form.
    if (at == 8 && (offset_bytes % 4) == 0 && offset_bytes <= 60) {
        uint8_t offw = (uint8_t)(offset_bytes / 4);
        emit_u8(ctx, 0x89);
        emit_u8(ctx, (uint8_t)(((offw & 0xF) << 4) | (as & 0xF)));
        return;
    }

    // Full 24-bit form verified by objdump:
    //   s32i a8, a11, 64  => 106b82 (bytes 82 6B 10)
    //   s32i a8, a11, 128 => 206b82 (bytes 82 6B 20)
    //   s32i a9, a11, 64  => 106b92 (bytes 92 6B 10)
    //   s32i a8, a10, 64  => 106a82 (bytes 82 6A 10)
    if ((offset_bytes % 4) == 0) {
        uint32_t offw = (uint32_t)(offset_bytes / 4);
        if (offw <= 0xFFu) {
            emit_u8(ctx, (uint8_t)(((at & 0xF) << 4) | 0x2u));
            emit_u8(ctx, (uint8_t)(0x60u | (as & 0xFu)));
            emit_u8(ctx, (uint8_t)offw);
            return;
        }
    }

    ESP_LOGE(TAG, "emit_s32i_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_s32i(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;

    if ((offset_bytes % 4) == 0) {
        uint32_t offw = (uint32_t)(offset_bytes / 4);
        if (offw <= 0xFFu) {
            emit_s32i_raw(ctx, at, as, offset_bytes);
            return;
        }

        // Large offset: use a7 as scratch address
        if (at == 7 || as == 7) {
            ESP_LOGE(TAG, "emit_s32i: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
            ctx->error = true;
            return;
        }

        emit_mov_n(ctx, 7, as);
        int32_t rem = (int32_t)offset_bytes;
        while (rem > 127) {
            emit_addi(ctx, 7, 7, 127);
            rem -= 127;
        }
        while (rem < -128) {
            emit_addi(ctx, 7, 7, -128);
            rem += 128;
        }
        if (rem != 0) {
            emit_addi(ctx, 7, 7, (int8_t)rem);
        }

        emit_s32i_raw(ctx, at, 7, 0);
        return;
    }

    ESP_LOGE(TAG, "emit_s32i: unsupported unaligned offset at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

// S8I aT, aS, off (store byte)
// Verified by objdump:
//   s8i a8,a1,16 => 104182 (82 41 10)
//   s8i a8,a1,17 => 114182 (82 41 11)
// Encoding (3-byte): byte0 = (aT<<4)|0x2, byte1 = 0x40|aS, byte2 = off
static void emit_s8i_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (offset_bytes <= 0xFFu) {
        emit_u8(ctx, (uint8_t)(((at & 0xFu) << 4) | 0x2u));
        emit_u8(ctx, (uint8_t)(0x40u | (as & 0xFu)));
        emit_u8(ctx, (uint8_t)offset_bytes);
        return;
    }

    ESP_LOGE(TAG, "emit_s8i_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_s8i(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;

    if (offset_bytes <= 0xFFu) {
        emit_s8i_raw(ctx, at, as, offset_bytes);
        return;
    }

    // Large offset: use a7 as scratch address
    if (at == 7 || as == 7) {
        ESP_LOGE(TAG, "emit_s8i: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
        ctx->error = true;
        return;
    }

    emit_mov_n(ctx, 7, as);
    int32_t rem = (int32_t)offset_bytes;
    while (rem > 127) { emit_addi(ctx, 7, 7, 127); rem -= 127; }
    while (rem < -128) { emit_addi(ctx, 7, 7, -128); rem += 128; }
    if (rem != 0) emit_addi(ctx, 7, 7, (int8_t)rem);

    emit_s8i_raw(ctx, at, 7, 0);
}

// S16I aT, aS, off (store halfword)
// Verified by objdump:
//   s16i a8,a1,16 => 085182 (82 51 08)
//   s16i a8,a1,18 => 095182 (82 51 09)
// Encoding (3-byte): byte0 = (aT<<4)|0x2, byte1 = 0x50|aS, byte2 = off/2
static void emit_s16i_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_u8(ctx, (uint8_t)(((at & 0xFu) << 4) | 0x2u));
            emit_u8(ctx, (uint8_t)(0x50u | (as & 0xFu)));
            emit_u8(ctx, (uint8_t)offh);
            return;
        }
    }

    ESP_LOGE(TAG, "emit_s16i_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_s16i(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;

    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_s16i_raw(ctx, at, as, offset_bytes);
            return;
        }
    }

    // Large offset: use a7 as scratch address
    if (at == 7 || as == 7) {
        ESP_LOGE(TAG, "emit_s16i: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
        ctx->error = true;
        return;
    }

    emit_mov_n(ctx, 7, as);
    int32_t rem = (int32_t)offset_bytes;
    while (rem > 127) { emit_addi(ctx, 7, 7, 127); rem -= 127; }
    while (rem < -128) { emit_addi(ctx, 7, 7, -128); rem += 128; }
    if (rem != 0) emit_addi(ctx, 7, 7, (int8_t)rem);

    emit_s16i_raw(ctx, at, 7, 0);
}

// L8UI aT, aS, off (load unsigned byte)
// Encoding (3-byte RRI8): byte0 = (t<<4)|op0, byte1 = (op1<<4)|s, byte2 = imm8
// For L8UI: op0=2, op1=0
// Verified by Xtensa ISA: L8UI uses op0=2, op1=0
static void emit_l8ui_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (offset_bytes <= 0xFFu) {
        emit_u8(ctx, (uint8_t)(((at & 0xFu) << 4) | 0x2u));       // byte0: t[7:4], op0=2[3:0]
        emit_u8(ctx, (uint8_t)(0x00u | (as & 0xFu)));             // byte1: op1=0[7:4], s[3:0]
        emit_u8(ctx, (uint8_t)offset_bytes);                      // byte2: imm8
        return;
    }
    ESP_LOGE(TAG, "emit_l8ui_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_l8ui(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;
    if (offset_bytes <= 0xFFu) {
        emit_l8ui_raw(ctx, at, as, offset_bytes);
        return;
    }
    // Large offset: use a7 as scratch address
    if (at == 7 || as == 7) {
        ESP_LOGE(TAG, "emit_l8ui: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
        ctx->error = true;
        return;
    }
    emit_mov_n(ctx, 7, as);
    int32_t rem = (int32_t)offset_bytes;
    while (rem > 127) { emit_addi(ctx, 7, 7, 127); rem -= 127; }
    while (rem < -128) { emit_addi(ctx, 7, 7, -128); rem += 128; }
    if (rem != 0) emit_addi(ctx, 7, 7, (int8_t)rem);
    emit_l8ui_raw(ctx, at, 7, 0);
}

// L16UI aT, aS, off (load unsigned 16-bit)
// Encoding (3-byte): byte0 = (aT<<4)|0x2, byte1 = 0x10|aS, byte2 = off/2
// Verified by Xtensa ISA: L16UI uses op0=2, op1=1
static void emit_l16ui_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_u8(ctx, (uint8_t)(((at & 0xFu) << 4) | 0x2u));
            emit_u8(ctx, (uint8_t)(0x10u | (as & 0xFu)));
            emit_u8(ctx, (uint8_t)offh);
            return;
        }
    }
    ESP_LOGE(TAG, "emit_l16ui_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_l16ui(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;
    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_l16ui_raw(ctx, at, as, offset_bytes);
            return;
        }
    }
    // Large offset or unaligned: use a7 as scratch address
    if (at == 7 || as == 7) {
        ESP_LOGE(TAG, "emit_l16ui: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
        ctx->error = true;
        return;
    }
    emit_mov_n(ctx, 7, as);
    int32_t rem = (int32_t)offset_bytes;
    while (rem > 127) { emit_addi(ctx, 7, 7, 127); rem -= 127; }
    while (rem < -128) { emit_addi(ctx, 7, 7, -128); rem += 128; }
    if (rem != 0) emit_addi(ctx, 7, 7, (int8_t)rem);
    emit_l16ui_raw(ctx, at, 7, 0);
}

// L16SI aT, aS, off (load signed 16-bit)
// Encoding (3-byte): byte0 = (aT<<4)|0x2, byte1 = 0x90|aS, byte2 = off/2
// Verified by Xtensa ISA: L16SI uses op0=2, op1=9
static void emit_l16si_raw(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_u8(ctx, (uint8_t)(((at & 0xFu) << 4) | 0x2u));
            emit_u8(ctx, (uint8_t)(0x90u | (as & 0xFu)));
            emit_u8(ctx, (uint8_t)offh);
            return;
        }
    }
    ESP_LOGE(TAG, "emit_l16si_raw: unsupported form at=%u as=%u off_bytes=%u", (unsigned)at, (unsigned)as, (unsigned)offset_bytes);
    ctx->error = true;
}

static void emit_l16si(XtensaJitContext* ctx, uint8_t at, uint8_t as, uint16_t offset_bytes) {
    if (ctx->error) return;
    if ((offset_bytes % 2) == 0) {
        uint32_t offh = (uint32_t)(offset_bytes / 2);
        if (offh <= 0xFFu) {
            emit_l16si_raw(ctx, at, as, offset_bytes);
            return;
        }
    }
    // Large offset or unaligned: use a7 as scratch address
    if (at == 7 || as == 7) {
        ESP_LOGE(TAG, "emit_l16si: large offset needs scratch a7, conflict at=%u as=%u", (unsigned)at, (unsigned)as);
        ctx->error = true;
        return;
    }
    emit_mov_n(ctx, 7, as);
    int32_t rem = (int32_t)offset_bytes;
    while (rem > 127) { emit_addi(ctx, 7, 7, 127); rem -= 127; }
    while (rem < -128) { emit_addi(ctx, 7, 7, -128); rem += 128; }
    if (rem != 0) emit_addi(ctx, 7, 7, (int8_t)rem);
    emit_l16si_raw(ctx, at, 7, 0);
}

// SLLI aR, aS, sa (shift left logical immediate)
// Verified by objdump (big-endian display, little-endian memory):
//   slli a8, a8, 1   => f08811 => bytes: f0 88 11 (imm=31)
//   slli a8, a8, 8   => 808811 => bytes: 80 88 11 (imm=24)
//   slli a8, a8, 16  => 008811 => bytes: 00 88 11 (imm=16)
//   slli a8, a8, 24  => 808801 => bytes: 80 88 01 (imm=8)
//   slli a8, a9, 16  => 008911 => bytes: 00 89 11 (imm=16)
// Encoding: imm = 32 - sa
//   byte0 = (imm & 0xF) << 4
//   byte1 = (ar << 4) | as
//   byte2 = ((imm >> 4) << 4) | 0x01
static void emit_slli(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t sa) {
    if (sa == 0 || sa > 31) {
        ESP_LOGE(TAG, "emit_slli: invalid shift amount sa=%u", (unsigned)sa);
        ctx->error = true;
        return;
    }
    uint8_t imm = (uint8_t)(32 - sa);
    emit_u8(ctx, (uint8_t)((imm & 0xF) << 4));
    emit_u8(ctx, (uint8_t)(((ar & 0xF) << 4) | (as & 0xF)));
    emit_u8(ctx, (uint8_t)(((imm >> 4) << 4) | 0x01u));
}

// SRAI aR, aS, sa (shift right arithmetic immediate)
// Verified by objdump (big-endian display, little-endian memory):
//   srai a8, a8, 1   => 808121 => bytes: 80 81 21
//   srai a8, a8, 8   => 808821 => bytes: 80 88 21
//   srai a8, a8, 16  => 808031 => bytes: 80 80 31
//   srai a8, a8, 24  => 808831 => bytes: 80 88 31
//   srai a8, a9, 16  => 908031 => bytes: 90 80 31
// Encoding:
//   byte0 = ar << 4
//   byte1 = (as << 4) | (sa & 0xF)
//   byte2 = ((sa >> 4) << 4) | 0x21
static void emit_srai(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t sa) {
    if (sa > 31) {
        ESP_LOGE(TAG, "emit_srai: invalid shift amount sa=%u", (unsigned)sa);
        ctx->error = true;
        return;
    }
    emit_u8(ctx, (uint8_t)((ar & 0xF) << 4));
    emit_u8(ctx, (uint8_t)(((as & 0xF) << 4) | (sa & 0xF)));
    emit_u8(ctx, (uint8_t)(((sa >> 4) << 4) | 0x21u));
}

// Sign-extend 8-bit to 32-bit using shift left then arithmetic shift right
// result = (val << 24) >> 24  (arithmetic)
static void emit_sext_i8(XtensaJitContext* ctx, uint8_t ar, uint8_t as) {
    emit_slli(ctx, ar, as, 24);
    emit_srai(ctx, ar, ar, 24);
}

// ADDI a8, a1, 16
// Verified by objdump: addi a8, a1, 16 => 10c182 (bytes 82 C1 10)
static void emit_addi_a8_a1_16(XtensaJitContext* ctx) {
    emit_u8(ctx, 0x82);
    emit_u8(ctx, 0xC1);
    emit_u8(ctx, 0x10);
}

// MOV.N aR, aS (narrow move, 2 bytes)
static void emit_mov_n(XtensaJitContext* ctx, uint8_t ar, uint8_t as) {
    // Verified by objdump:
    //   mov.n a12,a11 => bytes CD 0B
    // Encoding: base 0x0D, reg fields packed.
    uint16_t ins = (uint16_t)(0x000D | ((ar & 0xF) << 4) | ((as & 0xF) << 8));
    emit_u16(ctx, ins);
}

// SUB a8, a8, a9 (3-byte)
static void emit_sub_a8_a8_a9(XtensaJitContext* ctx) {
    // Verified by objdump: sub a8, a8, a9 => bytes 90 88 C0
    emit_u8(ctx, 0x90);
    emit_u8(ctx, 0x88);
    emit_u8(ctx, 0xC0);
}

// ADD.N aR, aS, aT (2-byte)
// Verified by objdump:
//   add.n a8,  a8, a9  => bytes 88 9A
//   add.n a10, a8, a9  => bytes A8 9A
// Encoding (verified by objdump):
//   add.n a8,  a8, a9  => halfword 0x889A => bytes 9A 88
// So:
//   byte0 = (aT << 4) | 0xA
//   byte1 = (aS << 4) | aR
static void emit_add_n(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t at) {
    if ((ar | as | at) & 0xF0) {
        ESP_LOGE(TAG, "emit_add_n: regs out of range ar=%u as=%u at=%u", (unsigned)ar, (unsigned)as, (unsigned)at);
        ctx->error = true;
        return;
    }
    uint8_t b0 = (uint8_t)(((at & 0xF) << 4) | 0xAu);
    uint8_t b1 = (uint8_t)(((as & 0xF) << 4) | (ar & 0xF));
    emit_u8(ctx, b0);
    emit_u8(ctx, b1);
}

// OR aR, aS, aT (3-byte) - bitwise OR
// Verified by objdump:
//   or a9, a9, a10 => 2099a0 (bytes a0 99 20)
//   or a8, a9, a10 => 2089a0 (bytes a0 89 20)
// Encoding: byte0 = (at << 4) | 0x0, byte1 = (as << 4) | ar, byte2 = 0x20
static void emit_or(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t at) {
    if ((ar | as | at) & 0xF0) {
        ESP_LOGE(TAG, "emit_or: regs out of range ar=%u as=%u at=%u", (unsigned)ar, (unsigned)as, (unsigned)at);
        ctx->error = true;
        return;
    }
    uint8_t b0 = (uint8_t)(((at & 0xF) << 4) | 0x0u);
    uint8_t b1 = (uint8_t)(((as & 0xF) << 4) | (ar & 0xF));
    uint8_t b2 = 0x20u;
    emit_u8(ctx, b0);
    emit_u8(ctx, b1);
    emit_u8(ctx, b2);
}

// EXTUI aR, aS, shift, width (3-byte) - extract unsigned immediate
// Verified by objdump (big-endian display, little-endian memory):
//   extui a10, a9, 8, 8   => 74a890 => bytes: a0 98 74 (shift=8, width=8)
//   extui a10, a9, 16, 8  => 75a090 => bytes: a0 90 75 (shift=16, width=8)
//   extui a10, a9, 24, 8  => 75a890 => bytes: a0 98 75 (shift=24, width=8)
//   extui a8, a8, 0, 16   => 8080f4 => bytes: 80 80 f4 (shift=0, width=16)
// Encoding:
//   byte0 = (ar << 4) | 0x0
//   byte1 = (as << 4) | (shift & 0xF)
//   byte2 = ((width-1) << 4) | 0x04 | (shift >= 16 ? 1 : 0)
static void emit_extui(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t shift, uint8_t width) {
    if ((ar | as) & 0xF0) {
        ESP_LOGE(TAG, "emit_extui: regs out of range ar=%u as=%u", (unsigned)ar, (unsigned)as);
        ctx->error = true;
        return;
    }
    uint8_t shift_low = shift & 0xF;
    uint8_t shift_high = (shift >> 4) & 0x1;
    uint8_t b0 = (uint8_t)(((ar & 0xF) << 4) | 0x0u);
    uint8_t b1 = (uint8_t)(((as & 0xF) << 4) | shift_low);
    uint8_t b2 = (uint8_t)(((width - 1) << 4) | 0x04u | shift_high);
    emit_u8(ctx, b0);
    emit_u8(ctx, b1);
    emit_u8(ctx, b2);
}

// SRLI aR, aS, sa - shift right logical immediate (for sa <= 15)
// For sa > 15, use emit_extui instead
static void emit_srli(XtensaJitContext* ctx, uint8_t ar, uint8_t as, uint8_t sa) {
    if (sa > 15) {
        // Use EXTUI for larger shifts - extract remaining bits
        // This effectively does ar = (as >> sa) with zero-extension
        emit_extui(ctx, ar, as, sa, 32 - sa);
        return;
    }
    if ((ar | as) & 0xF0) {
        ESP_LOGE(TAG, "emit_srli: regs out of range ar=%u as=%u", (unsigned)ar, (unsigned)as);
        ctx->error = true;
        return;
    }
    // Verified by objdump: srli a10, a9, 8 => 90 a8 41
    // Encoding:
    //   byte0 = (as << 4) | 0x0
    //   byte1 = (ar << 4) | sa
    //   byte2 = 0x41
    uint8_t b0 = (uint8_t)(((as & 0xF) << 4) | 0x0u);
    uint8_t b1 = (uint8_t)(((ar & 0xF) << 4) | (sa & 0xF));
    uint8_t b2 = 0x41u;
    emit_u8(ctx, b0);
    emit_u8(ctx, b1);
    emit_u8(ctx, b2);
}

// BLTU aS, aT, target (3-byte) -- forward patchable, small range
// Verified by objdump:
//   bltu a8, a12, +? => bytes C7 38 imm
//   bltu a8, a13, +? => bytes D7 38 imm
//   bltu a9, a12, +? => bytes C7 39 imm
// Encoding (LE bytes) verified by objdump:
//   bltu a8, a13, +? => bytes D7 38 imm
//   bltu a8, a12, +? => bytes C7 38 imm
//   bltu a9, a12, +? => bytes C7 39 imm
// So:
//   byte0 = (aT << 4) | 0x7
//   byte1 = 0x30 | (aS & 0xF)
//   byte2 = imm8, where delta_bytes = (imm8 + 1)
static uint32_t emit_bltu_placeholder(XtensaJitContext* ctx, uint8_t as, uint8_t at) {
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u8(ctx, (uint8_t)(((at & 0xF) << 4) | 0x7u));
    emit_u8(ctx, (uint8_t)(0x30u | (as & 0xFu)));
    emit_u8(ctx, 0x00); // imm placeholder (delta=1)
    return pos;
}

static void patch_bltu_at(uint8_t* buf, uint32_t br_pos, int32_t target) {
    // pc_after = br_pos + 3
    int32_t delta = target - (int32_t)(br_pos + 3);
    // encoding uses delta = imm + 1
    int32_t imm = delta - 1;
    if (imm < 0 || imm > 0xFF) {
        ESP_LOGE(TAG, "patch_bltu_at: out of range br_pos=%u target=%d delta=%d imm=%d", (unsigned)br_pos, (int)target, (int)delta, (int)imm);
        return;
    }
    store_u8_exec(buf, br_pos + 2, (uint8_t)imm);
}

// BGEU aS, aT, target (3-byte) forward patchable.
// Verified by objdump:
//   bgeu a8, a13, +? => bytes D7 B8 imm
// So:
//   byte0 = (aT << 4) | 0x7
//   byte1 = 0xB0 | (aS & 0xF)
//   byte2 = imm8, where delta_bytes = (imm8 + 1)
static uint32_t emit_bgeu_placeholder(XtensaJitContext* ctx, uint8_t as, uint8_t at) {
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u8(ctx, (uint8_t)(((at & 0xF) << 4) | 0x7u));
    emit_u8(ctx, (uint8_t)(0xB0u | (as & 0xFu)));
    emit_u8(ctx, 0x00);
    return pos;
}

static void patch_bgeu_at(uint8_t* buf, uint32_t br_pos, int32_t target) {
    int32_t delta = target - (int32_t)(br_pos + 3);
    int32_t imm = delta - 1;
    if (imm < 0 || imm > 0xFF) {
        ESP_LOGE(TAG, "patch_bgeu_at: out of range br_pos=%u target=%d delta=%d imm=%d", (unsigned)br_pos, (int)target, (int)delta, (int)imm);
        return;
    }
    store_u8_exec(buf, br_pos + 2, (uint8_t)imm);
}

// Generic conditional branches for a8,a9 using 0x97 form (3 bytes), patchable forward.
// Verified by objdump for a8,a9:
//   beq  => bytes 97 18 imm
//   bne  => bytes 97 98 imm
//   blt  => bytes 97 28 imm
//   bge  => bytes 97 A8 imm
//   bltu => bytes 97 38 imm
//   bgeu => bytes 97 B8 imm
// Encoding:
//   byte0 = 0x97
//   byte1 = (cond_nibble << 4) | 0x8
//   byte2 = imm8, delta_bytes = (imm8 + 1)
static uint32_t emit_bcc_a8_a9_placeholder(XtensaJitContext* ctx, uint8_t cond_nibble) {
    // BNE/BEQ a8, a9 - verified by objdump:
    //   bne a8, a9, +6 => bytes 97 98 02 in memory
    // So: byte0=0x97, byte1=(cond<<4)|0x8, byte2=imm8
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u8(ctx, 0x97);  // byte 0: opcode base
    emit_u8(ctx, (uint8_t)((cond_nibble << 4) | 0x08));  // byte 1: (cond << 4) | 0x8
    emit_u8(ctx, 0x00);  // byte 2: imm8 placeholder
    return pos;
}

static void patch_bcc_a8_a9_at(uint8_t* buf, uint32_t br_pos, int32_t target) {
    // BNE/BEQ a8, a9 - verified by objdump:
    //   bne a8, a9, +6 => bytes 97 98 02 in memory
    // imm8 is in byte 2 (third byte)
    // Formula: target = PC + 4 + imm8, so imm8 = target - (br_pos + 4)
    // But from testing: delta_bytes = imm8 + 1, so imm8 = delta - 1 where delta = target - (br_pos + 3)
    int32_t delta = target - (int32_t)(br_pos + 3);
    int32_t imm = delta - 1;
    if (imm < 0 || imm > 0xFF) {
        ESP_LOGE(TAG, "patch_bcc_a8_a9_at: out of range br_pos=%u target=%d delta=%d imm=%d", (unsigned)br_pos, (int)target, (int)delta, (int)imm);
        return;
    }
    store_u8_exec(buf, br_pos + 2, (uint8_t)imm);  // imm8 is in byte 2
}

// Forward decl for literal pool (used by some emitters before the full definition)
typedef struct XtensaLiteralPool XtensaLiteralPool;

// NOP.N (2-byte)
static void emit_nop_n(XtensaJitContext* ctx) {
    // Verified by objdump: nop.n => halfword 0xF03D (bytes 3D F0)
    emit_u16(ctx, 0xF03D);
}

// NOP (3-byte)
static void emit_nop3(XtensaJitContext* ctx) {
    // Verified by objdump: nop => 0020f0 (bytes F0 20 00)
    emit_u8(ctx, 0xF0);
    emit_u8(ctx, 0x20);
    emit_u8(ctx, 0x00);
}

static void emit_align4_with_nops(XtensaJitContext* ctx) {
    // Align code position to 4 bytes.
    // Handle both odd and even misalignment cases:
    //   offset % 4 == 1: emit 3-byte NOP to reach 4-byte boundary
    //   offset % 4 == 2: emit 2-byte NOP.N to reach 4-byte boundary
    //   offset % 4 == 3: emit 3-byte NOP (to reach 6, mod 4 = 2), then 2-byte NOP.N
    //   offset % 4 == 0: already aligned
    uint32_t rem = ctx->offset & 3u;
    if (rem == 0) {
        return;
    } else if (rem == 1) {
        emit_nop3(ctx);  // +3 -> offset % 4 == 0
    } else if (rem == 2) {
        emit_nop_n(ctx); // +2 -> offset % 4 == 0
    } else { // rem == 3
        emit_nop3(ctx);  // +3 -> offset % 4 == 2
        emit_nop_n(ctx); // +2 -> offset % 4 == 0
    }
}

// JX a8 (absolute jump via register) (3-byte)
static void emit_jx_a8(XtensaJitContext* ctx) {
    // Verified by objdump: jx a8 => bytes A0 08 00
    emit_u8(ctx, 0xA0);
    emit_u8(ctx, 0x08);
    emit_u8(ctx, 0x00);
}

// BEQZ.N a8, target (2-byte) -- patchable
// Encoding derived from multiple objdump samples (a8 only):
//   word = 0x?88C / 0x?89C, where immediate imm5 encodes delta_bytes.
// We use the form emitted by the assembler for 'beqz a8, label' which becomes 'beqz.n'.
// imm = (delta_bytes - 2), where delta_bytes = target - pc_after_branch.
// imm is 5 bits (0..31) split as: imm[3:0] -> bits[15:12], imm[4] -> bit[4].
static uint32_t emit_beqz_n_a8_placeholder(XtensaJitContext* ctx) {
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u16(ctx, 0x088C); // imm=0 placeholder (beqz.n a8, +2)
    return pos;
}

static void patch_beqz_n_a8_at(uint8_t* buf, uint32_t br_pos, int32_t delta_bytes) {
    int32_t imm = delta_bytes - 2;
    if (imm < 0 || imm > 31) {
        ESP_LOGE(TAG, "patch_beqz_n_a8_at: delta=%d imm=%d OUT OF RANGE! br_pos=%u",
                 (int)delta_bytes, (int)imm, (unsigned)br_pos);
        return;
    }
    uint16_t ins = (uint16_t)(0x088C | ((imm & 0xF) << 12) | (imm & 0x10));
    store_u16_exec(buf, br_pos, ins);
}

// BEQZ a8, target (3-byte, wider range) -- patchable
// Derived from objdump for far branches:
//   beqz a8, L => word 0x195816 => bytes 16 58 19
// Displacement unit appears to be 16 bytes, relative to pc_after (3-byte insn).
static uint32_t emit_beqz_a8_placeholder(XtensaJitContext* ctx) {
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u8(ctx, 0x16);
    emit_u8(ctx, 0x58);
    emit_u8(ctx, 0x00);
    return pos;
}

static void patch_beqz_a8_at(uint8_t* buf, uint32_t br_pos, int32_t delta_bytes) {
    // pc_after = br_pos + 3
    // imm8 = floor(delta/16)
    if (delta_bytes < 0) return;
    uint32_t imm = (uint32_t)delta_bytes >> 4;
    if (imm > 0xFFu) return;
    // NOTE: IRAM does not support byte writes - use word RMW via store_u8_exec
    store_u8_exec(buf, br_pos + 2, (uint8_t)imm);
}

// BNEZ.N a8, target (2-byte) -- patchable (same imm encoding as BEQZ.N, different base opcode)
static uint32_t emit_bnez_n_a8_placeholder(XtensaJitContext* ctx) {
    uint32_t pos = (uint32_t)ctx->offset;
    emit_u16(ctx, 0x08CC); // imm=0 placeholder
    return pos;
}

static void patch_bnez_n_a8_at(uint8_t* buf, uint32_t br_pos, int32_t delta_bytes) {
    int32_t imm = delta_bytes - 2;
    if (imm < 0 || imm > 31) return;
    uint16_t ins = (uint16_t)(0x08CC | ((imm & 0xF) << 12) | (imm & 0x10));
    store_u16_exec(buf, br_pos, ins);
}

// ADDI aR, aS, imm8s (add immediate -128..127)
static void emit_addi(XtensaJitContext* ctx, uint8_t ar, uint8_t as, int8_t imm) {
    // Format verified by objdump:
    //   addi a8, a8, 100 => 64 c8 82 (bytes in memory order)
    //   addi a8, a9, 50  => 32 c9 82
    // 
    // Encoding (24-bit, little-endian):
    //   byte0 = (ar << 4) | 0x02   (ar in bits [7:4], opcode=2 in bits [3:0])
    //   byte1 = (op1 << 4) | as    (op1=0xC in bits [7:4], as in bits [3:0])
    //   byte2 = imm                (signed 8-bit immediate)
    //
    // As 24-bit word: ((imm & 0xFF) << 16) | (0xC0 << 8) | ((as & 0xF) << 8) | ((ar & 0xF) << 4) | 0x02
    //               = ((imm & 0xFF) << 16) | ((0xC0 | (as & 0xF)) << 8) | ((ar & 0xF) << 4) | 0x02
    uint32_t instr = (((uint8_t)imm) << 16) | ((0xC0 | (as & 0xF)) << 8) | ((ar & 0xF) << 4) | 0x02;
    emit_u24(ctx, instr);
}

// ENTRY aS, framesize (windowed call entry)
// framesize is in units of 8 bytes (0-4095, representing 0-32760 bytes)
static void emit_entry(XtensaJitContext* ctx, uint8_t as, uint16_t framesize_bytes) {
    // Verified by objdump: entry a1,64 => bytes 36 81 00
    // For ESP32: entry a1, N encodes N directly (not N/8) in a compact form.
    // We implement only entry a1,64 for now (enough to bootstrap).
    if (as == 1 && framesize_bytes == 64) {
        emit_u8(ctx, 0x36);
        emit_u8(ctx, 0x81);
        emit_u8(ctx, 0x00);
        return;
    }
    ctx->error = true;
}

// RETW (windowed return)
static void emit_retw(XtensaJitContext* ctx) {
    // Verified by objdump: retw.n => bytes 1D F0
    emit_u16(ctx, 0xF01D);
}

// CALL8 offset (windowed call, offset in words, PC-relative)
static void emit_call8(XtensaJitContext* ctx, int32_t offset_bytes) {
    // Format: CALL8 = 0x000015 | ((offset_words & 0x3FFFF) << 6)
    int32_t offset_words = offset_bytes / 4;
    uint32_t instr = 0x000015 | (((uint32_t)offset_words & 0x3FFFF) << 6);
    emit_u24(ctx, instr);
}

// L32R a8, <literal_back>
// Verified by objdump:
//   NOPs=0  : ffff81  (bytes 81 FF FF)
//   NOPs=1  : fffe81  (bytes 81 FE FF)
// Encoding:
//   byte0 encodes target reg (we use a8 only => 0x81)
//   bytes1..2 are signed 16-bit offset_words (little-endian) relative to base = ((PC+3) & ~3)
static void emit_l32r_a8_back_to(XtensaJitContext* ctx, uintptr_t pc_abs, uintptr_t lit_abs) {
    // PC here is address of first byte of l32r
    uintptr_t base = (pc_abs + 3u) & ~3u;
    int32_t off_bytes = (int32_t)((intptr_t)lit_abs - (intptr_t)base);
    if ((off_bytes % 4) != 0) {
        ESP_LOGE(TAG, "l32r: off_bytes not aligned: pc=%p base=%p lit=%p off_bytes=%d", (void*)pc_abs, (void*)base, (void*)lit_abs, (int)off_bytes);
        ctx->error = true;
        return;
    }
    int32_t off_words = off_bytes / 4;
    if (off_words < -32768 || off_words > 32767) {
        ESP_LOGE(TAG, "l32r: off_words out of range: pc=%p base=%p lit=%p off_words=%d", (void*)pc_abs, (void*)base, (void*)lit_abs, (int)off_words);
        ctx->error = true;
        return;
    }

    emit_u8(ctx, 0x81);
    emit_u8(ctx, (uint8_t)(off_words & 0xFF));
    emit_u8(ctx, (uint8_t)((off_words >> 8) & 0xFF));
}

// CALLX8 a8
// Verified by objdump (.text bytes) for callx8 a8: 0008e0 => bytes E0 08 00
static void emit_callx8_a8(XtensaJitContext* ctx) {
    emit_u8(ctx, 0xE0);
    emit_u8(ctx, 0x08);
    emit_u8(ctx, 0x00);
}

// ===== Literal pool manager (backward) =====
#define XTENSA_LIT_MAX 64

typedef struct {
    uint32_t value;
    uint32_t offset; // offset within code buffer
} XtensaLitEntry;

typedef struct XtensaLiteralPool {
    XtensaLitEntry entries[XTENSA_LIT_MAX];
    uint32_t count;
    bool has_pool; // whether we have emitted at least one pool
} XtensaLiteralPool;

static int lit_find(const XtensaLiteralPool* pool, uint32_t value) {
    for (uint32_t i = 0; i < pool->count; i++) {
        if (pool->entries[i].value == value) return (int)i;
    }
    return -1;
}

static int lit_add(XtensaLiteralPool* pool, uint32_t value) {
    if (pool->count >= XTENSA_LIT_MAX) return -1;
    pool->entries[pool->count] = (XtensaLitEntry){ .value = value, .offset = 0xFFFFFFFFu };
    return (int)(pool->count++);
}

// Emit unconditional jump (j) with a 16-bit immediate.
// Encoding verified by objdump:
//   j +4  => 0000c6 (bytes C6 00 00)
//   j +8  => 0001c6 (bytes C6 01 00)
//   j +12 => 0002c6 (bytes C6 02 00)
// Immediate encoding is in 4-byte units and is relative to the address *after* the 3-byte j instruction:
//   imm16 = (delta_bytes/4) - 1
// where delta_bytes = target - (pc_after_j)
static int32_t floor_div4(int32_t x) {
    // floor(x/4) for signed x (C division truncates toward 0).
    if (x >= 0) return x >> 2;
    int32_t ax = -x;
    return -((ax + 3) >> 2);
}

static int32_t compute_j_imm18_from_jpos(uint32_t j_pos, uint32_t target) {
    // Xtensa 'j' instruction encoding:
    //   target = PC + 4 + sign_extend(imm18)
    // where PC is the address of the 'j' instruction (NOT aligned!)
    // and imm18 is a RAW BYTE OFFSET.
    //
    // IMPORTANT: The formula does NOT mask PC to alignment!
    // This was verified by objdump testing:
    //   - j at PC=0 with imm=19 jumps to target=23 (0 + 4 + 19 = 23) ✓
    //   - j at PC=13 with imm=20 jumps to target=37 (13 + 4 + 20 = 37) ✓
    //
    // The 18-bit immediate is encoded as:
    //   byte0[7:6] = imm[1:0]
    //   byte1[7:0] = imm[9:2]
    //   byte2[7:0] = imm[17:10]
    
    int32_t imm = (int32_t)target - (int32_t)(j_pos + 4u);
    return imm;  // Return raw byte offset
}

static void emit_j_imm18(XtensaJitContext* ctx, int32_t imm18) {
    // Encode Xtensa 'j' instruction with 18-bit signed immediate (raw byte offset).
    //
    // Instruction format (3 bytes, little-endian):
    //   byte0 = 0x06 | (imm[1:0] << 6)
    //   byte1 = imm[9:2]
    //   byte2 = imm[17:10]
    //
    // Range check: imm18 is signed 18-bit, so -131072 to +131071
    if (imm18 < -131072 || imm18 > 131071) { 
        ESP_LOGE(TAG, "emit_j_imm18: imm out of 18-bit range: %d", (int)imm18);
        ctx->error = true; 
        return; 
    }
    
    uint32_t uimm = (uint32_t)imm18 & 0x3FFFFu;  // 18 bits
    uint8_t byte0 = 0x06u | (uint8_t)((uimm & 0x3u) << 6);       // opcode | imm[1:0]
    uint8_t byte1 = (uint8_t)((uimm >> 2) & 0xFFu);              // imm[9:2]
    uint8_t byte2 = (uint8_t)((uimm >> 10) & 0xFFu);             // imm[17:10]
    
    JIT_LOGI(TAG, "[j] off=%u imm=%d -> bytes %02X %02X %02X", 
             (unsigned)ctx->offset, (int)imm18, byte0, byte1, byte2);
    
    emit_u8(ctx, byte0);
    emit_u8(ctx, byte1);
    emit_u8(ctx, byte2);
}

static void emit_j_to_target(XtensaJitContext* ctx, uint32_t target) {
    uint32_t j_pos = (uint32_t)ctx->offset;
    int32_t imm = compute_j_imm18_from_jpos(j_pos, target);
    emit_j_imm18(ctx, imm);
}

static void emit_j_rel_bytes(XtensaJitContext* ctx, int32_t delta_bytes) {
    // Backward-compat helper: compute target from current position.
    uint32_t j_pos = (uint32_t)ctx->offset;
    uint32_t after_j = j_pos + 3u;
    uint32_t target = (uint32_t)((int32_t)after_j + delta_bytes);
    emit_j_to_target(ctx, target);
}

// Emit unconditional jump (j) forward by byte offset.
// Kept for literal-pool skipping code.
static void emit_j_fwd_bytes(XtensaJitContext* ctx, uint32_t bytes) {
    // We want to skip 'bytes' forward from the END of the j instruction (j_pos + 3),
    // so target = j_pos + 3 + bytes.
    //
    // IMPORTANT: We require that (j_pos + 3 + bytes) is 4-byte aligned (caller's responsibility).
    
    uint32_t j_pos = (uint32_t)ctx->offset;
    uint32_t target = j_pos + 3u + bytes;
    
    // Verify target is 4-byte aligned
    if ((target & 3u) != 0) {
        ESP_LOGE(TAG, "emit_j_fwd_bytes: target not 4-aligned! j_pos=%u bytes=%u target=%u",
                 (unsigned)j_pos, (unsigned)bytes, (unsigned)target);
        ctx->error = true;
        return;
    }
    
    // Use the unified imm calculation and encoding
    emit_j_to_target(ctx, target);
}

// Emit a patchable j placeholder (delta_bytes computed later). Returns patch position.
static uint32_t emit_j_placeholder(XtensaJitContext* ctx) {
    uint32_t pos = (uint32_t)ctx->offset;
    // Emit j with imm18=0 as placeholder (opcode 0x06, imm[1:0]=0 -> byte0=0x06)
    emit_u8(ctx, 0x06);
    emit_u8(ctx, 0x00);
    emit_u8(ctx, 0x00);
    return pos;
}

static void patch_j_at(uint8_t* buf, uint32_t j_pos, int32_t delta_bytes) {
    uint32_t target = (uint32_t)((int32_t)(j_pos + 3u) + delta_bytes);
    
    // Log alignment issue but don't abort - j can jump to any address
    if ((target & 3u) != 0) {
        JIT_LOGW(TAG, "patch_j_at: target not 4-byte aligned (ok for j): j_pos=%u delta=%d target=%u", 
                 (unsigned)j_pos, (int)delta_bytes, (unsigned)target);
    }
    
    int32_t imm = compute_j_imm18_from_jpos(j_pos, target);
    
    // Range check
    if (imm < -131072 || imm > 131071) {
        ESP_LOGE(TAG, "patch_j_at: imm out of 18-bit range: %d", (int)imm);
        return;
    }
    
    // Encode imm18 into 3 bytes:
    //   byte0 = 0x06 | (imm[1:0] << 6)
    //   byte1 = imm[9:2]
    //   byte2 = imm[17:10]
    uint32_t uimm = (uint32_t)imm & 0x3FFFFu;
    uint8_t byte0 = 0x06u | (uint8_t)((uimm & 0x3u) << 6);
    uint8_t byte1 = (uint8_t)((uimm >> 2) & 0xFFu);
    uint8_t byte2 = (uint8_t)((uimm >> 10) & 0xFFu);
    
    store_u8_exec(buf, j_pos + 0, byte0);
    store_u8_exec(buf, j_pos + 1, byte1);
    store_u8_exec(buf, j_pos + 2, byte2);
}

static void flush_literal_pool(XtensaJitContext* ctx, XtensaLiteralPool* pool) {
    JIT_LOGI(TAG, "[litpool] flush at off=%u", (unsigned)ctx->offset);
    // Emit only literals with offset unset.
    // IMPORTANT: literal pool is data and must NOT be executed.
    // We therefore emit a forward jump over the pool, then emit the pool bytes.

    // Compute how many bytes will be emitted.
    uint32_t new_count = 0;
    for (uint32_t i = 0; i < pool->count; i++) {
        if (pool->entries[i].offset == 0xFFFFFFFFu) new_count++;
    }
    if (new_count == 0) { pool->has_pool = true; return; }

    // Strategy: we want j to land on a 4-byte aligned address for clean code generation.
    // 
    // Layout after flush:
    //   [j instruction, 3 bytes]
    //   [pre-align padding, 0-3 bytes to align to 4]
    //   [literals, new_count * 4 bytes, 4-byte aligned]
    //   [post-align padding, 0-3 bytes to make total skip multiple of 4 AND land on 4-byte aligned]
    //   <- j lands here, this should be 4-byte aligned
    //
    // j encoding: j skips (skip_bytes) where skip_bytes must be multiple of 4.
    // j lands at: j_pos + 3 + skip_bytes
    // We want (j_pos + 3 + skip_bytes) % 4 == 0
    //
    // Let's compute:
    //   j_pos = ctx->offset (current)
    //   after_j = j_pos + 3
    //   pre_align = bytes to align after_j to 4
    //   literals_bytes = new_count * 4 (always multiple of 4)
    //   
    // After pre_align + literals, offset = after_j + pre_align + literals_bytes
    // This is 4-byte aligned because (after_j + pre_align) is 4-byte aligned and literals_bytes is multiple of 4.
    //
    // So if we set skip_bytes = pre_align + literals_bytes, then:
    //   landing = j_pos + 3 + skip_bytes = after_j + pre_align + literals_bytes
    // This is 4-byte aligned!
    //
    // But wait, skip_bytes must be multiple of 4 for j encoding.
    // pre_align can be 0,1,2,3, so skip_bytes = pre_align + literals_bytes may not be multiple of 4.
    //
    // Solution: add post_pad to make skip_bytes multiple of 4.
    //   skip_bytes = pre_align + literals_bytes + post_pad, where post_pad makes it multiple of 4.
    //   post_pad = (4 - ((pre_align + literals_bytes) % 4)) % 4
    //
    // Then landing = after_j + skip_bytes = after_j + pre_align + literals_bytes + post_pad
    // Is this 4-byte aligned?
    //   after_j % 4 can be anything (0,1,2,3)
    //   (after_j + pre_align) % 4 == 0 (by definition of pre_align)
    //   (after_j + pre_align + literals_bytes) % 4 == 0 (literals_bytes is multiple of 4)
    //   (after_j + pre_align + literals_bytes + post_pad) % 4 == post_pad % 4
    //
    // So landing is 4-byte aligned only if post_pad % 4 == 0, i.e., post_pad == 0.
    // But post_pad can be 0,1,2,3.
    //
    // NEW APPROACH: Always ensure skip_bytes is such that landing is 4-byte aligned.
    //   landing = j_pos + 3 + skip_bytes
    //   We want landing % 4 == 0.
    //   Minimum content = pre_align + literals_bytes
    //   We need skip_bytes >= minimum content AND skip_bytes % 4 == 0 AND (j_pos + 3 + skip_bytes) % 4 == 0.
    //
    // Since (j_pos + 3 + skip_bytes) % 4 == 0 and skip_bytes % 4 == 0:
    //   (j_pos + 3) % 4 == 0 is required for both conditions to be satisfied.
    //
    // But (j_pos + 3) % 4 can be anything! So we need to add extra padding.
    //
    // SIMPLEST FIX: Align ctx->offset to 4 BEFORE emitting j, so j_pos % 4 == 0.
    // Then after_j = j_pos + 3, after_j % 4 == 3.
    // pre_align = 1 (to get to 4-byte boundary).
    // skip_bytes = 1 + literals_bytes, which is NOT multiple of 4 unless literals_bytes % 4 == 3.
    // literals_bytes = new_count * 4, so literals_bytes % 4 == 0.
    // So skip_bytes = 1 + 0 = 1 (mod 4), not valid.
    //
    // OK let's try a different approach: pad to make skip_bytes work, then set ctx->offset to landing.
    
    // Current position
    uint32_t j_pos = (uint32_t)ctx->offset;
    
    // Literals
    uint32_t literals_bytes = new_count * 4u;
    
    // We need:
    // 1) skip_bytes >= content_bytes
    // 2) skip_bytes % 4 == 0 (for j encoding)
    // 3) (after_j + skip_bytes) % 4 == 0 (so landing is 4-byte aligned)
    //
    // Condition 3: (after_j + skip_bytes) % 4 == 0
    //   => skip_bytes % 4 == (4 - (after_j % 4)) % 4 == (4 - (after_j & 3)) & 3
    //
    // But condition 2 requires skip_bytes % 4 == 0.
    // So we need (4 - (after_j & 3)) & 3 == 0, i.e., after_j % 4 == 0.
    //
    // after_j = j_pos + 3. For after_j % 4 == 0, we need j_pos % 4 == 1.
    //
    // If j_pos % 4 != 1, conditions 2 and 3 cannot both be satisfied with same skip_bytes.
    //
    // SOLUTION: Add pre-j padding to make j_pos % 4 == 1, so after_j % 4 == 0.
    // Then skip_bytes % 4 == 0 automatically satisfies condition 3.
    
    // Compute pre-j padding to align j_pos to (j_pos % 4 == 1)
    uint32_t pre_j_pad = (1u - (j_pos & 3u)) & 3u;  // 0,1,2,3 to reach j_pos % 4 == 1
    
    // Adjusted positions
    uint32_t adj_j_pos = j_pos + pre_j_pad;
    uint32_t adj_after_j = adj_j_pos + 3u;  // adj_after_j % 4 == 0
    
    // Pre-align after adjusted j: since adj_after_j % 4 == 0, no pre-align needed
    uint32_t adj_pre_align = 0u;
    
    // Content bytes with adjusted pre-align
    uint32_t adj_content_bytes = adj_pre_align + literals_bytes;
    
    // skip_bytes must be >= adj_content_bytes AND multiple of 4
    uint32_t skip_bytes = (adj_content_bytes + 3u) & ~3u;
    if (skip_bytes < 4u) skip_bytes = 4u;
    
    // Post-padding = skip_bytes - adj_content_bytes
    uint32_t post_pad = skip_bytes - adj_content_bytes;
    
    // Landing position (should be 4-byte aligned)
    uint32_t landing = adj_after_j + skip_bytes;
    
    JIT_LOGI(TAG, "[litpool] new=%u pre_j=%u literals=%u post_pad=%u skip=%u landing=%u (land%%4=%u)", 
             (unsigned)new_count, (unsigned)pre_j_pad, (unsigned)literals_bytes, 
             (unsigned)post_pad, (unsigned)skip_bytes, (unsigned)landing, (unsigned)(landing & 3u));

    // Emit pre-j padding (NOPs) to align j_pos so that (j_pos + 3) % 4 == 0
    // IMPORTANT: Must use real NOP instructions, not zero bytes (0x00 is illegal instruction on Xtensa)
    // We need adj_after_j = adj_j_pos + 3 to be 4-byte aligned.
    // Loop until we reach this condition.
    {
        uint32_t cur_pos = (uint32_t)ctx->offset;
        uint32_t after_j_if_emit_now = cur_pos + 3u;
        
        // Keep emitting NOPs until (cur_pos + 3) % 4 == 0
        while ((after_j_if_emit_now & 3u) != 0 && !ctx->error) {
            // Choose NOP size based on how many bytes we need
            uint32_t need = (4u - (after_j_if_emit_now & 3u)) & 3u;
            if (need == 0) break;
            
            if (need >= 3 || need == 1) {
                // For need=1, we can't emit 1-byte NOP. Emit 3-byte NOP which will overshoot.
                // For need=3, emit 3-byte NOP exactly.
                emit_nop3(ctx);
            } else if (need == 2) {
                emit_nop_n(ctx);  // 2-byte NOP
            }
            
            cur_pos = (uint32_t)ctx->offset;
            after_j_if_emit_now = cur_pos + 3u;
        }
        
        // Update adjusted values
        adj_j_pos = cur_pos;
        adj_after_j = after_j_if_emit_now;
        
        // Recalculate skip_bytes and landing with updated positions
        adj_content_bytes = literals_bytes;  // no pre_align needed since adj_after_j % 4 == 0
        skip_bytes = (adj_content_bytes + 3u) & ~3u;
        if (skip_bytes < 4u) skip_bytes = 4u;
        post_pad = skip_bytes - adj_content_bytes;
        landing = adj_after_j + skip_bytes;
        
        JIT_LOGI(TAG, "[litpool] FINAL: j_pos=%u after_j=%u skip=%u landing=%u",
                 (unsigned)adj_j_pos, (unsigned)adj_after_j, (unsigned)skip_bytes, (unsigned)landing);
    }

    // Emit j that skips skip_bytes
    emit_j_fwd_bytes(ctx, skip_bytes);
    
    // CRITICAL: Flush word buffer after j instruction!
    // Without this, the j bytes may not be written to memory before we start
    // writing literals, causing corruption.
    emit_flush_words(ctx);

    // No pre-align needed after j since adj_after_j % 4 == 0

    // Emit literals (each 4 bytes)
    for (uint32_t i = 0; i < pool->count; i++) {
        if (pool->entries[i].offset != 0xFFFFFFFFu) continue;
        pool->entries[i].offset = (uint32_t)ctx->offset;
        uint32_t v = pool->entries[i].value;
        emit_u8(ctx, (uint8_t)(v & 0xFF));
        emit_u8(ctx, (uint8_t)((v >> 8) & 0xFF));
        emit_u8(ctx, (uint8_t)((v >> 16) & 0xFF));
        emit_u8(ctx, (uint8_t)((v >> 24) & 0xFF));
    }
    
    // Flush after literals too
    emit_flush_words(ctx);

    // Emit post-padding to reach landing point.
    // IMPORTANT: Use NOP instructions instead of 0x00 bytes!
    // On Xtensa, 0x00 is an illegal instruction opcode. Even though this padding
    // is inside the literal pool (data) and should never be executed, using valid
    // NOP opcodes is safer for debugging and in case of control flow bugs.
    // 
    // We emit NOP.N (2 bytes: 0x3D 0xF0) or fill with 0xF0 0x20 0x00 (3-byte NOP)
    // as needed to reach the landing point.
    {
        uint32_t pad_remaining = post_pad;
        while (pad_remaining > 0) {
            if (pad_remaining >= 3) {
                // Emit 3-byte NOP: F0 20 00
                emit_u8(ctx, 0xF0);
                emit_u8(ctx, 0x20);
                emit_u8(ctx, 0x00);
                pad_remaining -= 3;
            } else if (pad_remaining >= 2) {
                // Emit 2-byte NOP.N: 3D F0
                emit_u8(ctx, 0x3D);
                emit_u8(ctx, 0xF0);
                pad_remaining -= 2;
            } else {
                // 1 byte remaining - can't emit a valid 1-byte instruction on Xtensa.
                // This should not happen if our calculations are correct.
                // Use 0x00 as a fallback (will crash if executed, but this is a bug).
                ESP_LOGE(TAG, "[litpool] BUG: 1-byte post_pad remaining!");
                emit_u8(ctx, 0x00);
                pad_remaining -= 1;
            }
        }
    }

    // Verify we're at landing
    if (ctx->offset != landing && !ctx->error) {
        ESP_LOGE(TAG, "[litpool] offset mismatch: expected %u, got %u", (unsigned)landing, (unsigned)ctx->offset);
        ctx->error = true;
    }

    // ИСПРАВЛЕНИЕ: НЕ обновляем bc_to_native здесь!
    // bc_map должен указывать на НАЧАЛО опкода, а не на середину.
    // Если обновлять bc_map при каждом flush, то переходы к этому опкоду
    // будут приземляться на середину кода, пропуская начальные инструкции.
    // 
    // Переходы (BR, BR_IF) патчатся в КОНЦЕ компиляции функции и используют
    // bc_map для определения целевого адреса. Если bc_map указывает на середину
    // опкода, то часть кода будет пропущена.
    //
    // Старый код (УДАЛЁН):
    // if (ctx->bc_to_native && ctx->current_bc_off <= ctx->code_size) {
    //     ctx->bc_to_native[ctx->current_bc_off] = (uint32_t)ctx->offset;
    // }

    pool->has_pool = true;
}

static void emit_load_u32_to_a8(XtensaJitContext* ctx, XtensaLiteralPool* pool, uint32_t value) {
    int idx = lit_find(pool, value);
    if (idx < 0) {
        idx = lit_add(pool, value);
        if (idx < 0) {
            // Literal pool overflow (XTENSA_LIT_MAX). Flush pending literals and start a fresh pool.
            // Multiple pools are supported because l32r is always used backward.
            JIT_LOGW(TAG, "[lit] pool full (count=%u). flushing and starting new pool", (unsigned)pool->count);
            flush_literal_pool(ctx, pool);

            // Reset pool state for the next batch of literals.
            // Keep has_pool=true so flush logic remains consistent.
            memset(pool->entries, 0, sizeof(pool->entries));
            pool->count = 0;
            pool->has_pool = true;

            idx = lit_add(pool, value);
        }

        JIT_LOGI(TAG, "[lit] Added new literal idx=%d value=0x%08X at pool->count=%u", 
                 idx, (unsigned)value, (unsigned)pool->count);
    }
    if (idx < 0) { ctx->error = true; return; }

    // Ensure this literal is emitted BEFORE we emit l32r (backward)
    if (pool->entries[idx].offset == 0xFFFFFFFFu) {
        JIT_LOGI(TAG, "[lit] Flushing pool for literal idx=%d value=0x%08X", idx, (unsigned)value);
        flush_literal_pool(ctx, pool);
        JIT_LOGI(TAG, "[lit] After flush: literal idx=%d offset=%u", idx, (unsigned)pool->entries[idx].offset);
    }

    uintptr_t pc_abs = (uintptr_t)(ctx->buffer + ctx->offset);
    uintptr_t lit_abs = (uintptr_t)(ctx->buffer + pool->entries[idx].offset);

    JIT_LOGI(TAG, "[L32R] idx=%d value=0x%08X pc_off=%u lit_off=%u pc_abs=0x%08X lit_abs=0x%08X", 
             idx, (unsigned)value, (unsigned)ctx->offset, (unsigned)pool->entries[idx].offset,
             (unsigned)pc_abs, (unsigned)lit_abs);
    
    emit_l32r_a8_back_to(ctx, pc_abs, lit_abs);
}

static void emit_jump_to_target(XtensaJitContext* ctx, XtensaLiteralPool* lp, uint32_t target_off, bool prefer_abs) {
    (void)lp;
    (void)prefer_abs;
    // With per-instruction alignment, target_off is 4B-aligned.
    emit_j_to_target(ctx, target_off);
}

// ===== High-level: Call C helper =====
// CALL8 rel (PC-relative)
// We will prefer call8 when the offset fits, to avoid callx8 issues.
static void emit_call8_rel(XtensaJitContext* ctx, uintptr_t pc_abs, uintptr_t target_abs) {
    // TODO: fill after objdump probe
    (void)pc_abs; (void)target_abs;
    ctx->error = true;
}

static void emit_call_helper(XtensaJitContext* ctx, XtensaLiteralPool* pool, void* helper_func) {
    uintptr_t pc_abs = (uintptr_t)(ctx->buffer + ctx->offset);
    uintptr_t tgt_abs = (uintptr_t)helper_func;
    emit_call8_rel(ctx, pc_abs, tgt_abs);
    if (!ctx->error) return;

    // Fallback: indirect callx8
    ctx->error = false;
    emit_load_u32_to_a8(ctx, pool, (uint32_t)(uintptr_t)helper_func);
    emit_callx8_a8(ctx);
}

// ===== Helper: Sync code cache =====
static void xtensa_sync_code(void* code, size_t size) {
    esp_cache_msync(code, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

// ===== High-level helpers =====

// Load 32-bit immediate (using L32R literal pool if needed)
static void emit_load_imm32(XtensaJitContext* ctx, XtensaLiteralPool* pool, uint8_t ar, uint32_t imm32) {
    // Avoid uncalibrated 24-bit MOVI encoding: use movi.n for 0..15, otherwise literal pool.
    if (imm32 <= 15) {
        emit_movi_n(ctx, ar, (int8_t)imm32);
        return;
    }

    // Load imm32 into a8 via literal pool, then mov.n to target reg.
    emit_load_u32_to_a8(ctx, pool, imm32);
    if (ar != 8) {
        emit_mov_n(ctx, ar, 8);
    }
}

// Load v_regs[rd] into register ar
static void emit_load_vreg(XtensaJitContext* ctx, uint8_t ar, uint8_t rd) {
    // IMPORTANT: In our call path, arguments are observed in a10/a11 on entry
    // (see crash dump: A10=instance, A11=v_regs). a3 may be used as call target.
    uint16_t offset = rd * 8;
    emit_l32i(ctx, ar, 11, offset);  // a11 = v_regs base
}

// Store register ar into v_regs[rd]
static void emit_store_vreg(XtensaJitContext* ctx, uint8_t ar, uint8_t rd) {
    uint16_t offset = rd * 8;
    emit_s32i(ctx, ar, 11, offset);  // a11 = v_regs base
}

// ===== Debug helpers (opcode profiler) =====
#if ESPB_JIT_DUMP_USED_OPCODES
static uint32_t espb_jit_xtensa_debug_op_len(uint8_t o, const uint8_t* p, const uint8_t* e) {
    (void)p; (void)e;
    switch (o) {
        case 0x02: return 1 + 2;           // BR rel16
        case 0x03: return 1 + 1 + 2;       // BR_IF cond(u8) rel16
        case 0x04: {                       // BR_TABLE Ridx(u8), num_targets(u16), [targets:i16*num], default(i16)
            // Variable length: 1 + 1 + 2 + num_targets*2 + 2
            if (p + 3 > e) return 0;       // Need at least Ridx + num_targets
            uint16_t num_targets;
            memcpy(&num_targets, p + 1, sizeof(num_targets));
            return 1 + 1 + 2 + (size_t)num_targets * 2 + 2;
        }
        case 0x0A: return 1 + 2;           // CALL local_func_idx(u16)
        case 0x0B: return 1 + 1 + 2;       // CALL_INDIRECT Rfunc(u8), type_idx(u16)
        case 0x0D: return 1 + 1 + 2;       // CALL_INDIRECT_PTR Rfunc_ptr(u8), type_idx(u16)
        case 0x0F: return 1;               // END
        case 0x10: return 1 + 2;           // MOV.I8 (rd,rs)
        case 0x11: return 1 + 2;           // MOV.I16 (rd,rs)
        case 0x12: return 1 + 2;           // MOV.I32 (rd,rs)

        case 0x13: return 1 + 2;           // MOV.I64 (rd,rs)

        case 0x90: return 1 + 2;           // TRUNC.I64.I32 (rd,rs)

        case 0x18: return 1 + 1 + 4;       // LD_IMM32 (rd,u32)


        case 0x1A: return 1 + 1 + 4;       // LDC.F32.IMM (rd,imm32)
        case 0x1C: return 1 + 1 + 4;       // LDC.PTR.IMM (rd,imm32)
        case 0x1D: return 1 + 1 + 2;       // LD_GLOBAL_ADDR (rd,u16)
        case 0x1E: return 1 + 1 + 2;       // LD_GLOBAL (rd,u16)
        case 0x1F: return 1 + 2 + 1;       // ST_GLOBAL (u16,rs)
        case 0xA5: return 1 + 1 + 1;       // FPROMOTE (rd,rs)
        case 0xAC: return 1 + 1 + 1;       // CVT.F64.I32 (rd,rs)
        case 0x60: return 1 + 3;           // ADD.F32
        case 0x61: return 1 + 3;           // SUB.F32
        case 0x62: return 1 + 3;           // MUL.F32
        case 0x63: return 1 + 3;           // DIV.F32
        case 0x64: return 1 + 3;           // MIN.F32
        case 0x65: return 1 + 3;           // MAX.F32
        case 0x66: return 1 + 2;           // ABS.F32
        case 0x67: return 1 + 2;           // SQRT.F32
        case 0x6C: return 1 + 3;           // MIN.F64
        case 0x6D: return 1 + 3;           // MAX.F64
        case 0x6E: return 1 + 2;           // ABS.F64
        case 0x6F: return 1 + 2;           // SQRT.F64
        case 0x78: return 1 + 1 + 1 + 2;   // STORE.F32 (rs,ra,off16)
        case 0x79: return 1 + 1 + 1 + 2;   // STORE.F64 (rs,ra,off16)
        case 0x7A: return 1 + 1 + 1 + 2;   // STORE.PTR (rs,ra,off16)
        case 0x86: return 1 + 1 + 1 + 2;   // LOAD.F32 (rd,ra,off16)
        case 0x87: return 1 + 1 + 1 + 2;   // LOAD.F64 (rd,ra,off16)
        case 0x88: return 1 + 1 + 1 + 2;   // LOAD.PTR (rd,ra,off16)
        case 0x89: return 1 + 1 + 1 + 2;   // LOAD.BOOL (rd,ra,off16)
        case 0x92: return 1 + 1 + 1;       // TRUNC.I32.I8 (rd,rs)
        case 0x94: return 1 + 1 + 1;       // TRUNC.I32.I8 (alias)
        case 0x93: return 1 + 1 + 1;       // TRUNC.I32.I16 (rd,rs)
        case 0x95: return 1 + 1 + 1;       // TRUNC.I16.I8 (rd,rs)
        case 0x96: return 1 + 1 + 1;       // ZEXT.I8.I16 (rd,rs)
        case 0x97: return 1 + 1 + 1;       // ZEXT.I8.I32 (rd,rs)
        case 0x98: return 1 + 1 + 1;       // ZEXT.I8.I64 (rd,rs)
        case 0x99: return 1 + 1 + 1;       // ZEXT.I16.I32 (rd,rs)
        case 0x9C: return 1 + 1 + 1;       // SEXT.I8.I16 (rd,rs)
        case 0x9D: return 1 + 1 + 1;       // SEXT.I8.I32 (rd,rs)
        case 0x9E: return 1 + 1 + 1;       // SEXT.I8.I64 (rd,rs)
        case 0x9F: return 1 + 1 + 1;       // SEXT.I16.I32 (rd,rs)
        case 0xA0: return 1 + 1 + 1;       // SEXT.I16.I64 (rd,rs)
        case 0x21: return 1;               // RET
        case 0x30: return 1 + 3;           // ADD.I64 (rd,rs1,rs2)
        case 0x33: return 1 + 3;           // DIVS.I64 (rd,rs1,rs2)
        case 0x34: return 1 + 3;           // REMS.I64 (rd,rs1,rs2)
        case 0x37: return 1 + 3;           // REMU.I64 (rd,rs1,rs2)
        case 0x38: return 1 + 3;           // AND.I64 (rd,rs1,rs2)
        case 0x39: return 1 + 3;           // OR.I64 (rd,rs1,rs2)
        case 0x3A: return 1 + 3;           // XOR.I64 (rd,rs1,rs2)
        case 0x3B: return 1 + 3;           // SHL.I64 (rd,rs1,rs2)
        case 0x3E: return 1 + 2;           // NOT.I64 (rd,rs)
        case 0x40: return 1 + 3;           // ADD.I32.IMM8 (rd,rs,imm8)
        case 0x41: return 1 + 3;           // SUB.I32.IMM8 (rd,rs,imm8)
        case 0x42: return 1 + 3;           // MUL.I32.IMM8 (rd,rs,imm8)
        case 0x43: return 1 + 3;           // DIVS.I32.IMM8 (rd,rs,imm8)
        case 0x44: return 1 + 3;           // DIVU.I32.IMM8 (rd,rs,imm8)
        case 0x45: return 1 + 3;           // REMS.I32.IMM8 (rd,rs,imm8)
        case 0x46: return 1 + 3;           // REMU.I32.IMM8 (rd,rs,imm8)
        case 0x47: return 1 + 3;           // SHRS.I32.IMM8 (rd,rs,imm8)
        case 0x48: return 1 + 3;           // SHRU.I32.IMM8 (rd,rs,imm8)
        case 0x49: return 1 + 3;           // AND.I32.IMM8 (rd,rs,imm8)
        case 0x4A: return 1 + 3;           // OR.I32.IMM8 (rd,rs,imm8)
        case 0x4B: return 1 + 3;           // XOR.I32.IMM8 (rd,rs,imm8)
        case 0x50: return 1 + 3;           // ADD.I64.IMM8 (rd,rs,imm8)
        case 0x51: return 1 + 3;           // SUB.I64.IMM8 (rd,rs,imm8)
        case 0x52: return 1 + 3;           // MUL.I64.IMM8 (rd,rs,imm8)
        case 0x53: return 1 + 3;           // DIVS.I64.IMM8 (rd,rs,imm8)
        case 0x54: return 1 + 3;           // DIVU.I64.IMM8 (rd,rs,imm8)
        case 0x55: return 1 + 3;           // REMS.I64.IMM8 (rd,rs,imm8)
        case 0x56: return 1 + 3;           // REMU.I64.IMM8 (rd,rs,imm8)
        case 0x58: return 1 + 3;           // SHRU.I64.IMM8 (rd,rs,imm8)
        case 0xC0: return 1 + 3;           // CMP group (rd,r1,r2)
        case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xC6: case 0xC7: case 0xC8: case 0xC9:
            return 1 + 3;
        case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE:
        case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            return 1 + 3;
        case 0xBD: return 1 + 2;           // INTTOPTR Rd(u8), Rs(u8)
        case 0xBE: case 0xBF: case 0xD4: case 0xD5: case 0xD6:
            return 1 + 4;                  // SELECT.* (rd,cond,true,false)
        case 0x09:
            // CALL_IMPORT is variable-length; for profiling we skip idx(u16) and let the next step resync.
            // If you need exact loop histogram for CALL_IMPORT-heavy loops, extend decoding here.
            return 1 + 2;
        default:
            return 1;
    }
}
#endif

// ===== Main compile function =====
EspbResult espb_jit_compile_function_xtensa_inline(
    EspbInstance* instance,
    uint32_t func_idx,
    const EspbFunctionBody* body,
    void** out_code,
    size_t* out_size
) {
    if (!instance || !body || !out_code || !out_size) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESPB_ERR_INVALID_OPERAND;
    }

    const EspbFuncHeader* header = &body->header;
    uint16_t num_vregs = header->num_virtual_regs;
    const uint8_t* code = body->code;
    size_t code_size = body->code_size;
    
#if ESPB_JIT_DEBUG
    ESP_LOGI(TAG, "Starting inline Xtensa JIT compilation for func_idx=%u (code_size=%zu, num_vregs=%u)", 
             func_idx, code_size, num_vregs);
#endif

#if ESPB_JIT_DUMP_USED_OPCODES
    // === Opcode profiling (compile-time) ===
    // Counts occurrences of opcodes in this function's bytecode.
    // Also tries to detect a backward-loop range (hot loop) via BR/BR_IF negative offsets.
    uint32_t opcode_hist[256] = {0};
    uint32_t loop_min = 0xFFFFFFFFu;
    uint32_t loop_max = 0;

    // Minimal length decoder for the subset of opcodes seen in fibonacci_iterative / run_performance_test.
    // Returns 1 (only opcode byte) on unknown to ensure forward progress.
    // NOTE: This is C code (no lambdas).

    {
        const uint8_t* start = code;
        const uint8_t* end = code + code_size;
        const uint8_t* p = start;
        const uint8_t* e = end;
        while (p < e) {
            uint32_t off = (uint32_t)(p - start);
            uint8_t o = *p;
            opcode_hist[o]++;

            // loop detection for backward BR/BR_IF
            if (o == 0x02 && p + 1 + 2 <= e) {
                int16_t rel = (int16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
                uint32_t tgt = (uint32_t)((int32_t)off + 1 + 2 + rel);
                if (rel < 0) {
                    if (tgt < loop_min) loop_min = tgt;
                    if (off > loop_max) loop_max = off;
                }
            } else if (o == 0x03 && p + 1 + 1 + 2 <= e) {
                int16_t rel = (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
                uint32_t tgt = (uint32_t)((int32_t)off + 1 + 1 + 2 + rel);
                if (rel < 0) {
                    if (tgt < loop_min) loop_min = tgt;
                    if (off > loop_max) loop_max = off;
                }
            }

            uint32_t len = espb_jit_xtensa_debug_op_len(o, p, e);
            if (len == 0) len = 1;
            if (p + len > e) break;
            p += len;
        }
    }

    JIT_LOGI(TAG, "[opc-prof] func_idx=%u opcode histogram (nonzero):", (unsigned)func_idx);
    for (int i = 0; i < 256; i++) {
        if (opcode_hist[i]) {
            JIT_LOGI(TAG, "[opc-prof]   op=0x%02X count=%u", i, (unsigned)opcode_hist[i]);
        }
    }
    if (loop_min != 0xFFFFFFFFu) {
        JIT_LOGI(TAG, "[opc-prof] detected backward-loop bytecode range: [%u..%u]", (unsigned)loop_min, (unsigned)loop_max);

        uint32_t loop_hist[256] = {0};
        const uint8_t* start = code;
        const uint8_t* end = code + code_size;
        const uint8_t* p = start;
        while (p < end) {
            uint32_t off = (uint32_t)(p - start);
            uint8_t o = *p;
            if (off >= loop_min && off <= loop_max) loop_hist[o]++;
            uint32_t len = espb_jit_xtensa_debug_op_len(o, p, end);
            if (len == 0) len = 1;
            if (p + len > end) break;
            p += len;
        }
        JIT_LOGI(TAG, "[opc-prof] loop-range opcode histogram (nonzero):");
        for (int i = 0; i < 256; i++) {
            if (loop_hist[i]) {
                JIT_LOGI(TAG, "[opc-prof]   loop op=0x%02X count=%u", i, (unsigned)loop_hist[i]);
            }
        }
    } else {
        JIT_LOGI(TAG, "[opc-prof] no backward loop detected");
    }
#endif

    // Bytecode dump for debugging control-flow / decoding issues.
    {
        uint32_t dump_n = (code_size < 96) ? (uint32_t)code_size : 96u;
        char line[3 * 16 + 16];
        for (uint32_t i = 0; i < dump_n; i += 16) {
            uint32_t n = (dump_n - i) < 16 ? (dump_n - i) : 16;
            int pos = snprintf(line, sizeof(line), "%03u: ", (unsigned)i);
            for (uint32_t j = 0; j < n; j++) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", code[i + j]);
            }
            JIT_LOGI(TAG, "[bc] %s", line);
        }
    }

    // Allocate JIT buffer
    // NOTE: Allocating a fixed 32KB per function quickly exhausts/fragment EXEC heap.
    // Use a conservative upper bound based on bytecode size instead.
    // Empirically, Xtensa native code is usually within ~10-20x of bytecode size,
    // plus literal pools and fixup tables.
    size_t max_size = (size_t)code_size * 24u + 4096u;
    if (max_size < 4096u) max_size = 4096u;
    if (max_size > (64u * 1024u)) max_size = (64u * 1024u);

    uint8_t* buffer = (uint8_t*)espb_exec_alloc(max_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate JIT buffer");
        return ESPB_ERR_OUT_OF_MEMORY;
    }
    
    JIT_LOGI(TAG, "Allocated JIT buffer at %p (size=%zu)", (void*)buffer, max_size);

    // Basic writable self-test (word store only; IRAM may not support byte stores)
    *(uint32_t*)buffer = 0x00000000u;

    XtensaJitContext ctx = {
        .buffer = buffer,
        .capacity = max_size,
        .offset = 0,
        .error = false,
        .word_buf = 0,
        .word_fill = 0,
        .bc_to_native = NULL,  // Will be set later after bc_to_native allocation
        .current_bc_off = 0,
        .code_size = code_size
    };

    XtensaLiteralPool litpool = {0};

    // Prologue: Windowed ABI
    emit_entry(&ctx, 1, 64);

    // Pre-seed literal pool with common helper addresses to avoid frequent flushes
    // (dedup will keep them unique). This typically reduces pool flushes to 1 per function.
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_call_import);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_ld_global_addr);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_ld_global);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_runtime_alloca);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_store_i64);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_store_i32);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_store_i16);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_store_i8);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_store_bool);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_load_i8_s);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_load_i8_u);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_load_i16_s);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_load_i16_u);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_load_bool);
    lit_add(&litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_sext_i8_i32);

    // Save incoming args (caller passes in a2/a3 under windowed ABI)
    // After CALL8 + entry, caller's outgoing a10/a11 appear as our a2/a3.
    // a2 = instance, a3 = v_regs
    // Windowed ABI uses a1+0 for outgoing stack args (7th+).
    // Reserve a1+0 for outgoing args; save locals at a1+4 and a1+8.
    emit_mov_n(&ctx, 8, 2);      // a8 = instance
    emit_s32i(&ctx, 8, 1, 4);    // [a1+4] = instance
    emit_mov_n(&ctx, 8, 3);      // a8 = v_regs
    emit_s32i(&ctx, 8, 1, 8);    // [a1+8] = v_regs

    // CRITICAL: the rest of the JIT assumes a11 == v_regs base for vreg load/store.
    // If we don't initialize it, vreg stores may go to a garbage address and crash (PIF addr error).
    emit_mov_n(&ctx, 11, 3);     // a11 = v_regs

    // Preserve callee-saved registers expected by the C caller (windowed ABI).
    // Our JIT freely uses a12-a15 (and may alias a14 via window rotation), so restore them before retw.
    // IMPORTANT: a1+16.. is used by CALL_IMPORT to build variadic arg types.
    // Keep our callee-saved spill above that region.
    emit_s32i(&ctx, 12, 1, 32);  // save a12
    emit_s32i(&ctx, 13, 1, 36);  // save a13
    emit_s32i(&ctx, 14, 1, 40);  // save a14
    emit_s32i(&ctx, 15, 1, 44);  // save a15

    // Emit initial pool now (jump+pool) so subsequent helper calls can use backward l32r without extra flushes.
    flush_literal_pool(&ctx, &litpool);

    (void)num_vregs;  // Will use later for bounds checks

    // Compile bytecode
    const uint8_t* start = code;
    const uint8_t* end = start + code_size;
    const uint8_t* pc = start;

    // Control-flow support (bytecode offset -> native offset mapping + forward-branch fixups)
    // Enough for BR / BR_IF style CFG.
    #define XTENSA_BC_UNSET 0xFFFFFFFFu
    uint32_t* bc_to_native = (uint32_t*)heap_caps_malloc((code_size + 1) * sizeof(uint32_t), MALLOC_CAP_8BIT);
    if (!bc_to_native) {
        heap_caps_free(buffer);
        return ESPB_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i <= code_size; i++) bc_to_native[i] = XTENSA_BC_UNSET;
    ctx.bc_to_native = bc_to_native;  // Set bc_to_native pointer in context after allocation

    typedef struct {
        uint32_t j_pos_native;   // where the 3-byte 'j' starts in native buffer
        uint32_t target_bc_off;  // target bytecode offset
    } XtensaBranchFixup;

    XtensaBranchFixup* fixups = (XtensaBranchFixup*)heap_caps_malloc((code_size + 1) * sizeof(XtensaBranchFixup), MALLOC_CAP_8BIT);
    uint32_t fixup_count = 0;
    if (!fixups) {
        heap_caps_free(bc_to_native);
        heap_caps_free(buffer);
        return ESPB_ERR_OUT_OF_MEMORY;
    }

    uint8_t last_op = 0x00;
    size_t last_off = 0;

    while (pc < end && !ctx.error) {
        last_op = *pc;
        last_off = (size_t)(pc - start);
        uint8_t op = *pc++;

        // NOTE: Alignment disabled for performance. Xtensa can execute unaligned 2/3-byte instructions.
        // emit_align4_with_nops(&ctx);
        // Update current_bc_off for potential bc_to_native update after literal pool
        ctx.current_bc_off = last_off;
        if (last_off <= code_size && bc_to_native[last_off] == XTENSA_BC_UNSET) {
            bc_to_native[last_off] = (uint32_t)ctx.offset;
            // Debug: log bc offset mapping (only around problem area)
            if (last_off >= 320 && last_off <= 350) {
                JIT_LOGI(TAG, "[bc_map_debug] bc=%u -> native=%u (op=0x%02X)", 
                         (unsigned)last_off, (unsigned)ctx.offset, (unsigned)op);
            }
        }

        // (runtime trace disabled)

        switch (op) {
            case 0x00: // NOP
            case 0x01: // NOP
                break;

            case 0x88: { // LOAD.PTR Rd(u8), Ra(u8), offset(i16) - INLINE (same as I32 on 32-bit)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // PTR is 4 bytes on 32-bit architecture, same as I32
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = base pointer from v_regs[ra].ptr
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                // a8 += off16
                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // Load 32-bit pointer value
                // Check alignment for optimal code path
                if ((off16 & 3) == 0) {
                    // Aligned: use l32i directly
                    emit_l32i(&ctx, 9, 8, 0);
                } else {
                    // Unaligned: use byte-by-byte load
                    emit_l8ui(&ctx, 9, 8, 0);       // a9 = byte0
                    emit_l8ui(&ctx, 10, 8, 1);      // a10 = byte1
                    emit_slli(&ctx, 10, 10, 8);
                    emit_or(&ctx, 9, 9, 10);
                    emit_l8ui(&ctx, 10, 8, 2);      // a10 = byte2
                    emit_slli(&ctx, 10, 10, 16);
                    emit_or(&ctx, 9, 9, 10);
                    emit_l8ui(&ctx, 10, 8, 3);      // a10 = byte3
                    emit_slli(&ctx, 10, 10, 24);
                    emit_or(&ctx, 9, 9, 10);
                }

                // Store result to v_regs[rd].ptr
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_PTR (value 5) to v_regs[rd].type
                emit_movi_n(&ctx, 10, 5);  // ESPB_TYPE_PTR = 5
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore v_regs to a11
                break;
            }

            case 0x92: { // TRUNC.I64.I8 Rd(u8), Rs(u8)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Use low 32 bits of v_regs[rs] to truncate to signed 8-bit
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = v_regs[rs].low32
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));

                // Truncate to 8-bit signed and sign-extend back to 32-bit
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I8 (value 7) to v_regs[rd].type
                emit_movi_n(&ctx, 10, 7);  // ESPB_TYPE_I8 = 7
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore v_regs to a11
                break;
            }

            case 0x94: { // TRUNC.I32.I8 Rd(u8), Rs(u8) - truncate to signed 8-bit
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load value from v_regs[rs].i32, truncate to 8-bit signed, store to v_regs[rd]
                // Result: rd = (int8_t)rs (sign-extended back to 32-bit for storage)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = v_regs[rs].i32
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));

                // Truncate to 8-bit signed and sign-extend back to 32-bit
                // This is done by: (val << 24) >> 24 (arithmetic shift)
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I8 (value 7) to v_regs[rd].type
                emit_movi_n(&ctx, 10, 7);  // ESPB_TYPE_I8 = 7
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore v_regs to a11
                break;
            }

            case 0x95: { // TRUNC.I16.I8 Rd(u8), Rs(u8) - truncate to signed 8-bit
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = v_regs[rs].i32 (contains low 16 bits)
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));

                // Truncate to 8-bit signed and sign-extend back to 32-bit
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I8 (value 7) to v_regs[rd].type
                emit_movi_n(&ctx, 10, 7);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x93: { // TRUNC.I32.I16 Rd(u8), Rs(u8) - truncate to signed 16-bit
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load value from v_regs[rs].i32, truncate to 16-bit signed, store to v_regs[rd]
                // Result: rd = (int16_t)rs (sign-extended back to 32-bit for storage)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = v_regs[rs].i32
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));

                // Truncate to 16-bit signed and sign-extend back to 32-bit
                // This is done by: (val << 16) >> 16 (arithmetic shift)
                emit_slli(&ctx, 8, 8, 16);
                emit_srai(&ctx, 8, 8, 16);

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I16 (value 8) to v_regs[rd].type
                emit_movi_n(&ctx, 10, 8);  // ESPB_TYPE_I16 = 8
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore v_regs to a11
                break;
            }

            case 0x0F: { // END
                // END is a terminator for the *executed* path. We emit a jump to epilogue.
                // We must keep decoding subsequent bytes to resolve forward branch targets,
                // but execution must not fall through to the next opcode!
                
                // Record this END position for later - we'll patch it to jump to epilogue
                // For now, emit a placeholder j that we'll patch after we know epilogue location
                uint32_t end_j_pos = emit_j_placeholder(&ctx);
                
                // Store in fixups array with special marker (target_bc_off = code_size means epilogue)
                fixups[fixup_count++] = (XtensaBranchFixup){ 
                    .j_pos_native = end_j_pos, 
                    .target_bc_off = code_size  // Special: means "jump to epilogue"
                };
                
                break;
            }

            case 0x03: { // BR_IF reg(u8), offset(i16) -- branch if V_I32(reg) != 0
                // DEBUG: trace branch targets (helps verify loop back-edges)

                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t cond_reg = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                uint32_t source_bc = (uint32_t)last_off;
                int32_t target_bc_signed = (int32_t)source_bc + (int32_t)off16;
                if (target_bc_signed < 0 || target_bc_signed > (int32_t)code_size) {
                    ESP_LOGE(TAG, "BR_IF: bad target bc=%d (src=%u off=%d)", target_bc_signed, (unsigned)source_bc, (int)off16);
                    ctx.error = true;
                    break;
                }
                uint32_t target_bc = (uint32_t)target_bc_signed;
                // Load condition low32 into a8
                emit_l32i(&ctx, 8, 11, (uint16_t)(cond_reg * 8));

                // Lower BR_IF using BEQZ.N (2-byte) + J:
                //   beqz.n a8, L_after   ; if cond==0 skip
                //   j <bytecode target>  ; taken when cond!=0
                // L_after:

                uint32_t beqz_pos = emit_beqz_n_a8_placeholder(&ctx);

                // Emit jump to bytecode target (immediate if known else fixup)
                if (bc_to_native[target_bc] != XTENSA_BC_UNSET) {
                    uint32_t tgt_native = bc_to_native[target_bc];
                    bool backward = (tgt_native < (uint32_t)ctx.offset);
                    emit_jump_to_target(&ctx, &litpool, tgt_native, backward);
                } else {
                    uint32_t j_pos = emit_j_placeholder(&ctx);
                    JIT_LOGW(TAG, "BR_IF fixup: bc_off=%u off16=%d -> target_bc=%u", (unsigned)source_bc, (int)off16, (unsigned)target_bc);
                    fixups[fixup_count++] = (XtensaBranchFixup){ .j_pos_native = j_pos, .target_bc_off = target_bc };
                }

                // IMPORTANT: Align to 4 bytes BEFORE computing after_pos!
                // beqz.n will jump to after_pos, so it must be properly aligned
                // for the next instruction. Without this, beqz.n may jump to an
                // unaligned address causing IllegalInstruction crash.
                emit_align4_with_nops(&ctx);
                
                uint32_t after_pos = (uint32_t)ctx.offset;

                // Flush any pending bytes before patching (RMW on EXEC memory)
                emit_flush_words(&ctx);

                // Patch local beqz -> after_pos
                {
                    uint32_t pc_after = beqz_pos + 2u;
                    int32_t delta = (int32_t)after_pos - (int32_t)pc_after;
                    
                    // Verify delta is within BEQZ.N range (0..33 bytes, imm = delta-2, 0..31)
                    if (delta < 2 || delta > 33) {
                        ESP_LOGE(TAG, "BR_IF: beqz.n delta out of range: %d (beqz_pos=%u after_pos=%u)",
                                 (int)delta, (unsigned)beqz_pos, (unsigned)after_pos);
                        ctx.error = true;
                        break;
                    }
                    
                    patch_beqz_n_a8_at(ctx.buffer, beqz_pos, delta);
                }

                break;
            }

            case 0x02: { // BR offset(i16) -- unconditional branch
                // DEBUG: trace branch targets (helps verify loop back-edges)

                if (pc + 2 > end) { ctx.error = true; break; }
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // IMPORTANT: BR offset is relative to the start of the BR instruction (opcode byte),
                // like in the interpreter and jit_riscv:
                //   source_bc = (pc_after_imm - 3)
                //   target_bc = source_bc + off16
                uint32_t source_bc = (uint32_t)last_off;
                int32_t target_bc_signed = (int32_t)source_bc + (int32_t)off16;
                if (target_bc_signed < 0 || target_bc_signed > (int32_t)code_size) {
                    ESP_LOGE(TAG, "BR: bad target bc=%d (src=%u off=%d)", target_bc_signed, (unsigned)source_bc, (int)off16);
                    ctx.error = true;
                    break;
                }
                uint32_t target_bc = (uint32_t)target_bc_signed;
                // If target already emitted -> immediate jump (possibly backward)
                if (bc_to_native[target_bc] != XTENSA_BC_UNSET) {
                    uint32_t target_native = bc_to_native[target_bc];
                    bool backward = (target_native < (uint32_t)ctx.offset);
                    emit_jump_to_target(&ctx, &litpool, target_native, backward);
                } else {
                    // Forward branch: emit placeholder and patch later
                    uint32_t j_pos = emit_j_placeholder(&ctx);
                    JIT_LOGW(TAG, "BR fixup: bc_off=%u off16=%d -> target_bc=%u", (unsigned)source_bc, (int)off16, (unsigned)target_bc);
                    fixups[fixup_count++] = (XtensaBranchFixup){ .j_pos_native = j_pos, .target_bc_off = target_bc };
                }

                // BR is a terminator in bytecode, but for linear codegen we keep scanning
                // to ensure all forward targets are emitted and fixups can be resolved.
                break;
            }

            case 0x04: { // BR_TABLE - indirect branch (switch-case)
                // Format: [0x04] [Ridx:u8] [num_targets:u16] [target_offsets:i16 * num_targets] [default_offset:i16]
                if (pc + 1 + 2 > end) {
                    ESP_LOGE(TAG, "BR_TABLE: truncated header");
                    ctx.error = true;
                    break;
                }
                uint8_t ridx = *pc++;
                uint16_t num_targets;
                memcpy(&num_targets, pc, 2);
                pc += 2;

                // Check we have enough data for targets and default
                if (pc + num_targets * 2 + 2 > end) {
                    ESP_LOGE(TAG, "BR_TABLE: truncated targets");
                    ctx.error = true;
                    break;
                }

                // Read target offsets (relative to start of this instruction)
                const uint8_t* targets_ptr = pc;
                pc += num_targets * 2;

                int16_t default_off;
                memcpy(&default_off, pc, 2);
                pc += 2;

                // BR_TABLE is a switch-like control-flow op. Keep logging minimal by default.
#if ESPB_JIT_DEBUG_OPTOCODES
                ESP_LOGI(TAG, "BR_TABLE ridx=%u num_targets=%u default_off=%d src_bc=%u",
                         ridx, num_targets, (int)default_off, (unsigned)(pc - code));
#endif

                // Load index value from v_regs[ridx] into a10 (not a8, because emit_load_imm32 uses a8 internally)
                // v_regs base is in a11, but it may have been clobbered by previous helper calls
                // Restore a11 from stack [a1+8] where v_regs was saved in prologue
                // NOTE: We cannot use a6 because it's only set before CALL_IMPORT and may be invalid
                // when BR_TABLE is reached via backward jump from a loop.
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs (restore from stack)
                
                uint16_t ridx_off = (uint16_t)(ridx * 8);
                emit_l32i(&ctx, 10, 11, ridx_off); // a10 = v_regs[ridx].lo (a11 = v_regs base)

                // IMPORTANT: In the interpreter, target_offset is applied AFTER reading the entire BR_TABLE instruction.
                // So source_bc should be the bytecode offset AFTER the BR_TABLE, which is current pc position.
                uint32_t source_bc = (uint32_t)(pc - code);

                if (num_targets == 0) {
                    // No targets, always jump to default
                    int32_t default_target_signed = (int32_t)source_bc + (int32_t)default_off;
                    uint32_t default_target = (uint32_t)default_target_signed;
                    
                    if (bc_to_native[default_target] != XTENSA_BC_UNSET) {
                        uint32_t target_native = bc_to_native[default_target];
                        bool backward = (target_native < (uint32_t)ctx.offset);
                        emit_jump_to_target(&ctx, &litpool, target_native, backward);
                    } else {
                        uint32_t j_pos = emit_j_placeholder(&ctx);
                        fixups[fixup_count++] = (XtensaBranchFixup){ .j_pos_native = j_pos, .target_bc_off = default_target };
                    }
                } else {
                    // Generate comparison chain for each case
                    // For each case i:
                    //   load i into a9
                    //   BNE a8, a9, skip  (if index != i, skip J)
                    //   J target[i]
                    // skip:
                    // ...
                    // J default

                    for (uint16_t i = 0; i < num_targets; i++) {
                        int16_t tgt_off;
                        memcpy(&tgt_off, targets_ptr + i * 2, 2);
                        int32_t target_bc_signed = (int32_t)source_bc + (int32_t)tgt_off;
                        uint32_t target_bc_off = (uint32_t)target_bc_signed;

#if ESPB_JIT_DEBUG_OPTOCODES
                        ESP_LOGI(TAG, "BR_TABLE case %u: tgt_off=%d target_bc=%u native_off=%zu",
                                 i, (int)tgt_off, target_bc_off, ctx.offset);
#endif

                        // Load case index i into a9
                        emit_load_imm32(&ctx, &litpool, 9, (uint32_t)i);
                        
                        // Copy index from a10 to a8 for comparison (emit_load_imm32 may have clobbered a8)
                        emit_mov_n(&ctx, 8, 10);

                        // BNE a8, a9, skip (skip the J instruction if not equal)
                        // emit_bcc_a8_a9_placeholder returns position, 0x9 = BNE condition
                        uint32_t bne_pos = emit_bcc_a8_a9_placeholder(&ctx, 0x9);
#if ESPB_JIT_DEBUG_OPTOCODES
                        ESP_LOGI(TAG, "BR_TABLE case %u: BNE at %u", i, bne_pos);
#endif

                        // Emit J to target[i]
                        if (bc_to_native[target_bc_off] != XTENSA_BC_UNSET) {
                            uint32_t target_native = bc_to_native[target_bc_off];
                            bool backward = (target_native < (uint32_t)ctx.offset);
#if ESPB_JIT_DEBUG_OPTOCODES
                            ESP_LOGI(TAG, "BR_TABLE case %u: J (backward=%d) to native=%u at %zu",
                                     i, backward, target_native, ctx.offset);
#endif
                            emit_jump_to_target(&ctx, &litpool, target_native, backward);
                        } else {
                            uint32_t j_pos = emit_j_placeholder(&ctx);
#if ESPB_JIT_DEBUG_OPTOCODES
                            ESP_LOGI(TAG, "BR_TABLE case %u: J placeholder at %u -> bc=%u", i, j_pos, target_bc_off);
#endif
                            fixups[fixup_count++] = (XtensaBranchFixup){ .j_pos_native = j_pos, .target_bc_off = target_bc_off };
                        }

                        // Patch BNE to skip the J instruction (skip to current position)
#if ESPB_JIT_DEBUG_OPTOCODES
                        ESP_LOGI(TAG, "BR_TABLE case %u: patch BNE at %u to skip to %zu", i, bne_pos, ctx.offset);
#endif
                        patch_bcc_a8_a9_at(ctx.buffer, bne_pos, (int32_t)ctx.offset);
                    }

                    // Fall through to default
                    int32_t default_target_signed = (int32_t)source_bc + (int32_t)default_off;
                    uint32_t default_target = (uint32_t)default_target_signed;
                    
#if ESPB_JIT_DEBUG_OPTOCODES
                    ESP_LOGI(TAG, "BR_TABLE default: target_bc=%u native_off=%zu", default_target, ctx.offset);
#endif
                    if (bc_to_native[default_target] != XTENSA_BC_UNSET) {
                        uint32_t target_native = bc_to_native[default_target];
                        bool backward = (target_native < (uint32_t)ctx.offset);
#if ESPB_JIT_DEBUG_OPTOCODES
                        ESP_LOGI(TAG, "BR_TABLE default: J (backward=%d) to native=%u", backward, target_native);
#endif
                        emit_jump_to_target(&ctx, &litpool, target_native, backward);
                    } else {
                        uint32_t j_pos = emit_j_placeholder(&ctx);
#if ESPB_JIT_DEBUG_OPTOCODES
                        ESP_LOGI(TAG, "BR_TABLE default: J placeholder at %u -> bc=%u", j_pos, default_target);
#endif
                        fixups[fixup_count++] = (XtensaBranchFixup){ .j_pos_native = j_pos, .target_bc_off = default_target };
                    }
                }
                
                // NOTE: No hex dump here. It caused crashes on real hardware and isn't useful in normal builds.
                // If you need BR_TABLE codegen diagnostics, enable ESPB_JIT_DEBUG_OPTOCODES and add targeted logs.

                break;
            }

            case 0x18: { // LDC.I32.IMM
                if (pc + 5 > end) {  // 1 byte rd + 4 bytes imm32
                    ESP_LOGE(TAG, "LDC.I32: truncated");
                    ctx.error = true;
                    break;
                }
                uint8_t rd = *pc++;
                uint32_t imm32;
                memcpy(&imm32, pc, 4);
                pc += 4;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x18 LDC.I32 imm32=%u", (unsigned)last_off, (unsigned)imm32);
                }

                JIT_LOGD(TAG, "LDC.I32 rd=%u imm32=%u", rd, imm32);

                // Load imm32 into a8, then store to v_regs[rd]
                emit_load_imm32(&ctx, &litpool, 8, imm32);
                emit_store_vreg(&ctx, 8, rd);
                break;
            }

            case 0x1C: { // LDC.PTR.IMM
                if (pc + 5 > end) {  // 1 byte rd + 4 bytes imm32
                    ESP_LOGE(TAG, "LDC.PTR: truncated");
                    ctx.error = true;
                    break;
                }
                uint8_t rd = *pc++;
                uint32_t imm32;
                memcpy(&imm32, pc, 4);
                pc += 4;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x1C LDC.PTR imm32=%u", (unsigned)last_off, (unsigned)imm32);
                }

                JIT_LOGD(TAG, "LDC.PTR rd=%u imm32=0x%08X", rd, imm32);

                // Load imm32 into a8, then store to v_regs[rd]
                emit_load_imm32(&ctx, &litpool, 8, imm32);
                emit_store_vreg(&ctx, 8, rd);
                break;
            }

            case 0x19: { // LDC.I64.IMM
                if (pc + 9 > end) {  // 1 byte rd + 8 bytes imm64
                    ESP_LOGE(TAG, "LDC.I64: truncated");
                    ctx.error = true;
                    break;
                }
                uint8_t rd = *pc++;
                uint64_t imm64;
                memcpy(&imm64, pc, 8);
                pc += 8;

                // Value is 8 bytes (union). We store imm64 as two 32-bit words.
                uint32_t lo = (uint32_t)(imm64 & 0xFFFFFFFFu);
                uint32_t hi = (uint32_t)((imm64 >> 32) & 0xFFFFFFFFu);

                uint16_t off = (uint16_t)(rd * 8);

                // store low 32
                emit_load_u32_to_a8(&ctx, &litpool, lo);
                emit_s32i(&ctx, 8, 11, off);

                // store high 32
                emit_load_u32_to_a8(&ctx, &litpool, hi);
                emit_s32i(&ctx, 8, 11, (uint16_t)(off + 4));

                break;
            }

            case 0x10: // MOV.I8 - copies low 32 bits, same as generic MOV for JIT
            case 0x11: // MOV.I16 - copies full 64-bit Value
            case 0x12: // MOV.I32 / MOV (generic)
            case 0x13: // MOV.64 - same as MOV.I32, copies full 64-bit Value
            {
                // IMPORTANT: copy the full Value (8 bytes), not only low 32 bits.
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x12 MOV rs=%u", (unsigned)last_off, (unsigned)rs);
                }

                uint16_t rs_off = (uint16_t)(rs * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // low 32
                emit_l32i(&ctx, 8, 11, rs_off);
                emit_s32i(&ctx, 8, 11, rd_off);

                // high 32
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs_off + 4));
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            // ========== Arithmetic Operations I32 (0x20-0x27) ==========
            case 0x20: // ADD.I32 Rd, Rs1, Rs2
            case 0x21: // SUB.I32 Rd, Rs1, Rs2
            case 0x22: // MUL.I32 Rd, Rs1, Rs2
            case 0x23: // DIVS.I32 Rd, Rs1, Rs2 (signed division)
            case 0x24: // REMS.I32 Rd, Rs1, Rs2 (signed remainder)
            case 0x26: // DIVU.I32 Rd, Rs1, Rs2 (unsigned division)
            case 0x27: // REMU.I32 Rd, Rs1, Rs2 (unsigned remainder)
            {
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Load rs1 into a8, rs2 into a9
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs1 * 8));   // a8 = v_regs[rs1].i32
                emit_l32i(&ctx, 9, 11, (uint16_t)(rs2 * 8));   // a9 = v_regs[rs2].i32
                
                switch (op) {
                    case 0x20: // ADD.I32: a8 = a8 + a9
                        // add.n ar, as, at: narrow format (2 bytes)
                        // Encoding: (ar << 4) | 0xA, (at << 4) | as
                        // add.n a8, a8, a9 => 0x889A (bytes: 9A 88) - but objdump shows 88 9a
                        // Actually: add.n a8, a8, a9 => bytes 88 9a in memory
                        // Format: byte0 = (as << 4) | 0xA, byte1 = (at << 4) | ar
                        // For add.n a8, a8, a9: as=8, at=9, ar=8
                        //   byte0 = (8 << 4) | 0xA = 0x8A
                        //   byte1 = (9 << 4) | 8 = 0x98
                        // But objdump shows "889a" which is bytes 9a 88? No, it's hex dump of instruction.
                        // Let me check: 889a as 16-bit LE = bytes 9A 88 in memory
                        // Verified: add.n a8, a8, a9 => emit 0x889A as u16
                        emit_u16(&ctx, 0x889A);  // add.n a8, a8, a9
                        break;
                        
                    case 0x21: // SUB.I32: a8 = a8 - a9
                        // sub ar, as, at: 3 bytes
                        // Verified by objdump: sub a8, a8, a9 => c08890
                        // As 24-bit in LE memory: bytes 90 88 C0
                        // Format: op2=0xC (sub), encoded as:
                        //   byte0 = (at << 4) | 0x00 = (9 << 4) | 0 = 0x90
                        //   byte1 = (as << 4) | ar = (8 << 4) | 8 = 0x88
                        //   byte2 = (op2 << 4) | 0x00 = (0xC << 4) | 0 = 0xC0
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar
                        emit_u8(&ctx, 0xC0);  // (op2 << 4) | 0
                        break;
                        
                    case 0x22: // MUL.I32: a8 = a8 * a9
                        // mull ar, as, at: multiply low 32 bits
                        // Verified by objdump: mull a8, a8, a9 => 828890
                        // As 24-bit in LE memory: bytes 90 88 82
                        // Format: op2=0x8 (mull), encoded as:
                        //   byte0 = (at << 4) | 0x00 = (9 << 4) | 0 = 0x90
                        //   byte1 = (as << 4) | ar = (8 << 4) | 8 = 0x88
                        //   byte2 = (op2 << 4) | 0x02 = (0x8 << 4) | 2 = 0x82
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar
                        emit_u8(&ctx, 0x82);  // (op2 << 4) | 2
                        break;
                        
                    case 0x23: // DIVS.I32 - signed division (no native Xtensa instruction)
                    case 0x24: // REMS.I32 - signed remainder
                    case 0x26: // DIVU.I32 - unsigned division
                    case 0x27: // REMU.I32 - unsigned remainder
                    {
                        // Xtensa doesn't have native division - call helper via windowed ABI
                        // Helper signature: uint32_t helper(uint32_t a, uint32_t b)
                        // Windowed ABI: callee a2, a3 <= caller a10, a11
                        
                        // Save v_regs pointer (a11) across windowed call.
                        // With CALLX8 window rotation, caller a6 becomes callee a14 (callee-saved), so it's safe.
                        emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                        
                        // Arguments already in a8, a9; move to a10, a11
                        emit_mov_n(&ctx, 10, 8);  // a10 = a8 (rs1)
                        emit_mov_n(&ctx, 11, 9);  // a11 = a9 (rs2)
                        
                        void* helper = NULL;
                        switch (op) {
                            case 0x23: helper = (void*)&jit_helper_divs32; break;
                            case 0x24: helper = (void*)&jit_helper_rems32; break;
                            case 0x26: helper = (void*)&jit_helper_divu32; break;
                            case 0x27: helper = (void*)&jit_helper_remu32; break;
                        }
                        
                        emit_call_helper(&ctx, &litpool, helper);
                        
                        // Result returned in a10, move to a8
                        emit_mov_n(&ctx, 8, 10);
                        
                        // Restore a11 back to v_regs pointer from a6
                        emit_mov_n(&ctx, 11, 6);  // a11 = v_regs
                        break;
                    }
                }
                
                // Store result to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                break;
            }

            // ========== Bitwise Operations I32 (0x28-0x2A, 0x2E) ==========
            case 0x28: // AND.I32 Rd, Rs1, Rs2
            case 0x29: // OR.I32 Rd, Rs1, Rs2
            case 0x2A: // XOR.I32 Rd, Rs1, Rs2
            {
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Load rs1 into a8, rs2 into a9
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs1 * 8));   // a8 = v_regs[rs1].i32
                emit_l32i(&ctx, 9, 11, (uint16_t)(rs2 * 8));   // a9 = v_regs[rs2].i32
                
                // Xtensa bitwise instructions (3 bytes each):
                // Format: RST with op2 field
                // Verified encoding pattern from SUB (op2=0xC): sub a8, a8, a9 => bytes 90 88 C0
                //   byte0 = (at << 4) | 0x0 = (9 << 4) | 0 = 0x90
                //   byte1 = (as << 4) | ar = (8 << 4) | 8 = 0x88
                //   byte2 = (op2 << 4) | 0x0
                // AND: op2=0x1, OR: op2=0x2, XOR: op2=0x3
                switch (op) {
                    case 0x28: // AND.I32: a8 = a8 & a9
                        // and a8, a8, a9 => bytes 90 88 10
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar
                        emit_u8(&ctx, 0x10);  // (op2 << 4) | 0 where op2=1
                        break;
                        
                    case 0x29: // OR.I32: a8 = a8 | a9
                        // or a8, a8, a9 => bytes 90 88 20
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar
                        emit_u8(&ctx, 0x20);  // (op2 << 4) | 0 where op2=2
                        break;
                        
                    case 0x2A: // XOR.I32: a8 = a8 ^ a9
                        // xor a8, a8, a9 => bytes 90 88 30
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar
                        emit_u8(&ctx, 0x30);  // (op2 << 4) | 0 where op2=3
                        break;
                }
                
                // Store result to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                break;
            }

            case 0x2E: // NOT.I32 Rd, Rs (bitwise NOT, 2 operands)
            {
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Load rs into a8
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));   // a8 = v_regs[rs].i32
                
                // Xtensa doesn't have a native NOT instruction.
                // Implement NOT as XOR with -1 (all bits set):
                // movi a9, -1
                // xor a8, a8, a9
                emit_movi(&ctx, 9, -1);  // a9 = -1 (0xFFFFFFFF)
                emit_u8(&ctx, 0x90);     // xor a8, a8, a9 => bytes 90 88 30
                emit_u8(&ctx, 0x88);
                emit_u8(&ctx, 0x30);
                
                // Store result to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                break;
            }

            // ========== Shift Operations I32 (0x2B-0x2D) ==========
            case 0x2B: // SHL.I32 Rd, Rs1, Rs2 (shift left logical)
            case 0x2C: // SHRS.I32 Rd, Rs1, Rs2 (shift right arithmetic/signed)
            case 0x2D: // SHRU.I32 Rd, Rs1, Rs2 (shift right logical/unsigned)
            {
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Load rs1 (value to shift) into a8, rs2 (shift amount) into a9
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs1 * 8));   // a8 = v_regs[rs1].i32
                emit_l32i(&ctx, 9, 11, (uint16_t)(rs2 * 8));   // a9 = v_regs[rs2].i32 (shift amount)
                
                // Xtensa variable shift uses a two-step sequence:
                //   1) set shift amount register via SSL/SSR
                //   2) execute SLL/SRA/SRL
                // Verified via objdump (see obj_test/tmp_shift_test.o):
                //   ssl a9      => 0x401900 (bytes: 00 19 40)
                //   ssr a9      => 0x400900 (bytes: 00 09 40)
                //   sll a8, a8  => 0xa18800 (bytes: 00 88 A1)
                //   sra a8, a8  => 0xb18080 (bytes: 80 80 B1)
                //   srl a8, a8  => 0x918080 (bytes: 80 80 91)
                
                switch (op) {
                    case 0x2B: // SHL.I32: a8 = a8 << a9
                        // ssl a9
                        emit_u8(&ctx, 0x00);
                        emit_u8(&ctx, 0x19);
                        emit_u8(&ctx, 0x40);
                        // sll a8, a8
                        emit_u8(&ctx, 0x00);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0xA1);
                        break;

                    case 0x2C: // SHRS.I32: a8 = (int32_t)a8 >> a9 (arithmetic)
                        // ssr a9
                        emit_u8(&ctx, 0x00);
                        emit_u8(&ctx, 0x09);
                        emit_u8(&ctx, 0x40);
                        // sra a8, a8
                        emit_u8(&ctx, 0x80);
                        emit_u8(&ctx, 0x80);
                        emit_u8(&ctx, 0xB1);
                        break;

                    case 0x2D: // SHRU.I32: a8 = (uint32_t)a8 >> a9 (logical)
                        // ssr a9
                        emit_u8(&ctx, 0x00);
                        emit_u8(&ctx, 0x09);
                        emit_u8(&ctx, 0x40);
                        // srl a8, a8
                        emit_u8(&ctx, 0x80);
                        emit_u8(&ctx, 0x80);
                        emit_u8(&ctx, 0x91);
                        break;
                }
                
                // Store result to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                break;
            }

            // ========== Extension Operations (0x97-0x9B, 0x9D, 0xA1) ==========
            case 0x98: { // ZEXT.I8.I64 Rd, Rs (zero-extend 8->64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Zero-extend 8-bit to 32-bit using EXTUI: extract 8 bits
                emit_extui(&ctx, 8, 8, 0, 8);

                // Store low32 to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Zero high32
                emit_movi_n(&ctx, 9, 0);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }

            case 0x99: { // ZEXT.I16.I32 Rd, Rs (zero-extend 16->32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));
                
                // Zero-extend 16-bit to 32-bit using EXTUI: extract 16 bits from position 0
                // EXTUI aR, aS, shift, width - extracts bits and zero-extends
                // Verified by objdump: extui a8, a8, 0, 16 => 8080f4 => bytes: 80 80 f4
                // Encoding for extui a8, a8, 0, 16:
                //   byte0 = (ar << 4) | 0x0 = 0x80
                //   byte1 = (as << 4) | (shift & 0xF) = 0x80
                //   byte2 = ((width-1) << 4) | 0x04 = 0xF4
                emit_u8(&ctx, 0x80);  // (a8 << 4) | 0
                emit_u8(&ctx, 0x80);  // (a8 << 4) | 0 (shift=0)
                emit_u8(&ctx, 0xF4);  // ((16-1) << 4) | 0x04
                
                // Store to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                
                // Clear high 32 bits (type field) - store 0 for I32 type
                // Use a9 to avoid clobbering a8 (result)
                emit_movi_n(&ctx, 9, 0);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));
                
                break;
            }

            case 0x9D: { // SEXT.I8.I32 Rd, Rs (sign-extend 8->32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Sign-extend 8-bit to 32-bit: (val << 24) >> 24
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                // Store to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Set type = ESPB_TYPE_I32 (value 1)
                emit_movi_n(&ctx, 9, 1);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }

            case 0x9C: { // SEXT.I8.I16 Rd, Rs (sign-extend 8->16)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Sign-extend 8-bit to 32-bit: (val << 24) >> 24
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                // Store to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Set type = ESPB_TYPE_I16 (value 2)
                emit_movi_n(&ctx, 9, 2);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }

            case 0x9E: { // SEXT.I8.I64 Rd, Rs (sign-extend 8->64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Sign-extend 8-bit to 32-bit: (val << 24) >> 24
                emit_slli(&ctx, 8, 8, 24);
                emit_srai(&ctx, 8, 8, 24);

                // Store low 32 bits to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Sign-extend into high 32 bits: srai a9, a8, 31
                emit_u8(&ctx, 0x80);  // (as=8 << 4) | 0
                emit_u8(&ctx, 0x9F);  // (at=9 << 4) | 0xF
                emit_u8(&ctx, 0x31);  // opcode with sa=31
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }

            case 0x9F: { // SEXT.I16.I32 Rd, Rs (sign-extend 16->32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Sign-extend 16-bit to 32-bit: (val << 16) >> 16
                emit_slli(&ctx, 8, 8, 16);
                emit_srai(&ctx, 8, 8, 16);

                // Store to v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Set type = ESPB_TYPE_I32 (value 1)
                emit_movi_n(&ctx, 9, 1);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }

            case 0xA0: { // SEXT.I16.I64 Rd, Rs (sign-extend 16->64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Load 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));

                // Sign-extend 16-bit to 32-bit: (val << 16) >> 16
                emit_slli(&ctx, 8, 8, 16);
                emit_srai(&ctx, 8, 8, 16);

                // Store low 32 bits
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                // Sign-extend into high 32 bits: srai a9, a8, 31
                emit_u8(&ctx, 0x80);  // (as=8 << 4) | 0
                emit_u8(&ctx, 0x9F);  // (at=9 << 4) | 0xF
                emit_u8(&ctx, 0x31);  // opcode with sa=31
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                break;
            }
            
            case 0x9B: { // ZEXT.I32.I64 Rd, Rs (zero-extend 32->64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Load low 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));
                
                // Store to low 32 bits of v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                
                // Zero out high 32 bits of v_regs[rd]
                // Use movi.n a8, 0 then s32i
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8 + 4));
                
                break;
            }
            
            case 0xA1: { // SEXT.I32.I64 Rd, Rs (sign-extend 32->64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Load low 32 bits from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));
                
                // Store to low 32 bits of v_regs[rd]
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                
                // Sign-extend: high 32 bits = (low32 >> 31) ? 0xFFFFFFFF : 0
                // srai a9, a8, 31 (arithmetic shift right by 31 gives all sign bits)
                // Verified by objdump: srai a9, a8, 31 => 319f80 (bytes: 80 9F 31 in memory)
                // Format: byte0 = (as << 4) | 0x00, byte1 = (at << 4) | 0x0F, byte2 = 0x31
                // For srai a9, a8, 31: as=8, at=9, sa=31
                //   byte0 = (8 << 4) | 0x00 = 0x80
                //   byte1 = (9 << 4) | 0x0F = 0x9F  
                //   byte2 = 0x31 (encodes sa=31 and opcode)
                emit_u8(&ctx, 0x80);  // (as << 4) | 0
                emit_u8(&ctx, 0x9F);  // (at << 4) | 0xF
                emit_u8(&ctx, 0x31);  // opcode with sa encoded
                
                // Store sign-extended value to high 32 bits
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));
                
                break;
            }

            // ========== LDC.F32.IMM (0x1A) ==========
            case 0x1A: { // LDC.F32.IMM Rd, imm32 (float bits)
                if (pc + 1 + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;

                // Read raw 32-bit immediate bits (little-endian). For F32 we store bits as-is.
                uint32_t imm32 = pc[0] | (pc[1] << 8) | (pc[2] << 16) | (pc[3] << 24);
                pc += 4;

                // Store to low 32 bits of v_regs[rd]
                emit_load_u32_to_a8(&ctx, &litpool, imm32);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                break;
            }

            // ========== LDC.F64.IMM (0x1B) ==========
            case 0x1B: { // LDC.F64.IMM Rd, imm64 (double constant)
                if (pc + 1 + 8 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                
                // Read 64-bit double value from bytecode (little-endian)
                uint32_t lo32 = pc[0] | (pc[1] << 8) | (pc[2] << 16) | (pc[3] << 24);
                uint32_t hi32 = pc[4] | (pc[5] << 8) | (pc[6] << 16) | (pc[7] << 24);
                pc += 8;
                
                // Load lo32 into a8 via literal pool and store to v_regs[rd].lo
                emit_load_u32_to_a8(&ctx, &litpool, lo32);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                
                // Load hi32 into a8 via literal pool and store to v_regs[rd].hi
                emit_load_u32_to_a8(&ctx, &litpool, hi32);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8 + 4));
                
                break;
            }

            // ========== F32 arithmetic (0x60-0x67) ==========
            case 0x60: // ADD.F32
            case 0x61: // SUB.F32
            case 0x62: // MUL.F32
            case 0x63: // DIV.F32
            case 0x64: // MIN.F32
            case 0x65: // MAX.F32
            {
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                void* helper = NULL;
                switch (op) {
                    case 0x60: helper = (void*)jit_helper_fadd_f32_bits; break;
                    case 0x61: helper = (void*)jit_helper_fsub_f32_bits; break;
                    case 0x62: helper = (void*)jit_helper_fmul_f32_bits; break;
                    case 0x63: helper = (void*)jit_helper_fdiv_f32_bits; break;
                    case 0x64: helper = (void*)jit_helper_fmin_f32_bits; break;
                    case 0x65: helper = (void*)jit_helper_fmax_f32_bits; break;
                    default: ctx.error = true; break;
                }
                if (!helper) { ctx.error = true; break; }

                emit_mov_n(&ctx, 6, 11); // save v_regs

                // a10 = r1 bits, a11 = r2 bits
                emit_l32i(&ctx, 10, 11, (uint16_t)(r1 * 8));
                emit_l32i(&ctx, 11, 11, (uint16_t)(r2 * 8));

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                emit_callx8_a8(&ctx);

                // store result (a10) to rd.lo and clear rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x66: // ABS.F32
            case 0x67: // SQRT.F32
            {
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;

                void* helper = (op == 0x66) ? (void*)jit_helper_fabs_f32_bits : (void*)jit_helper_fsqrt_f32_bits;

                emit_mov_n(&ctx, 6, 11);
                emit_l32i(&ctx, 10, 11, (uint16_t)(r1 * 8));

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                emit_callx8_a8(&ctx);

                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            // ========== CVT Operations (0xAC-0xB5) ==========
            case 0xA5: { // FPROMOTE Rd, Rs (F32 -> F64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw f32 bits (low32)
                emit_l32i(&ctx, 10, 11, (uint16_t)(rs * 8));

                // call helper, returns uint64 bits in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_fpromote_f32_to_f64_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] using a6 as base
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xA4: { // FPROUND Rd, Rs (F64 -> F32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10:a11 = raw f64 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));

                // call helper, returns uint32 f32 bits in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_fpround_f64_to_f32_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low32 and clear high32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xA6: { // CVT.F32.U32 Rd, Rs (F32 -> U32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw f32 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // call helper, returns uint32 u32 value in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f32_u32_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low32 and clear high32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xA7: { // CVT.F32.U64 Rd, Rs (F32 -> U64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw f32 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // call helper, returns uint64 u64 value in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f32_u64_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low/high
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xA8: { // CVT.F64.U32 Rd, Rs (F64 -> U32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10:a11 = raw f64 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));

                // call helper, returns u32 in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f64_u32);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low32 and clear high32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xA9: { // CVT.F64.U64 Rd, Rs (F64 -> U64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10:a11 = raw f64 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));

                // call helper, returns u64 in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f64_u64);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low/high
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAC: { // CVT.F64.I32 Rd, Rs (F64 -> I32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // Load raw f64 bits into a10:a11
                emit_l32i(&ctx, 10, 11, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 11, (uint16_t)(rs * 8 + 4));

                // Call helper -> returns int32 in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f64_i32_bits);
                emit_callx8_a8(&ctx);

                // Store result to rd.lo and sign-extend rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_srai(&ctx, 11, 10, 31);
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAD: { // CVT.F64.I64 Rd, Rs (F64 -> I64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // Load raw f64 bits into a10:a11
                emit_l32i(&ctx, 10, 11, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 11, (uint16_t)(rs * 8 + 4));

                // Call helper -> returns int64 in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f64_i64);
                emit_callx8_a8(&ctx);

                // Store result to rd.lo/rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAA: { // CVT.F32.I32 Rd, Rs (F32 -> I32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw f32 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // call helper, returns int32 in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f32_i32_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low32 and sign-extend high32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_srai(&ctx, 11, 10, 31);
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAB: { // CVT.F32.I64 Rd, Rs (F32 -> I64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw f32 bits
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // call helper, returns int64 in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_f32_i64_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low/high
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAE: { // CVT.U32.F32 Rd, Rs (U32 -> F32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer in a6 across windowed call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw u32 value
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // call helper, returns uint32 f32 bits in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_u32_f32_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low32 and clear high32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xAF: { // CVT.U32.F64 Rd, Rs (U32 -> F64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw u32 value
                emit_l32i(&ctx, 10, 11, (uint16_t)(rs * 8));

                // call helper, returns uint64 f64 bits in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_u32_f64_bits);
                emit_callx8_a8(&ctx);

                // store result to v_regs[rd] low/high
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xB0: { // CVT.U64.F32 Rd, Rs (U64 -> F32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // a10:a11 = raw u64 value
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));

                // Call helper -> returns f32 bits in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_u64_f32_bits);
                emit_callx8_a8(&ctx);

                // Store result to rd.lo and clear rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xB2: { // CVT.I32.F32 Rd, Rs (I32 -> F32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw i32 value
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // Call helper -> returns f32 bits in a10
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_i32_f32_bits);
                emit_callx8_a8(&ctx);

                // Store result to rd.lo and clear rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xB3: { // CVT.I32.F64 Rd, Rs (I32 -> F64)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Save v_regs pointer across call
                emit_mov_n(&ctx, 6, 11);

                // a10 = raw i32 value
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));

                // Call helper -> returns f64 bits in a10:a11
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_i32_f64_bits);
                emit_callx8_a8(&ctx);

                // Store result to rd.lo/rd.hi
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }
            
            // I64/U64 -> F32 conversions (result is 32-bit float)
            case 0xB4: // CVT.I64.F32 Rd, Rs (signed i64 -> f32)
            {
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Save v_regs pointer (a11) to a6
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                
                // Load rs (64-bit) into a10:a11
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));      // a10 = v_regs[rs].lo
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));  // a11 = v_regs[rs].hi
                
                // Load helper address into a8 and call
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_cvt_i64_f32_bits);
                emit_callx8_a8(&ctx);
                
                // Result is 32-bit in a10 only (F32)
                // Store only low 32 bits to rd, clear high 32 bits
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));       // v_regs[rd].lo = a10 (f32 bits)
                emit_movi(&ctx, 10, 0);                           // a10 = 0
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));   // v_regs[rd].hi = 0
                
                // Restore v_regs pointer to a11
                emit_mov_n(&ctx, 11, 6);
                break;
            }
            
            // I64/U64 -> F64 conversions (result is 64-bit double)
            case 0xB1: // CVT.U64.F64 Rd, Rs (unsigned u64 -> f64)
            case 0xB5: // CVT.I64.F64 Rd, Rs (signed i64 -> f64)
            {
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Select helper function (returns uint64_t with double bits)
                void* helper = (op == 0xB1) ? (void*)jit_helper_cvt_u64_f64_bits 
                                            : (void*)jit_helper_cvt_i64_f64_bits;
                
                // Save v_regs pointer (a11) to a6
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                
                // Load rs (64-bit) into a10:a11
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8));      // a10 = v_regs[rs].lo
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8 + 4));  // a11 = v_regs[rs].hi
                
                // Load helper address into a8 and call
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                emit_callx8_a8(&ctx);
                
                // Result is 64-bit in a10:a11
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));       // v_regs[rd].lo = a10
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));   // v_regs[rd].hi = a11
                
                // Restore v_regs pointer to a11
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            // ========== I64 Arithmetic Operations (0x30-0x3F) ==========
            case 0x30: // ADD.I64 Rd, Rs1, Rs2
            case 0x31: // SUB.I64 Rd, Rs1, Rs2
            case 0x32: // MUL.I64 Rd, Rs1, Rs2
            case 0x33: // DIV.I64.S Rd, Rs1, Rs2 (signed)
            case 0x34: // REM.I64.S Rd, Rs1, Rs2 (signed)
            case 0x35: // DIV.I64.S (legacy opcode)
            case 0x36: // DIV.I64.U Rd, Rs1, Rs2 (unsigned)
            case 0x37: // REM.I64.U Rd, Rs1, Rs2 (unsigned)
            case 0x38: // AND.I64 Rd, Rs1, Rs2
            case 0x39: // OR.I64 Rd, Rs1, Rs2
            case 0x3A: // XOR.I64 Rd, Rs1, Rs2
            case 0x3B: // SHL.I64 Rd, Rs1, Rs2 (shift left)
            case 0x3C: // SHR.I64 Rd, Rs1, Rs2 (arithmetic shift right)
            case 0x3D: // USHR.I64 Rd, Rs1, Rs2 (logical shift right)
            {
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;

                uint8_t rs1 = *pc++;

                uint8_t rs2 = *pc++;

                // Fast path: inline NOT.I64
                if (op == 0x3E) {
                    // Use a6 as base for v_regs
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs

                    // Load src
                    emit_l32i(&ctx, 8, 6, (uint16_t)(rs1 * 8));
                    emit_l32i(&ctx, 9, 6, (uint16_t)(rs1 * 8 + 4));

                    // a10 = -1
                    emit_movi(&ctx, 10, -1);

                    // lo: xor a8, a8, a10
                    emit_u8(&ctx, (uint8_t)((10u << 4) | 0x00));
                    emit_u8(&ctx, 0x88);
                    emit_u8(&ctx, 0x30);

                    // hi: xor a9, a9, a10
                    emit_u8(&ctx, (uint8_t)((10u << 4) | 0x00));
                    emit_u8(&ctx, 0x99);
                    emit_u8(&ctx, 0x30);

                    // Store
                    emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                    emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));

                    // Restore v_regs pointer
                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fast path: inline ADD.I64 (critical for Fibonacci hot loop)
                // OPTIMIZED: use a11 directly as v_regs base (no mov a6,a11 / mov a11,a6)
                if (op == 0x30) {
                    // Load operands directly from a11 (v_regs base):
                    // rs1 -> a8(lo), a9(hi)
                    // rs2 -> a10(lo), a12(hi)
                    emit_l32i(&ctx, 8,  11, (uint16_t)(rs1 * 8));
                    emit_l32i(&ctx, 9,  11, (uint16_t)(rs1 * 8 + 4));
                    emit_l32i(&ctx, 10, 11, (uint16_t)(rs2 * 8));
                    emit_l32i(&ctx, 12, 11, (uint16_t)(rs2 * 8 + 4));

                    // Preserve rs1.lo for carry check
                    emit_mov_n(&ctx, 13, 8); // a13 = rs1.lo

                    // lo = rs1.lo + rs2.lo
                    emit_add_n(&ctx, 8, 8, 10); // a8 = a8 + a10

                    // hi = rs1.hi + rs2.hi
                    emit_add_n(&ctx, 9, 9, 12); // a9 = a9 + a12

                    // if (lo >= old_lo) goto no_carry
                    uint32_t bgeu_pos = emit_bgeu_placeholder(&ctx, 8, 13);

                    // carry path: hi += 1
                    emit_addi(&ctx, 9, 9, 1);

                    // no_carry label
                    uint32_t no_carry_target = (uint32_t)ctx.offset;

                    // Store result directly via a11
                    emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                    emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                    // Patch branch (flush first)
                    emit_flush_words(&ctx);
                    patch_bgeu_at(ctx.buffer, bgeu_pos, (int32_t)no_carry_target);

                    break;
                }

                // Fast path: inline SUB.I64 (used in math test)
                // OPTIMIZED: use a11 directly as v_regs base (no mov a6,a11 / mov a11,a6)
                if (op == 0x31) {
                    // Load operands directly from a11 (v_regs base):
                    // rs1 -> a8(lo), a9(hi)
                    // rs2 -> a10(lo), a12(hi)
                    emit_l32i(&ctx, 8,  11, (uint16_t)(rs1 * 8));
                    emit_l32i(&ctx, 9,  11, (uint16_t)(rs1 * 8 + 4));
                    emit_l32i(&ctx, 10, 11, (uint16_t)(rs2 * 8));
                    emit_l32i(&ctx, 12, 11, (uint16_t)(rs2 * 8 + 4));

                    // Preserve rs1.lo for borrow check
                    emit_mov_n(&ctx, 13, 8); // a13 = rs1.lo

                    // lo = rs1.lo - rs2.lo : sub a8, a8, a10
                    emit_u8(&ctx, (10u << 4) | 0x00); // 0xA0
                    emit_u8(&ctx, (8u << 4)  | 8u);   // 0x88
                    emit_u8(&ctx, 0xC0);

                    // hi = rs1.hi - rs2.hi : sub a9, a9, a12
                    emit_u8(&ctx, (12u << 4) | 0x00); // 0xC0
                    emit_u8(&ctx, (9u << 4)  | 9u);   // 0x99
                    emit_u8(&ctx, 0xC0);

                    // if (old_lo >= rs2.lo) goto no_borrow
                    uint32_t bgeu_pos = emit_bgeu_placeholder(&ctx, 13, 10);

                    // borrow path: hi -= 1
                    emit_addi(&ctx, 9, 9, -1);

                    // no_borrow label
                    uint32_t no_borrow_target = (uint32_t)ctx.offset;

                    // Store result directly via a11
                    emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));
                    emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));

                    // Patch branch (flush first)
                    emit_flush_words(&ctx);
                    patch_bgeu_at(ctx.buffer, bgeu_pos, (int32_t)no_borrow_target);

                    break;
                }

                // Fast path: inline AND/OR/XOR.I64 (common in bitwise tests)
                // OPTIMIZED: use a11 directly as v_regs base
                if (op == 0x38 || op == 0x39 || op == 0x3A) {
                    // lo
                    emit_l32i(&ctx, 8, 11, (uint16_t)(rs1 * 8));      // a8 = rs1.lo
                    emit_l32i(&ctx, 9, 11, (uint16_t)(rs2 * 8));      // a9 = rs2.lo
                    if (op == 0x38) {
                        // and a8, a8, a9 => bytes 90 88 10
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x10);
                    } else if (op == 0x39) {
                        // or a8, a8, a9 => bytes 90 88 20
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x20);
                    } else {
                        // xor a8, a8, a9 => bytes 90 88 30
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x30);
                    }
                    emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));

                    // hi
                    emit_l32i(&ctx, 8, 11, (uint16_t)(rs1 * 8 + 4));  // a8 = rs1.hi
                    emit_l32i(&ctx, 9, 11, (uint16_t)(rs2 * 8 + 4));  // a9 = rs2.hi
                    if (op == 0x38) {
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x10);
                    } else if (op == 0x39) {
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x20);
                    } else {
                        emit_u8(&ctx, 0x90);
                        emit_u8(&ctx, 0x88);
                        emit_u8(&ctx, 0x30);
                    }
                    emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8 + 4));

                    break;
                }

                // NOTE: Duplicate ADD.I64 block removed - it was dead code (XOR instead of ADD) 
                // The real ADD.I64 fast path is above (lines 3088-3125)

                // For other I64 operations on 32-bit Xtensa, we call helper functions.
                // Windowed ABI with callx8: window rotates by 8.
                // Caller's a10:a11 -> Callee's a2:a3 (first u64 arg)
                // Caller's a12:a13 -> Callee's a4:a5 (second u64 arg)
                // Return: Callee's a2:a3 -> Caller's a10:a11
                
                // Select helper function
                void* helper = NULL;
                switch (op) {
                    case 0x30: helper = (void*)jit_helper_addu64; break;
                    // case 0x31: helper = (void*)jit_helper_subu64; break; // SUB.I64 is inlined (fast-path)
                    case 0x32: helper = (void*)jit_helper_mulu64; break;
                    case 0x33: helper = (void*)jit_helper_divs64; break;  // DIVS.I64
                    case 0x34: helper = (void*)jit_helper_rems64; break;  // REMS.I64
                    case 0x35: helper = (void*)jit_helper_divs64; break;  // DIVS.I64 (legacy opcode)
                    case 0x36: helper = (void*)jit_helper_divu64; break;
                    case 0x37: helper = (void*)jit_helper_remu64; break;  // REMU.I64
                    case 0x3B: helper = (void*)jit_helper_shl64; break;  // SHL.I64
                    case 0x3C: helper = (void*)jit_helper_shr64; break;  // SHR.I64 (arithmetic)
                    case 0x3D: helper = (void*)jit_helper_ushr64; break; // USHR.I64 (logical)
                    default: ctx.error = true; break;
                }
                if (!helper) { ctx.error = true; break; }
                
                // Save v_regs pointer (a11) to a6 (maps to callee a14, callee-saved)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                
                // Load rs1 into a10:a11 (64-bit value, lo in a10, hi in a11)
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs1 * 8));      // a10 = v_regs[rs1].lo
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs1 * 8 + 4));  // a11 = v_regs[rs1].hi
                
                // Load rs2 into a12:a13
                emit_l32i(&ctx, 12, 6, (uint16_t)(rs2 * 8));      // a12 = v_regs[rs2].lo
                emit_l32i(&ctx, 13, 6, (uint16_t)(rs2 * 8 + 4));  // a13 = v_regs[rs2].hi
                
                // Load helper address into a8 and call
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                
                // callx8 a8 - windowed call through register (E0 08 00)
                emit_callx8_a8(&ctx);
                
                // After windowed call returns:
                // - Result is in caller's a10:a11
                // - v_regs pointer is still in a6
                
                // Store result (a10:a11) to rd using a6 as v_regs base
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));       // v_regs[rd].lo = a10
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));   // v_regs[rd].hi = a11
                
                // Restore v_regs pointer to a11 for subsequent opcodes
                emit_mov_n(&ctx, 11, 6);  // a11 = v_regs
                
                break;
            }

            // ========== I64 IMM8 Operations (0x51-0x56) ==========
            case 0x51: // SUB.I64.IMM8 rd(u8), r1(u8), imm8(i8)
            case 0x52: // MUL.I64.IMM8 rd(u8), r1(u8), imm8(i8)
            case 0x53: // DIVS.I64.IMM8 rd(u8), r1(u8), imm8(i8)
            case 0x54: // DIVU.I64.IMM8 rd(u8), r1(u8), imm8(u8)
            case 0x55: // REMS.I64.IMM8 rd(u8), r1(u8), imm8(i8)
            case 0x56: // REMU.I64.IMM8 rd(u8), r1(u8), imm8(u8)
            {
                // Call a simple helper that takes v_regs and writes result to v_regs[rd].
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t imm_u8 = *pc++;

                void* helper = NULL;
                switch (op) {
                    case 0x51: helper = (void*)&espb_jit_xtensa_sub_i64_imm8; break;
                    case 0x52: helper = (void*)&espb_jit_xtensa_mul_i64_imm8; break;
                    case 0x53: helper = (void*)&espb_jit_xtensa_divs_i64_imm8; break;
                    case 0x54: helper = (void*)&espb_jit_xtensa_divu_i64_imm8; break;
                    case 0x55: helper = (void*)&espb_jit_xtensa_rems_i64_imm8; break;
                    case 0x56: helper = (void*)&espb_jit_xtensa_remu_i64_imm8; break;
                    default: ctx.error = true; break;
                }
                if (ctx.error) break;

                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = rd
                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                // a12 = r1
                if (r1 <= 15) emit_movi_n(&ctx, 12, (int8_t)r1);
                else { emit_load_u32_to_a8(&ctx, &litpool, r1); emit_mov_n(&ctx, 12, 8); }

                // a13 = imm8 (sign-extended i8 for signed ops, zero-extended for unsigned)
                if (op == 0x51 || op == 0x53 || op == 0x55) {
                    int8_t imm_s8 = (int8_t)imm_u8;
                    emit_movi(&ctx, 13, (int16_t)imm_s8);
                } else {
                    emit_movi(&ctx, 13, (int16_t)imm_u8);
                }

                // Re-load v_regs and set up args (callee a2..a7 <= caller a10..a15)
                // NOTE: a8 is used as scratch in this sequence, so load helper address into a8 *after* args are ready.
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8); // a10 = v_regs

                // a11 = rd
                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                // a12 = r1
                if (r1 <= 15) emit_movi_n(&ctx, 12, (int8_t)r1);
                else { emit_load_u32_to_a8(&ctx, &litpool, r1); emit_mov_n(&ctx, 12, 8); }

                // Load helper address into a8 just before the call (a8 is scratch while preparing args)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);

                // Re-emit a13 right before call (helper address load may clobber a13 on some sequences)
                if (op == 0x51 || op == 0x53 || op == 0x55) {
                    int8_t imm_s8 = (int8_t)imm_u8;
                    emit_movi(&ctx, 13, (int16_t)imm_s8);
                } else {
                    emit_movi(&ctx, 13, (int16_t)imm_u8);
                }

                // Call helper through a8
                emit_callx8_a8(&ctx);

                // Restore a11 back to v_regs pointer (ABI for rest of JIT assumes a11=v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x58: { // SHRU.I64.IMM8 Rd, R1, imm8 (Logical Shift Right Unsigned)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t imm = *pc++;
                
                uint8_t sh = imm & 63;
                
                // Load source 64-bit value: a8 = lo, a9 = hi
                emit_l32i(&ctx, 8, 11, (uint16_t)(r1 * 8));      // a8 = v_regs[r1].lo
                emit_l32i(&ctx, 9, 11, (uint16_t)(r1 * 8 + 4));  // a9 = v_regs[r1].hi
                
                if (sh == 0) {
                    // No shift - just copy
                    // Result already in a8, a9
                } else if (sh < 32) {
                    // new_lo = (lo >> sh) | (hi << (32-sh))
                    // new_hi = hi >> sh (logical)
                    uint8_t left = 32 - sh;
                    
                    // Step 1: srli a8, a8, sh (a8 = lo >> sh)
                    // Verified by objdump: srli a8, a8, 1 => 418180 (bytes: 80 81 41)
                    // Format: byte0 = (as << 4), byte1 = (at << 4) | sh, byte2 = 0x41
                    emit_u8(&ctx, (8 << 4) | 0x00);           // (as << 4) = 0x80
                    emit_u8(&ctx, (8 << 4) | sh);             // (at << 4) | sh
                    emit_u8(&ctx, 0x41);                       // opcode
                    
                    // Step 2: slli a10, a9, (32-sh) (a10 = hi << left)
                    // Verified by objdump:
                    //   slli a10, a9, 31 => 01a910 (bytes: 10 A9 01) - left >= 16
                    //   slli a10, a9, 1  => 11a9f0 (bytes: F0 A9 11) - left < 16
                    if (left >= 16) {
                        // slli at, as, left where left >= 16
                        // Verified: slli a10, a9, 31 => bytes: 10 A9 01
                        // byte0 = ((32 - left) << 4), byte1 = (at << 4) | as, byte2 = 0x01
                        emit_u8(&ctx, ((32 - left) << 4) | 0x00);
                        emit_u8(&ctx, (10 << 4) | 9);  // (at << 4) | as = 0xA9
                        emit_u8(&ctx, 0x01);
                    } else {
                        // slli at, as, left where left < 16 (i.e., sh > 16)
                        // Verified: slli a10, a9, 1 => bytes: F0 A9 11
                        // byte0 = ((16 - left) << 4), byte1 = (at << 4) | as, byte2 = 0x11
                        emit_u8(&ctx, ((16 - left) << 4) | 0x00);
                        emit_u8(&ctx, (10 << 4) | 9);  // (at << 4) | as = 0xA9
                        emit_u8(&ctx, 0x11);
                    }
                    
                    // Step 3: or a8, a8, a10 (a8 = (lo >> sh) | (hi << left))
                    // Verified: or a8, a8, a10 => 2088a0 (bytes: A0 88 20)
                    emit_u8(&ctx, 0xA0);  // (at << 4) | 0 = (10 << 4) | 0
                    emit_u8(&ctx, 0x88);  // (as << 4) | ar = (8 << 4) | 8
                    emit_u8(&ctx, 0x20);  // opcode
                    
                    // Step 4: srli a9, a9, sh (a9 = hi >> sh) - new high word
                    // Verified by objdump: srli a9, a9, 1 => bytes 90 91 41
                    emit_u8(&ctx, (9 << 4) | 0x00);           // (as << 4) = 0x90
                    emit_u8(&ctx, (9 << 4) | sh);             // (at << 4) | sh = 0x90 | sh
                    emit_u8(&ctx, 0x41);                       // opcode
                } else if (sh == 32) {
                    // new_lo = hi, new_hi = 0
                    // mov.n a8, a9 => 098d (bytes: 8D 09)
                    emit_u8(&ctx, 0x8D);
                    emit_u8(&ctx, 0x09);
                    // movi.n a9, 0
                    emit_movi_n(&ctx, 9, 0);
                } else {
                    // sh in 33..63: new_lo = hi >> (sh-32), new_hi = 0
                    uint8_t s = sh - 32;
                    
                    // srli a8, a9, s  (a8 = hi >> s)
                    // Verified: srli a8, a9, 10 => 418a90 (bytes: 90 8A 41)
                    // Format: byte0 = (as << 4) | 0, byte1 = (sh << 4) | at, byte2 = 0x41
                    emit_u8(&ctx, (9 << 4) | 0x00);           // (as << 4) | 0 = 0x90
                    emit_u8(&ctx, (s << 4) | 8);              // (s << 4) | at
                    emit_u8(&ctx, 0x41);                       // opcode
                    
                    // movi.n a9, 0
                    emit_movi_n(&ctx, 9, 0);
                }
                
                // Store result
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));      // v_regs[rd].lo = a8
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd * 8 + 4));  // v_regs[rd].hi = a9
                break;
            }

            case 0x40: { // ADD.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x40 ADD.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a8 = a8 + imm8
                emit_addi(&ctx, 8, 8, imm8);

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x41: { // SUB.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x41 SUB.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a8 = a8 - imm8
                emit_addi(&ctx, 8, 8, (int8_t)(-imm8));

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x42: { // MUL.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x42 MUL.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a9 = imm32 (sign-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // mull a8, a8, a9
                emit_u8(&ctx, 0x90);  // (at << 4) | 0, at=a9
                emit_u8(&ctx, 0x88);  // (as << 4) | ar, as=a8, ar=a8
                emit_u8(&ctx, 0x82);  // (op2 << 4) | 2 (mull)

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x43: { // DIVS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x43 DIVS.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);
                // a9 = imm32 (sign-extended)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // Save v_regs pointer across helper call
                emit_mov_n(&ctx, 6, 11);

                // Move args into a10/a11
                emit_mov_n(&ctx, 10, 8);
                emit_mov_n(&ctx, 11, 9);

                emit_call_helper(&ctx, &litpool, (void*)&jit_helper_divs32);

                // Result in a10 -> a8
                emit_mov_n(&ctx, 8, 10);

                // Restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x44: { // DIVU.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x44 DIVU.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);
                // a9 = imm32 (zero-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (uint32_t)(uint8_t)imm8);

                // Save v_regs pointer across helper call
                emit_mov_n(&ctx, 6, 11);

                // Move args into a10/a11
                emit_mov_n(&ctx, 10, 8);
                emit_mov_n(&ctx, 11, 9);

                emit_call_helper(&ctx, &litpool, (void*)&jit_helper_divu32);

                // Result in a10 -> a8
                emit_mov_n(&ctx, 8, 10);

                // Restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x45: { // REMS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x45 REMS.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);
                // a9 = imm32 (sign-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // Save v_regs pointer across helper call
                emit_mov_n(&ctx, 6, 11);

                // Move args into a10/a11
                emit_mov_n(&ctx, 10, 8);
                emit_mov_n(&ctx, 11, 9);

                emit_call_helper(&ctx, &litpool, (void*)&jit_helper_rems32);

                // Result in a10 -> a8
                emit_mov_n(&ctx, 8, 10);

                // Restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x46: { // REMU.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x46 REMU.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);
                // a9 = imm32 (zero-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (uint32_t)(uint8_t)imm8);

                // Save v_regs pointer across helper call
                emit_mov_n(&ctx, 6, 11);

                // Move args into a10/a11
                emit_mov_n(&ctx, 10, 8);
                emit_mov_n(&ctx, 11, 9);

                emit_call_helper(&ctx, &litpool, (void*)&jit_helper_remu32);

                // Result in a10 -> a8
                emit_mov_n(&ctx, 8, 10);

                // Restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x47: { // SHRS.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x47 SHRS.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a8 = a8 >> imm8 (arith)
                emit_srai(&ctx, 8, 8, (uint8_t)(imm8 & 0x1F));

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x48: { // SHRU.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x48 SHRU.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a8 = a8 >> imm8 (logical)
                emit_srli(&ctx, 8, 8, (uint8_t)(imm8 & 0x1F));

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x49: { // AND.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x49 AND.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a9 = imm32 (sign-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // a8 = a8 & a9
                emit_u8(&ctx, 0x90);  // (at << 4) | 0
                emit_u8(&ctx, 0x88);  // (as << 4) | ar
                emit_u8(&ctx, 0x10);  // (op2 << 4) | 0 where op2=1 (AND)

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x4A: { // OR.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x4A OR.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a9 = imm32 (sign-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // a8 = a8 | a9
                emit_u8(&ctx, 0x90);  // (at << 4) | 0
                emit_u8(&ctx, 0x88);  // (as << 4) | ar
                emit_u8(&ctx, 0x20);  // (op2 << 4) | 0 where op2=2 (OR)

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x4B: { // XOR.I32.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x4B XOR.I32.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // a8 = v_regs[r1].low32
                emit_l32i(&ctx, 8, 11, r1_off);

                // a9 = imm32 (sign-extended from imm8)
                emit_load_imm32(&ctx, &litpool, 9, (int32_t)imm8);

                // a8 = a8 ^ a9
                emit_u8(&ctx, 0x90);  // (at << 4) | 0
                emit_u8(&ctx, 0x88);  // (as << 4) | ar
                emit_u8(&ctx, 0x30);  // (op2 << 4) | 0 where op2=3 (XOR)

                // store result low32
                emit_s32i(&ctx, 8, 11, rd_off);
                // clear high32
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0x50: { // ADD.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm8 = *(int8_t*)pc; pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0x50 ADD.I64.IMM8 r1=%u imm=%d", (unsigned)last_off, (unsigned)r1, (int)imm8);
                }

                uint16_t r1_off = (uint16_t)(r1 * 8);
                uint16_t rd_off = (uint16_t)(rd * 8);

                // rs -> a8(lo), a9(hi)
                emit_l32i(&ctx, 8, 11, r1_off);
                emit_l32i(&ctx, 9, 11, (uint16_t)(r1_off + 4));

                // a10 = imm8 (sign-extended), a13 = imm_hi (sign bit)
                emit_movi(&ctx, 10, (int)imm8);
                emit_movi(&ctx, 13, (imm8 < 0) ? -1 : 0);

                // preserve old lo for carry check
                emit_mov_n(&ctx, 12, 8);

                // lo += imm_lo
                emit_add_n(&ctx, 8, 8, 10);
                // hi += imm_hi
                emit_add_n(&ctx, 9, 9, 13);

                // if (lo >= old_lo) skip carry
                uint32_t bgeu_pos = emit_bgeu_placeholder(&ctx, 8, 12);
                emit_addi(&ctx, 9, 9, 1);
                uint32_t no_carry_target = (uint32_t)ctx.offset;
                patch_bgeu_at(ctx.buffer, bgeu_pos, (int32_t)no_carry_target);

                // store result
                emit_s32i(&ctx, 8, 11, rd_off);
                emit_s32i(&ctx, 9, 11, (uint16_t)(rd_off + 4));
                break;
            }

            case 0xC0: { // CMP.EQ.I32 Rd(u8), R1(u8), R2(u8)  (R1 == R2)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (rd == 0) {
                    JIT_LOGW(TAG, "[wr0] bc_off=%u op=0xC0 CMP.EQ.I32 r1=%u r2=%u", (unsigned)last_off, (unsigned)r1, (unsigned)r2);
                }

                uint16_t rd_off = (uint16_t)(rd * 8);

                // Inline compare using a8/a9 branch forms (same style as 0xC1..0xC9)
                emit_mov_n(&ctx, 6, 11); // a6 = v_regs base

                emit_l32i(&ctx, 8, 6, (uint16_t)(r1 * 8));
                emit_l32i(&ctx, 9, 6, (uint16_t)(r2 * 8));

                // Default result = 0
                emit_movi_n(&ctx, 10, 0);

                // beq a8,a9 -> set1
                uint32_t br_pos = emit_bcc_a8_a9_placeholder(&ctx, 0x1);
                uint32_t j_end = emit_j_placeholder(&ctx);

                uint32_t set1_pos = (uint32_t)ctx.offset;
                emit_movi_n(&ctx, 10, 1);

                uint32_t end_pos = (uint32_t)ctx.offset;

                // Store result
                emit_s32i(&ctx, 10, 6, rd_off);
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd_off + 4));

                emit_flush_words(&ctx);
                patch_bcc_a8_a9_at(ctx.buffer, br_pos, (int32_t)set1_pos);
                patch_j_at(ctx.buffer, j_end, (int32_t)(end_pos - (j_end + 3u)));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xC1: // CMP.NE.I32
            case 0xC2: // CMP.LT.I32S
            case 0xC3: // CMP.GT.I32S
            case 0xC4: // CMP.LE.I32S
            case 0xC5: // CMP.GE.I32S
            case 0xC6: // CMP.LT.I32U
            case 0xC7: // CMP.GT.I32U
            case 0xC8: // CMP.LE.I32U
            case 0xC9: { // CMP.GE.I32U
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                uint16_t rd_off = (uint16_t)(rd * 8);

                // Inline compare using a8/a9 branch forms. Result is stored as Value:
                // low32 = 0/1, high32 = 0
                emit_mov_n(&ctx, 6, 11); // a6 = v_regs base

                emit_l32i(&ctx, 8, 6, (uint16_t)(r1 * 8));
                emit_l32i(&ctx, 9, 6, (uint16_t)(r2 * 8));

                // Default result = 0
                emit_movi_n(&ctx, 10, 0);

                // Determine branch condition and whether to swap operands
                uint8_t cond_nib = 0;
                bool swap = false;
                switch (op) {
                    case 0xC1: cond_nib = 0x9; swap = false; break; // bne  a8,a9
                    case 0xC2: cond_nib = 0x2; swap = false; break; // blt  a8,a9 (signed)
                    case 0xC3: cond_nib = 0x2; swap = true;  break; // gt.s: blt  a9,a8
                    case 0xC4: cond_nib = 0xA; swap = true;  break; // le.s: bge  a9,a8
                    case 0xC5: cond_nib = 0xA; swap = false; break; // ge.s: bge  a8,a9
                    case 0xC6: cond_nib = 0x3; swap = false; break; // bltu a8,a9
                    case 0xC7: cond_nib = 0x3; swap = true;  break; // gt.u: bltu a9,a8
                    case 0xC8: cond_nib = 0xB; swap = true;  break; // le.u: bgeu a9,a8
                    case 0xC9: cond_nib = 0xB; swap = false; break; // ge.u: bgeu a8,a9
                    default: ctx.error = true; break;
                }
                if (ctx.error) break;

                if (swap) {
                    emit_mov_n(&ctx, 12, 8);
                    emit_mov_n(&ctx, 8, 9);
                    emit_mov_n(&ctx, 9, 12);
                }

                // If condition true -> set result = 1
                uint32_t br_pos = emit_bcc_a8_a9_placeholder(&ctx, cond_nib);
                uint32_t j_end = emit_j_placeholder(&ctx);

                uint32_t set1_pos = (uint32_t)ctx.offset;
                emit_movi_n(&ctx, 10, 1);

                uint32_t end_pos = (uint32_t)ctx.offset;

                // Store result
                emit_s32i(&ctx, 10, 6, rd_off);
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd_off + 4));

                // Patch
                emit_flush_words(&ctx);
                patch_bcc_a8_a9_at(ctx.buffer, br_pos, (int32_t)set1_pos);
                patch_j_at(ctx.buffer, j_end, (int32_t)(end_pos - (j_end + 3u)));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x74: { // STORE.I32 Rs(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Fast inline path for aligned 32-bit store.
                // NOTE: interpreter uses memcpy (supports unaligned). Xtensa s32i requires word alignment.
                // Keep helper fallback for unaligned offsets.
                if ((off16 & 3) == 0) {
                    // a6 = v_regs (save for restore)
                    emit_mov_n(&ctx, 6, 11);

                    // a8 = base pointer (low32) from v_regs[ra]
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 += off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            // FIX: save base address before loading offset
                            emit_mov_n(&ctx, 7, 8);  // a7 = base address
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_add_n(&ctx, 8, 7, 8);  // a8 = base + offset
                        }
                    }

                    // value = v_regs[rs].lo
                    emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));

                    // *(uint32_t*)a8 = value
                    emit_s32i(&ctx, 9, 8, 0);

                    // restore v_regs pointer
                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fallback: unaligned offset -> helper (memcpy semantics)
                // Call helper: espb_jit_xtensa_store_i32(v_regs, rs, ra, offset)
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = rs
                if (rs <= 15) emit_movi_n(&ctx, 11, (int8_t)rs);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs); emit_mov_n(&ctx, 11, 8); }

                // a12 = ra
                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                // a13 = offset (sign-extended i16)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_store_i32);

                // Restore a11 back to v_regs pointer (ABI for rest of JIT assumes a11=v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x78: { // STORE.F32 Rs(u8), Ra(u8), offset(i16) (store raw f32 bits)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Same implementation strategy as STORE.I32:
                // - aligned => inline s32i
                // - unaligned => helper memcpy semantics
                if ((off16 & 3) == 0) {
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs (save for restore)

                    // a8 = base pointer from v_regs[ra]
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 += off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            // FIX: save base address before loading offset
                            emit_mov_n(&ctx, 7, 8);  // a7 = base address
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_add_n(&ctx, 8, 7, 8);  // a8 = base + offset
                        }
                    }

                    // f32 is stored in low 32 bits of v_regs[rs]
                    emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                    emit_s32i(&ctx, 9, 8, 0);

                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Unaligned -> same helper as STORE.I32
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                if (rs <= 15) emit_movi_n(&ctx, 11, (int8_t)rs);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs); emit_mov_n(&ctx, 11, 8); }

                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_store_i32);

                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x79: { // STORE.F64 Rs(u8), Ra(u8), offset(i16) (store raw f64 bits)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Same strategy as STORE.I64:
                // - aligned => inline two s32i
                // - unaligned => helper memcpy semantics (reuse store_i64)
                if ((off16 & 3) == 0) {
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs (save for restore)

                    // a8 = base pointer
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 += off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            // FIX: save base address before loading offset
                            emit_mov_n(&ctx, 7, 8);  // a7 = base address
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_add_n(&ctx, 8, 7, 8);  // a8 = base + offset
                        }
                    }

                    // load raw f64 bits (lo/hi)
                    emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                    emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8 + 4));

                    // store two words
                    emit_s32i(&ctx, 9, 8, 0);
                    emit_s32i(&ctx, 10, 8, 4);

                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fallback to memcpy semantics via store_i64 helper
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                if (rs <= 15) emit_movi_n(&ctx, 11, (int8_t)rs);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs); emit_mov_n(&ctx, 11, 8); }

                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_store_i64);

                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x7A: { // STORE.PTR Rs(u8), Ra(u8), offset(i16) - INLINE (same as I32 on 32-bit)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // PTR is 4 bytes on 32-bit architecture, same as I32
                // For aligned offsets, use inline s32i
                // For unaligned, use byte-by-byte store via helper or memcpy approach
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs

                // a8 = base pointer from v_regs[ra].ptr
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                // a8 += off16
                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // a9 = v_regs[rs].ptr (value to store)
                emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));

                // Check alignment for optimal code path
                if ((off16 & 3) == 0) {
                    // Aligned: use s32i directly
                    emit_s32i(&ctx, 9, 8, 0);
                } else {
                    // Unaligned: use byte-by-byte store
                    emit_s8i(&ctx, 9, 8, 0);
                    emit_srli(&ctx, 10, 9, 8);
                    emit_s8i(&ctx, 10, 8, 1);
                    emit_srli(&ctx, 10, 9, 16);
                    emit_s8i(&ctx, 10, 8, 2);
                    emit_srli(&ctx, 10, 9, 24);
                    emit_s8i(&ctx, 10, 8, 3);
                }

                emit_mov_n(&ctx, 11, 6);  // restore v_regs to a11
                break;
            }

            case 0x80: { // LOAD.I8S Rd(u8), Ra(u8), offset(i16) - INLINE load signed byte
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Inline load signed byte
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // a8 = v_regs[ra].ptr (base address)

                // Add offset to base address
                if (off16 == 0) {
                    // No offset needed
                } else if (off16 >= -128 && off16 <= 127) {
                    emit_addi(&ctx, 8, 8, (int8_t)off16);
                } else {
                    // Load large offset to a10, then add
                    emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                    emit_mov_n(&ctx, 10, 8);
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // reload base
                    emit_add_n(&ctx, 8, 8, 10);
                }

                // Load unsigned byte
                emit_l8ui(&ctx, 9, 8, 0);  // a9 = *(uint8_t*)(a8)

                // Sign-extend from 8 to 32 bit: slli 24, then srai 24
                emit_sext_i8(&ctx, 9, 9);  // a9 = sign-extended 8-bit value

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type (high word = 0 for i32)
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore a11 = v_regs
                break;
            }

            case 0x81: { // LOAD.I8U Rd(u8), Ra(u8), offset(i16) - INLINE
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Inline load unsigned byte (no sign-extend needed)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // a8 = v_regs[ra].ptr

                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // Load unsigned byte (already zero-extended by L8UI)
                emit_l8ui(&ctx, 9, 8, 0);  // a9 = *(uint8_t*)(a8)

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I32
                emit_movi_n(&ctx, 10, 1);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x82: { // LOAD.I16S Rd(u8), Ra(u8), offset(i16) - INLINE
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Inline load signed 16-bit
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // a8 = v_regs[ra].ptr

                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // Load signed 16-bit using byte loads (unaligned-safe)
                // l8ui a9, a8, 0   - load low byte (unsigned)
                emit_l8ui(&ctx, 9, 8, 0);
                // l8ui a10, a8, 1  - load high byte (unsigned)
                emit_l8ui(&ctx, 10, 8, 1);
                // slli a10, a10, 8 - shift high byte
                emit_slli(&ctx, 10, 10, 8);
                // or a9, a9, a10   - combine bytes
                emit_or(&ctx, 9, 9, 10);
                // Sign-extend from 16 to 32 bit: slli then srai
                emit_slli(&ctx, 9, 9, 16);
                emit_srai(&ctx, 9, 9, 16);  // a9 = sign-extended 16-bit value

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I32
                emit_movi_n(&ctx, 10, 1);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x83: { // LOAD.I16U Rd(u8), Ra(u8), offset(i16) - INLINE (unaligned-safe)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Inline load unsigned 16-bit
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // a8 = v_regs[ra].ptr

                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // Load unsigned 16-bit using byte loads (unaligned-safe)
                // l8ui a9, a8, 0   - load low byte (unsigned)
                emit_l8ui(&ctx, 9, 8, 0);
                // l8ui a10, a8, 1  - load high byte (unsigned)
                emit_l8ui(&ctx, 10, 8, 1);
                // slli a10, a10, 8 - shift high byte
                emit_slli(&ctx, 10, 10, 8);
                // or a9, a9, a10   - combine bytes (already zero-extended)
                emit_or(&ctx, 9, 9, 10);

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type ESPB_TYPE_I32
                emit_movi_n(&ctx, 10, 1);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x89: { // LOAD.BOOL Rd(u8), Ra(u8), offset(i16) - INLINE loads byte and normalizes to 0/1
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Inline load bool (load byte and normalize to 0/1)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // a8 = v_regs[ra].ptr (base address)

                // Add offset to base address
                if (off16 == 0) {
                    // No offset needed
                } else if (off16 >= -128 && off16 <= 127) {
                    emit_addi(&ctx, 8, 8, (int8_t)off16);
                } else {
                    // Load large offset to a10, then add
                    emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                    emit_mov_n(&ctx, 10, 8);
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));  // reload base
                    emit_add_n(&ctx, 8, 8, 10);
                }

                // Load unsigned byte
                emit_l8ui(&ctx, 9, 8, 0);  // a9 = *(uint8_t*)(a8)

                // Normalize to 0/1: if (a9 != 0) a9 = 1
                // Use movi.n a10, 1; moveqz a10, a9, a9; (if a9==0, a10=a9=0, else a10=1)
                // Simpler: use comparison - MOVI a10, 0; BEQ a9, a10, skip; MOVI a9, 1; skip:
                // Or even simpler: use SNE (set if not equal) - but Xtensa may not have it
                // Simplest portable way: compare with 0 and set result
                emit_movi_n(&ctx, 10, 0);      // a10 = 0
                emit_movi_n(&ctx, 9, 1);       // a9 = 1 (assume non-zero)
                // Now need to check if original byte was 0
                // Reload byte and check
                emit_l8ui(&ctx, 7, 8, 0);      // a7 = original byte
                // MOVNEZ: if (a7 != 0) a9 = a9 (stays 1), else need to set to 0
                // Actually use MOVEQZ: if (a7 == 0) a9 = a10 (which is 0)
                emit_moveqz(&ctx, 9, 10, 7);   // if (a7 == 0) a9 = a10 (0)

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8));

                // Store type (high word = 0 for bool/i32)
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);  // restore a11 = v_regs
                break;
            }

            case 0x76: { // STORE.I64 Rs(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Fast inline path for aligned 64-bit store.
                // NOTE: interpreter uses memcpy (supports unaligned). Xtensa s32i requires word alignment.
                // Keep helper fallback for unaligned offsets.
                if ((off16 & 3) == 0) {
                    // a6 = v_regs (save for restore)
                    emit_mov_n(&ctx, 6, 11);

                    // a8 = base pointer (low32) from v_regs[ra]
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 += off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            // FIX: save base address before loading offset
                            emit_mov_n(&ctx, 7, 8);  // a7 = base address
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_add_n(&ctx, 8, 7, 8);  // a8 = base + offset
                        }
                    }

                    // load value lo/hi from v_regs[rs]
                    emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                    emit_l32i(&ctx, 10, 6, (uint16_t)(rs * 8 + 4));

                    // store to *(uint64_t*)a8 as two words
                    emit_s32i(&ctx, 9, 8, 0);
                    emit_s32i(&ctx, 10, 8, 4);

                    // restore v_regs pointer
                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fallback: unaligned offset -> helper (memcpy semantics)
                // Call helper: espb_jit_xtensa_store_i64(v_regs, rs, ra, offset)
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = rs
                if (rs <= 15) emit_movi_n(&ctx, 11, (int8_t)rs);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs); emit_mov_n(&ctx, 11, 8); }

                // a12 = ra
                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                // a13 = offset (sign-extended i16)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_store_i64);

                // Restore a11 back to v_regs pointer (ABI for rest of JIT assumes a11=v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x84: { // LOAD.I32 Rd(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Fast inline path (aligned 32-bit load). NOTE: interpreter uses memcpy (supports unaligned),
                // but Xtensa l32i requires word alignment. Keep helper fallback for unaligned offsets.
                if ((off16 & 3) == 0) {
                    // a6 = v_regs (callee-saved across windowed calls)
                    emit_mov_n(&ctx, 6, 11);

                    // a8 = V_PTR(v_regs[ra]) (low32)
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 = a8 + off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            // a10 = (int32)off16
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_mov_n(&ctx, 10, 8);
                            emit_add_n(&ctx, 8, 8, 10);
                        }
                    }

                    // a9 = *(int32_t*)a8
                    emit_l32i(&ctx, 9, 8, 0);

                    // v_regs[rd].lo = a9; v_regs[rd].hi = 0
                    uint16_t rd_off = (uint16_t)(rd * 8);
                    emit_s32i(&ctx, 9, 6, rd_off);
                    emit_movi_n(&ctx, 9, 0);
                    emit_s32i(&ctx, 9, 6, (uint16_t)(rd_off + 4));

                    // restore v_regs pointer
                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fallback: unaligned offset -> helper (memcpy semantics)
                // Call helper: espb_jit_xtensa_load_i32(v_regs, rd, ra, offset)
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = rd
                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                // a12 = ra
                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                // a13 = offset (sign-extended i16)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_load_i32);

                // Restore a11 back to v_regs pointer (ABI for rest of JIT assumes a11=v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x86: { // LOAD.F32 Rd(u8), Ra(u8), offset(i16) (load raw f32 bits)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Same strategy as LOAD.I32:
                // - aligned => inline l32i
                // - unaligned => helper memcpy semantics
                if ((off16 & 3) == 0) {
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8)); // a8 = base pointer

                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_mov_n(&ctx, 10, 8);
                            emit_add_n(&ctx, 8, 8, 10);
                        }
                    }

                    emit_l32i(&ctx, 9, 8, 0); // raw bits

                    uint16_t rd_off = (uint16_t)(rd * 8);
                    emit_s32i(&ctx, 9, 6, rd_off); // store to low32
                    emit_movi_n(&ctx, 9, 0);
                    emit_s32i(&ctx, 9, 6, (uint16_t)(rd_off + 4));

                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Unaligned => reuse the same helper as LOAD.I32
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_load_i32);

                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x85: { // LOAD.I64 Rd(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Fast inline path for aligned 64-bit load.
                // NOTE: interpreter uses memcpy (supports unaligned). Xtensa l32i requires word alignment.
                // We keep helper fallback for unaligned offsets.
                if ((off16 & 3) == 0) {
                    // a6 = v_regs
                    emit_mov_n(&ctx, 6, 11);

                    // a8 = base pointer (low32) from v_regs[ra]
                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    // a8 += off16
                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_mov_n(&ctx, 10, 8);
                            emit_add_n(&ctx, 8, 8, 10);
                        }
                    }

                    // load lo/hi
                    emit_l32i(&ctx, 9, 8, 0);
                    emit_l32i(&ctx, 10, 8, 4);

                    // store into v_regs[rd]
                    uint16_t rd_off = (uint16_t)(rd * 8);
                    emit_s32i(&ctx, 9, 6, rd_off);
                    emit_s32i(&ctx, 10, 6, (uint16_t)(rd_off + 4));

                    // restore v_regs pointer
                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Fallback: unaligned offset -> helper (memcpy semantics)
                // Call helper: espb_jit_xtensa_load_i64(v_regs, rd, ra, offset)
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = rd
                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                // a12 = ra
                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                // a13 = offset (sign-extended i16)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_load_i64);

                // Restore a11 back to v_regs pointer (ABI for rest of JIT assumes a11=v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x87: { // LOAD.F64 Rd(u8), Ra(u8), offset(i16) (load raw f64 bits)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // Same strategy as LOAD.I64:
                // - aligned => inline two l32i
                // - unaligned => helper memcpy semantics (reuse load_i64)
                if ((off16 & 3) == 0) {
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs

                    emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                    if (off16 != 0) {
                        if (off16 >= -128 && off16 <= 127) {
                            emit_addi(&ctx, 8, 8, (int8_t)off16);
                        } else {
                            emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                            emit_mov_n(&ctx, 10, 8);
                            emit_add_n(&ctx, 8, 8, 10);
                        }
                    }

                    emit_l32i(&ctx, 9, 8, 0);
                    emit_l32i(&ctx, 10, 8, 4);

                    uint16_t rd_off = (uint16_t)(rd * 8);
                    emit_s32i(&ctx, 9, 6, rd_off);
                    emit_s32i(&ctx, 10, 6, (uint16_t)(rd_off + 4));

                    emit_mov_n(&ctx, 11, 6);
                    break;
                }

                // Unaligned -> reuse load_i64 helper (memcpy 8 bytes)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                if (ra <= 15) emit_movi_n(&ctx, 12, (int8_t)ra);
                else { emit_load_u32_to_a8(&ctx, &litpool, ra); emit_mov_n(&ctx, 12, 8); }

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                emit_mov_n(&ctx, 13, 8);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_load_i64);

                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0xCA: // CMP.EQ.I64
            case 0xCB: // CMP.NE.I64
            case 0xCC: // CMP.LT.I64S
            case 0xCD: // CMP.GT.I64S
            case 0xCE: // CMP.LE.I64S
            case 0xCF: // CMP.GE.I64S
            case 0xD0: // CMP.LT.I64U
            case 0xD1: // CMP.GT.I64U
            case 0xD2: // CMP.LE.I64U
            case 0xD3: { // CMP.GE.I64U
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                // Fast path: inline EQ/NE for I64 (very common in control flow)
                if (op == 0xCA || op == 0xCB) {
                    uint16_t rd_off = (uint16_t)(rd * 8);
                    emit_mov_n(&ctx, 6, 11); // a6 = v_regs base

                    // Compare low32
                    emit_l32i(&ctx, 8, 6, (uint16_t)(r1 * 8));
                    emit_l32i(&ctx, 9, 6, (uint16_t)(r2 * 8));

                    // Default result = (op==NE) ? 0 : 0
                    emit_movi_n(&ctx, 10, 0);

                    if (op == 0xCA) {
                        // EQ: if (lo !=) -> false; if (hi !=) -> false; else true
                        // if lo != -> end
                        uint32_t br_lo_ne = emit_bcc_a8_a9_placeholder(&ctx, 0x9); // bne
                        // if hi != -> end
                        emit_l32i(&ctx, 8, 6, (uint16_t)(r1 * 8 + 4));
                        emit_l32i(&ctx, 9, 6, (uint16_t)(r2 * 8 + 4));
                        uint32_t br_hi_ne = emit_bcc_a8_a9_placeholder(&ctx, 0x9); // bne

                        // equal => set1
                        emit_movi_n(&ctx, 10, 1);
                        uint32_t end_pos = (uint32_t)ctx.offset;

                        // store
                        emit_s32i(&ctx, 10, 6, rd_off);
                        emit_movi_n(&ctx, 8, 0);
                        emit_s32i(&ctx, 8, 6, (uint16_t)(rd_off + 4));

                        emit_flush_words(&ctx);
                        patch_bcc_a8_a9_at(ctx.buffer, br_lo_ne, (int32_t)end_pos);
                        patch_bcc_a8_a9_at(ctx.buffer, br_hi_ne, (int32_t)end_pos);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    } else {
                        // NE: if (lo !=) -> true; else if (hi !=) -> true; else false
                        uint32_t br_lo_ne = emit_bcc_a8_a9_placeholder(&ctx, 0x9); // bne -> set1
                        // hi compare
                        emit_l32i(&ctx, 8, 6, (uint16_t)(r1 * 8 + 4));
                        emit_l32i(&ctx, 9, 6, (uint16_t)(r2 * 8 + 4));
                        uint32_t br_hi_ne = emit_bcc_a8_a9_placeholder(&ctx, 0x9); // bne -> set1
                        // fallthrough false
                        uint32_t j_end = emit_j_placeholder(&ctx);

                        uint32_t set1_pos = (uint32_t)ctx.offset;
                        emit_movi_n(&ctx, 10, 1);
                        uint32_t end_pos = (uint32_t)ctx.offset;

                        emit_s32i(&ctx, 10, 6, rd_off);
                        emit_movi_n(&ctx, 8, 0);
                        emit_s32i(&ctx, 8, 6, (uint16_t)(rd_off + 4));

                        emit_flush_words(&ctx);
                        patch_bcc_a8_a9_at(ctx.buffer, br_lo_ne, (int32_t)set1_pos);
                        patch_bcc_a8_a9_at(ctx.buffer, br_hi_ne, (int32_t)set1_pos);
                        patch_j_at(ctx.buffer, j_end, (int32_t)(end_pos - (j_end + 3u)));
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }
                }

                // Slow path: call helper for other I64 comparisons
                // Windowed ABI mapping: callee a2..a7 <= caller a10..a15
                // a10 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 10, 8);

                // a11 = opcode (use l32r for values > 95 as movi.n has limited range -32..95)
                if (op <= 95) {
                    emit_movi_n(&ctx, 11, (int8_t)op);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, op);
                    emit_mov_n(&ctx, 11, 8);
                }

                // a12 = rd
                if (rd <= 15) emit_movi_n(&ctx, 12, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 12, 8); }

                // a13 = r1
                if (r1 <= 15) emit_movi_n(&ctx, 13, (int8_t)r1);
                else { emit_load_u32_to_a8(&ctx, &litpool, r1); emit_mov_n(&ctx, 13, 8); }

                // a14 = r2
                if (r2 <= 15) emit_movi_n(&ctx, 14, (int8_t)r2);
                else { emit_load_u32_to_a8(&ctx, &litpool, r2); emit_mov_n(&ctx, 14, 8); }

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_xtensa_cmp_i64);

                // Restore a11 back to v_regs pointer
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0xE0: // CMP.EQ.F32
            case 0xE1: // CMP.NE.F32
            case 0xE2: // CMP.LT.F32
            case 0xE3: // CMP.GT.F32
            case 0xE4: // CMP.LE.F32
            case 0xE5: { // CMP.GE.F32
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                void* helper = NULL;
                switch (op) {
                    case 0xE0: helper = (void*)jit_helper_cmp_eq_f32; break;
                    case 0xE1: helper = (void*)jit_helper_cmp_ne_f32; break;
                    case 0xE2: helper = (void*)jit_helper_cmp_lt_f32; break;
                    case 0xE3: helper = (void*)jit_helper_cmp_gt_f32; break;
                    case 0xE4: helper = (void*)jit_helper_cmp_le_f32; break;
                    case 0xE5: helper = (void*)jit_helper_cmp_ge_f32; break;
                    default: ctx.error = true; break;
                }
                if (ctx.error) break;

                emit_mov_n(&ctx, 6, 11); // save v_regs

                emit_l32i(&ctx, 10, 6, (uint16_t)(r1 * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(r2 * 8));

                emit_call_helper(&ctx, &litpool, helper);

                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xE6: // CMP.EQ.F64
            case 0xE7: // CMP.NE.F64
            case 0xE8: // CMP.LT.F64
            case 0xE9: // CMP.GT.F64
            case 0xEA: // CMP.LE.F64
            case 0xEB: { // CMP.GE.F64
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                void* helper = NULL;
                switch (op) {
                    case 0xE6: helper = (void*)jit_helper_cmp_eq_f64; break;
                    case 0xE7: helper = (void*)jit_helper_cmp_ne_f64; break;
                    case 0xE8: helper = (void*)jit_helper_cmp_lt_f64; break;
                    case 0xE9: helper = (void*)jit_helper_cmp_gt_f64; break;
                    case 0xEA: helper = (void*)jit_helper_cmp_le_f64; break;
                    case 0xEB: helper = (void*)jit_helper_cmp_ge_f64; break;
                    default: ctx.error = true; break;
                }
                if (ctx.error) break;

                emit_mov_n(&ctx, 6, 11); // save v_regs

                emit_l32i(&ctx, 10, 6, (uint16_t)(r1 * 8));
                emit_l32i(&ctx, 11, 6, (uint16_t)(r1 * 8 + 4));
                emit_l32i(&ctx, 12, 6, (uint16_t)(r2 * 8));
                emit_l32i(&ctx, 13, 6, (uint16_t)(r2 * 8 + 4));

                emit_call_helper(&ctx, &litpool, helper);

                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 10, 0);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xBD: { // INTTOPTR Rd(u8), Rs(u8) - Convert I32 to PTR
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // PTR is stored as low32 address, high32 must be 0
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));        // a8 = v_regs[rs].lo
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));        // v_regs[rd].lo = a8
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8 + 4));    // v_regs[rd].hi = 0
                break;
            }

            case 0xBE: // SELECT.I32
            case 0xBF: // SELECT.I64
            case 0xD4: // SELECT.F32
            case 0xD5: // SELECT.F64
            case 0xD6: { // SELECT.PTR
                // Rd = Rcond ? Rtrue : Rfalse
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t r_cond = *pc++;
                uint8_t r_true = *pc++;
                uint8_t r_false = *pc++;

                // SELECT.PTR: use helper to avoid subtle issues when pointer values are materialized via INTTOPTR/LDC.
                // Helper copies full Value slot.
                if (op == 0xD6) {
                    // a10 = v_regs
                    emit_l32i(&ctx, 8, 1, 8);
                    emit_mov_n(&ctx, 10, 8);

                    // a11 = rd
                    if (rd <= 15) emit_movi_n(&ctx, 11, (int8_t)rd);
                    else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 11, 8); }

                    // a12 = r_cond
                    if (r_cond <= 15) emit_movi_n(&ctx, 12, (int8_t)r_cond);
                    else { emit_load_u32_to_a8(&ctx, &litpool, r_cond); emit_mov_n(&ctx, 12, 8); }

                    // a13 = r_true
                    if (r_true <= 15) emit_movi_n(&ctx, 13, (int8_t)r_true);
                    else { emit_load_u32_to_a8(&ctx, &litpool, r_true); emit_mov_n(&ctx, 13, 8); }

                    // a14 = r_false
                    if (r_false <= 15) emit_movi_n(&ctx, 14, (int8_t)r_false);
                    else { emit_load_u32_to_a8(&ctx, &litpool, r_false); emit_mov_n(&ctx, 14, 8); }

                    // Load helper into a8 last (a8 is scratch above)
                    emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)&espb_jit_xtensa_select);
                    emit_callx8_a8(&ctx);

                    // Restore a11 back to v_regs
                    emit_l32i(&ctx, 8, 1, 8);
                    emit_mov_n(&ctx, 11, 8);
                    break;
                }

                uint16_t rd_off = (uint16_t)(rd * 8);
                uint16_t cond_off = (uint16_t)(r_cond * 8);
                uint16_t true_off = (uint16_t)(r_true * 8);
                uint16_t false_off = (uint16_t)(r_false * 8);

                // Inline select: load cond.i32, branch, copy 8 bytes
                // Do NOT rely on a11 always being v_regs here; load stable v_regs pointer from stack slot +8.
                emit_l32i(&ctx, 6, 1, 8); // a6 = saved v_regs

                // a8 = cond low32
                emit_l32i(&ctx, 8, 6, cond_off);

                // if (a8 == 0) goto false_path
                uint32_t br_false = emit_beqz_n_a8_placeholder(&ctx);

                // true path: copy r_true -> rd
                emit_l32i(&ctx, 9, 6, true_off);
                emit_l32i(&ctx, 10, 6, (uint16_t)(true_off + 4));
                emit_s32i(&ctx, 9, 6, rd_off);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd_off + 4));
                uint32_t j_end = emit_j_placeholder(&ctx);

                // false label
                uint32_t false_pos = (uint32_t)ctx.offset;
                emit_l32i(&ctx, 9, 6, false_off);
                emit_l32i(&ctx, 10, 6, (uint16_t)(false_off + 4));
                emit_s32i(&ctx, 9, 6, rd_off);
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd_off + 4));

                uint32_t end_pos = (uint32_t)ctx.offset;

                // Patch
                emit_flush_words(&ctx);
                {
                    uint32_t pc_after = br_false + 2u;
                    int32_t delta = (int32_t)false_pos - (int32_t)pc_after;
                    patch_beqz_n_a8_at(ctx.buffer, br_false, delta);
                }
                patch_j_at(ctx.buffer, j_end, (int32_t)(end_pos - (j_end + 3u)));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x0A: { // CALL local_func_idx(u16)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint16_t local_func_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;

                JIT_LOGI(TAG, "[CALL] Generating call to local_func_idx=%u at bc_off=%zu", 
                         (unsigned)local_func_idx, (size_t)(pc - start - 3));

                // Call helper: jit_call_espb_function_xtensa(instance, local_func_idx, v_regs)
                // Windowed ABI: callee a2..a7 <= caller a10..a15
                
                // a10 = instance (from stack slot 4, see prologue)
                emit_l32i(&ctx, 10, 1, 4);

                // a11 = local_func_idx
                emit_load_u32_to_a8(&ctx, &litpool, local_func_idx);
                emit_mov_n(&ctx, 11, 8);

                // a12 = v_regs (from stack slot 8, see prologue)
                emit_l32i(&ctx, 12, 1, 8);

                emit_call_helper(&ctx, &litpool, (void*)&jit_call_espb_function_xtensa);

                // Restore a11 back to v_regs pointer
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 11, 8);
                break;
            }

            case 0x0B: { // CALL_INDIRECT Rfunc(u8), type_idx(u16)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t r_func_idx = *pc++;
                uint16_t expected_type_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;

                JIT_LOGI(TAG, "[CALL_INDIRECT] Generating indirect call via v_regs[%u] at bc_off=%zu", 
                         (unsigned)r_func_idx, (size_t)(pc - start - 4));

                // ИСПРАВЛЕНО: Используем espb_jit_call_indirect вместо jit_call_espb_function_xtensa
                // Это позволяет обрабатывать как прямые local_func_idx, так и указатели в data segment
                // (аналогично логике интерпретатора op_0x0B и исправлению для RISC-V JIT)
                
                // Call helper: espb_jit_call_indirect(instance, func_idx_or_ptr, type_idx, v_regs, num_virtual_regs, func_idx_reg)
                // Windowed ABI: callee a2..a7 <= caller a10..a15

                // Preserve v_regs pointer
                emit_mov_n(&ctx, 6, 11);

                // a10 = instance (from stack slot 4)
                emit_l32i(&ctx, 10, 1, 4);

                // a11 = func_idx_or_ptr from v_regs[r_func_idx]
                emit_l32i(&ctx, 11, 6, (uint16_t)(r_func_idx * 8));

                // a12 = type_idx
                if (expected_type_idx <= 15) {
                    emit_movi_n(&ctx, 12, (int8_t)expected_type_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, expected_type_idx);
                    emit_mov_n(&ctx, 12, 8);
                }

                // a13 = v_regs
                emit_mov_n(&ctx, 13, 6);

                // a14 = num_vregs
                if (num_vregs <= 15) {
                    emit_movi_n(&ctx, 14, (int8_t)num_vregs);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, num_vregs);
                    emit_mov_n(&ctx, 14, 8);
                }

                // a15 = func_idx_reg
                if (r_func_idx <= 15) {
                    emit_movi_n(&ctx, 15, (int8_t)r_func_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, r_func_idx);
                    emit_mov_n(&ctx, 15, 8);
                }

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_call_indirect);

                // Restore a11 back to v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x0D: { // CALL_INDIRECT_PTR Rfunc_ptr(u8), type_idx(u16)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rptr = *pc++;
                uint16_t type_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;

                JIT_LOGI(TAG, "[CALL_INDIRECT_PTR] Generating indirect ptr call via v_regs[%u], type_idx=%u at bc_off=%zu", 
                         (unsigned)rptr, (unsigned)type_idx, (size_t)(pc - start - 4));

                // Call helper: espb_jit_call_indirect_ptr(instance, target_ptr, type_idx, v_regs, num_virtual_regs, func_ptr_reg)
                // Windowed ABI: callee a2..a7 <= caller a10..a15

                // Preserve v_regs pointer
                emit_mov_n(&ctx, 6, 11);

                // a10 = instance (from stack slot 4)
                emit_l32i(&ctx, 10, 1, 4);

                // a11 = target_ptr from v_regs[rptr] (PTR field)
                emit_l32i(&ctx, 11, 6, (uint16_t)(rptr * 8));

                // a12 = type_idx
                if (type_idx <= 15) {
                    emit_movi_n(&ctx, 12, (int8_t)type_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, type_idx);
                    emit_mov_n(&ctx, 12, 8);
                }

                // a13 = v_regs
                emit_mov_n(&ctx, 13, 6);

                // a14 = num_vregs
                if (num_vregs <= 15) {
                    emit_movi_n(&ctx, 14, (int8_t)num_vregs);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, num_vregs);
                    emit_mov_n(&ctx, 14, 8);
                }

                // a15 = func_ptr_reg
                if (rptr <= 15) {
                    emit_movi_n(&ctx, 15, (int8_t)rptr);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rptr);
                    emit_mov_n(&ctx, 15, 8);
                }

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_call_indirect_ptr);

                // Restore a11 back to v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x1D: { // LD_GLOBAL_ADDR
                // Call helper: espb_jit_ld_global_addr(instance, symbol_idx, v_regs, num_vregs, rd)
                if (pc + 3 > end) { ctx.error = true; break; }  // 1 byte rd + 2 bytes symbol_idx
                uint8_t rd = *pc++;
                uint16_t symbol_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;
                
                JIT_LOGI(TAG, "[0x1D] LD_GLOBAL_ADDR: rd=%u symbol_idx=%u num_vregs=%u", 
                         (unsigned)rd, (unsigned)symbol_idx, (unsigned)num_vregs);

                // Landing-zone (4 bytes): two NOP.N instructions.
                // If a branch lands at entry it executes NOPs then body.
                // If it lands at entry+4 it enters body directly.
                emit_nop_n(&ctx);
                emit_nop_n(&ctx);

                // Windowed ABI mapping for callx8/call8:
                // callee a2..a6 correspond to caller a10..a14.
                // espb_jit_ld_global_addr(instance, symbol_idx, v_regs, num_vregs, rd)

                // Prepare args in a10..a14 (windowed ABI) as close as possible to the call,
                // so partial entry into this block cannot skip a10=instance.

                // ИСПРАВЛЕНИЕ: НЕ сохраняем symbol_idx в a9!
                // Вместо этого загружаем все аргументы и адрес helper в правильном порядке,
                // чтобы избежать проблем с flush литерального пула.

                // a13 = num_vregs (может вызвать flush)
                if (num_vregs <= 15) emit_movi_n(&ctx, 13, (int8_t)num_vregs);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_vregs); emit_mov_n(&ctx, 13, 8); }

                // a14 = rd (может вызвать flush)
                if (rd <= 15) emit_movi_n(&ctx, 14, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 14, 8); }

                // a11 = symbol_idx (может вызвать flush если > 15)
                if (symbol_idx <= 15) {
                    emit_movi_n(&ctx, 11, (int8_t)symbol_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, symbol_idx);
                    emit_mov_n(&ctx, 11, 8);
                }

                // Загружаем адрес helper в a8 (может вызвать flush!)
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)&espb_jit_ld_global_addr);

                // После этого НЕ должно быть никаких flush'ей до callx8!
                // a12 = v_regs (from stack) - не вызывает flush
                emit_l32i(&ctx, 12, 1, 8);

                // a10 = instance (from stack) - не вызывает flush
                emit_l32i(&ctx, 10, 1, 4);

                // Вызываем helper напрямую через callx8
                emit_callx8_a8(&ctx);

                // Restore a11 back to v_regs pointer from stack
                // NOTE: Cannot use a12 here because CALL8 rotates window and a12 becomes
                // callee's a4 which may be clobbered by the callee function.
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs

                break;

            }



            case 0x1E: { // LD_GLOBAL

                // Call helper: espb_jit_ld_global(instance, global_idx, v_regs, num_vregs, rd)

                if (pc + 3 > end) { ctx.error = true; break; }  // 1 byte rd + 2 bytes global_idx

                uint8_t rd = *pc++;

                uint16_t global_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);

                pc += 2;



                // Landing-zone (4 bytes): two NOP.N instructions.

                // If a branch lands at entry it executes NOPs then body.

                // If it lands at entry+4 it enters body directly.

                emit_nop_n(&ctx);

                emit_nop_n(&ctx);



                // Windowed ABI mapping for callx8/call8:

                // callee a2..a6 correspond to caller a10..a14.

                // espb_jit_ld_global(instance, global_idx, v_regs, num_vregs, rd)



                // ИСПРАВЛЕНИЕ: Загружаем все аргументы и адрес helper в правильном порядке,
                // чтобы избежать перезаписи регистров при flush литерального пула.
                // 
                // ВАЖНО: emit_call_helper может вызвать emit_load_u32_to_a8 для загрузки адреса helper,
                // что может вызвать flush и вставить J инструкцию. Поэтому:
                // 1. Сначала загружаем ВСЕ что может вызвать flush (аргументы и адрес helper)
                // 2. Потом загружаем аргументы из стека и непосредственные значения

                // a13 = num_vregs (может вызвать flush)
                if (num_vregs <= 15) emit_movi_n(&ctx, 13, (int8_t)num_vregs);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_vregs); emit_mov_n(&ctx, 13, 8); }

                // a14 = rd (может вызвать flush)
                if (rd <= 15) emit_movi_n(&ctx, 14, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 14, 8); }

                // a11 = global_idx (может вызвать flush если > 15)
                if (global_idx <= 15) {
                    emit_movi_n(&ctx, 11, (int8_t)global_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, global_idx);
                    emit_mov_n(&ctx, 11, 8);
                }

                // Загружаем адрес helper в a8 (может вызвать flush!)
                // После этого НЕ должно быть никаких flush'ей до callx8!
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)&espb_jit_ld_global);

                // a12 = v_regs (from stack) - не вызывает flush
                emit_l32i(&ctx, 12, 1, 8);

                // a10 = instance (from stack) - не вызывает flush
                emit_l32i(&ctx, 10, 1, 4);

                // Вызываем helper напрямую через callx8 (без emit_call_helper который может вызвать flush)
                emit_callx8_a8(&ctx);

                // Restore a11 back to v_regs pointer from stack
                // NOTE: Cannot use a12 here because CALL8 rotates window and a12 becomes
                // callee's a4 which may be clobbered by the callee function.
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs
                break;
            }

            case 0x1F: { // ST_GLOBAL global_idx(u16), Rs(u8)
                // Call helper: espb_jit_st_global(instance, global_idx, v_regs, num_vregs, rs)
                if (pc + 3 > end) { ctx.error = true; break; }  // 2 bytes global_idx + 1 byte rs
                uint16_t global_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;
                uint8_t rs = *pc++;

                // Landing-zone (4 bytes): two NOP.N instructions.
                emit_nop_n(&ctx);
                emit_nop_n(&ctx);

                // Windowed ABI mapping for callx8/call8:
                // callee a2..a6 correspond to caller a10..a14.
                // espb_jit_st_global(instance, global_idx, v_regs, num_vregs, rs)

                // Precompute global_idx into a8, then preserve it in a9.
                if (global_idx <= 15) {
                    emit_movi_n(&ctx, 8, (int8_t)global_idx);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, global_idx);
                }
                emit_mov_n(&ctx, 9, 8); // a9 = global_idx (preserve)

                // a13 = num_vregs
                if (num_vregs <= 15) emit_movi_n(&ctx, 13, (int8_t)num_vregs);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_vregs); emit_mov_n(&ctx, 13, 8); }

                // a14 = rs
                if (rs <= 15) emit_movi_n(&ctx, 14, (int8_t)rs);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs); emit_mov_n(&ctx, 14, 8); }

                // a12 = v_regs (from stack)
                emit_l32i(&ctx, 12, 1, 8);

                // a11 = global_idx (from a9)
                emit_mov_n(&ctx, 11, 9);

                // a10 = instance (from stack) - LAST
                emit_l32i(&ctx, 10, 1, 4);

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_st_global);

                // Restore a11 back to v_regs pointer from stack
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs
                break;
            }

            case 0x90: { // TRUNC.I64.I32 Rd(u8), Rs(u8)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // rd (i32) = (int32_t)rs (i64)
                // In practice: take low32 and clear high32 for cleanliness.
                emit_mov_n(&ctx, 6, 11); // a6 = v_regs

                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8)); // low32
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                emit_movi_n(&ctx, 9, 0);
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x96: { // ZEXT.I8.I16 Rd, Rs (zero-extend 8->16)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Inline zero-extend: v_regs[rd].i16 = (uint8_t)v_regs[rs].i32
                emit_mov_n(&ctx, 6, 11);

                // Load v_regs[rs].i32 (low word) into a8
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));

                // Zero-extend to 16 bits
                emit_extui(&ctx, 8, 8, 0, 8);

                // Store result to v_regs[rd].i32 (low word)
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));

                // Store type = ESPB_TYPE_I16 (value 2)
                emit_movi_n(&ctx, 9, 2);
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x97: { // ZEXT.I8.I32 Rd, Rs (zero-extend 8->32)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Inline zero-extend: v_regs[rd].i32 = (uint8_t)v_regs[rs].i32
                // a6 = v_regs
                emit_mov_n(&ctx, 6, 11);
                
                // Load v_regs[rs].i32 (low word) into a8
                emit_l32i(&ctx, 8, 6, (uint16_t)(rs * 8));
                
                // Zero-extend: AND with 0xFF using EXTUI instruction
                // EXTUI at, as, shiftimm, maskimm - extract unsigned immediate
                // EXTUI a8, a8, 0, 8 - extract bits [7:0] with zero-extension
                emit_extui(&ctx, 8, 8, 0, 8);
                
                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                
                // Store type = ESPB_TYPE_I32 (value 1)
                emit_movi_n(&ctx, 9, 1);
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));
                
                emit_mov_n(&ctx, 11, 6);  // restore a11 = v_regs
                break;
            }

            case 0x8E: { // ADDR_OF Rd, Rs - get address of virtual register
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // v_regs[rd].ptr = &v_regs[rs]
                // a11 = v_regs pointer
                // Calculate address of v_regs[rs]: v_regs + rs*8
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                
                uint32_t rs_offset = rs * 8;
                if (rs_offset <= 255) {
                    emit_addi(&ctx, 8, 6, (int8_t)rs_offset);  // a8 = v_regs + rs*8
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rs_offset);
                    emit_add_n(&ctx, 8, 6, 8);  // a8 = v_regs + rs*8
                }
                
                // Store the address to v_regs[rd].ptr (low 32 bits)
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                
                // Clear high 32 bits of v_regs[rd]
                emit_movi_n(&ctx, 9, 0);
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));
                
                emit_mov_n(&ctx, 11, 6);  // restore a11 = v_regs
                break;
            }

            case 0x8F: { // ALLOCA: Rd(u8), Rs(u8), align(u8)
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs_size = *pc++;
                uint8_t align = *pc++;

                // Call helper: espb_runtime_alloca(instance, exec_ctx, regs, num_vregs, rd, rs_size, align)
                // NOTE: current JIT entrypoint signature doesn't provide ExecutionContext, so we pass NULL.

                // a10 = instance
                emit_l32i(&ctx, 8, 1, 4);
                emit_mov_n(&ctx, 10, 8);

                // a11 = exec_ctx (NULL)
                emit_movi_n(&ctx, 11, 0);

                // a12 = regs (v_regs)
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 12, 8);

                // a13 = num_vregs
                if (num_vregs <= 15) emit_movi_n(&ctx, 13, (int8_t)num_vregs);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_vregs); emit_mov_n(&ctx, 13, 8); }

                // a14 = rd
                if (rd <= 15) emit_movi_n(&ctx, 14, (int8_t)rd);
                else { emit_load_u32_to_a8(&ctx, &litpool, rd); emit_mov_n(&ctx, 14, 8); }

                // a15 = rs_size
                if (rs_size <= 15) emit_movi_n(&ctx, 15, (int8_t)rs_size);
                else { emit_load_u32_to_a8(&ctx, &litpool, rs_size); emit_mov_n(&ctx, 15, 8); }

                // 7th arg: align on stack at a1+0
                if (align <= 15) emit_movi_n(&ctx, 8, (int8_t)align);
                else { emit_load_u32_to_a8(&ctx, &litpool, align); }
                emit_s32i(&ctx, 8, 1, 0);

                emit_call_helper(&ctx, &litpool, (void*)&espb_runtime_alloca);

                // Restore a11 back to v_regs pointer from stack
                // NOTE: Cannot use a12 here because CALL8 rotates window and a12 becomes
                // callee's a4 which may be clobbered by the callee function.
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs
                break;
            }

            case 0x09: { // CALL_IMPORT
                // DEBUG: show where this call is located in bytecode and native code
                // (useful for verifying control-flow reaches the expected callsite on loop back-edges)
                
                // Minimal implementation: supports non-variadic (no 0xAA) with fixed num_args from signature.
                // Variadic (0xAA) will be added after stack arg_types placement is finalized.
                if (pc + 2 > end) { ctx.error = true; break; }
                uint16_t import_idx = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
                pc += 2;

                uint8_t has_var = 0;
                uint8_t num_args = 0;
                uint8_t arg_types_u8[16] = {0};

                if (pc < end && *pc == 0xAA) {
                    has_var = 1;
                    pc++;
                    if (pc >= end) { ctx.error = true; break; }
                    num_args = *pc++;
                    if (num_args > 16 || pc + num_args > end) { ctx.error = true; break; }
                    for (uint8_t i = 0; i < num_args; i++) {
                        arg_types_u8[i] = *pc++;
                    }
                } else {
                    // read num_args from signature
                    if (import_idx < instance->module->num_imports) {
                        const EspbImportDesc* imp = &instance->module->imports[import_idx];
                        if (imp->kind == ESPB_IMPORT_KIND_FUNC) {
                            uint16_t sig_idx = imp->desc.func.type_idx;
                            if (sig_idx < instance->module->num_signatures) {
                                num_args = instance->module->signatures[sig_idx].num_params;
                            }
                        }
                    }
                }

                // Prepare args for callx8 mapping (callee a2..a7 <= caller a10..a15)
                // espb_jit_call_import(instance, import_idx, v_regs, num_vregs, has_var, num_args, arg_types_ptr)

                // a10 = instance
                emit_l32i(&ctx, 8, 1, 4);
                emit_mov_n(&ctx, 10, 8);

                // a11 = import_idx
                if (import_idx <= 15) emit_movi_n(&ctx, 11, (int8_t)import_idx);
                else { emit_load_u32_to_a8(&ctx, &litpool, import_idx); emit_mov_n(&ctx, 11, 8); }

                // a12 = v_regs
                emit_l32i(&ctx, 8, 1, 8);
                emit_mov_n(&ctx, 12, 8);

                // a13 = num_vregs
                if (num_vregs <= 15) emit_movi_n(&ctx, 13, (int8_t)num_vregs);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_vregs); emit_mov_n(&ctx, 13, 8); }

                // a14 = has_var
                if (has_var <= 15) emit_movi_n(&ctx, 14, (int8_t)has_var);
                else { emit_load_u32_to_a8(&ctx, &litpool, has_var); emit_mov_n(&ctx, 14, 8); }

                // a15 = num_args
                if (num_args <= 15) emit_movi_n(&ctx, 15, (int8_t)num_args);
                else { emit_load_u32_to_a8(&ctx, &litpool, num_args); emit_mov_n(&ctx, 15, 8); }

                // 7th arg (arg_types_ptr) on stack.
                // Verified by compiler output: 7th arg is placed at a1+0.
                if (has_var) {
                    // Write arg_types_u8[] to frame at a1+16+i
                    // We have verified s8i a8,a1,16 and addi a8,a1,16 patterns.
                    // We'll use a9 as a running pointer (a9 = a1+16).
                    emit_addi_a8_a1_16(&ctx);      // a8 = a1+16
                    emit_mov_n(&ctx, 9, 8);        // a9 = a8

                    for (uint8_t i = 0; i < num_args && i < 16; i++) {
                        // a8 = arg_types_u8[i] (0..15 expected)
                        emit_movi_n(&ctx, 8, (int8_t)arg_types_u8[i]);
                        emit_s8i(&ctx, 8, 1, (uint16_t)(16 + i));
                    }

                    // arg_types_ptr = a1+16 -> pass as 7th arg via a1+0
                    emit_addi_a8_a1_16(&ctx);
                    emit_s32i(&ctx, 8, 1, 0);
                } else {
                    emit_movi_n(&ctx, 8, 0);
                    emit_s32i(&ctx, 8, 1, 0);
                }

                emit_call_helper(&ctx, &litpool, (void*)&espb_jit_call_import);

                // Restore a11 back to v_regs pointer from stack
                // NOTE: Cannot use a12 here because CALL8 rotates window and a12 becomes
                // callee's a4 which may be clobbered by the callee function.
                emit_l32i(&ctx, 11, 1, 8);  // a11 = [a1+8] = v_regs
                break;
            }

            // ===== F64 Arithmetic Operations =====
            // Format: [opcode][Rd][R1][R2] - 4 bytes
            // Operates on 64-bit IEEE-754 doubles stored as raw bits in v_regs
            case 0x68:  // ADD.F64: Rd = R1 + R2
            case 0x69:  // SUB.F64: Rd = R1 - R2
            case 0x6A:  // MUL.F64: Rd = R1 * R2
            case 0x6B:  // DIV.F64: Rd = R1 / R2
            {
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Select helper function
                void* helper = NULL;
                switch (op) {
                    case 0x68: helper = (void*)jit_helper_fadd_f64_bits; break;
                    case 0x69: helper = (void*)jit_helper_fsub_f64_bits; break;
                    case 0x6A: helper = (void*)jit_helper_fmul_f64_bits; break;
                    case 0x6B: helper = (void*)jit_helper_fdiv_f64_bits; break;
                    default: ctx.error = true; break;
                }
                if (!helper) { ctx.error = true; break; }
                
                // Save v_regs pointer (a11) across windowed call in a6 (maps to callee a14, callee-saved)
                emit_mov_n(&ctx, 6, 11);  // a6 = v_regs
                
                // Windowed ABI: callx8 rotates window by 8.
                // Caller's a10:a11 become callee's a2:a3 (first u64 arg)
                // Caller's a12:a13 become callee's a4:a5 (second u64 arg)
                
                // Load rs1 into a10:a11 (64-bit value, lo in a10, hi in a11)
                emit_l32i(&ctx, 10, 6, (uint16_t)(rs1 * 8));      // a10 = v_regs[rs1].lo (use a6 as base)
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs1 * 8 + 4));  // a11 = v_regs[rs1].hi
                
                // Load rs2 into a12:a13 (64-bit value, lo in a12, hi in a13)
                emit_l32i(&ctx, 12, 6, (uint16_t)(rs2 * 8));      // a12 = v_regs[rs2].lo
                emit_l32i(&ctx, 13, 6, (uint16_t)(rs2 * 8 + 4));  // a13 = v_regs[rs2].hi
                
                // Call helper: uint64_t result = helper(a_bits, b_bits)
                // Args: caller a10:a11 -> callee a2:a3 (first u64)
                //       caller a12:a13 -> callee a4:a5 (second u64)
                // Returns: callee a2:a3 -> caller a10:a11 (result u64)
                emit_call_helper(&ctx, &litpool, helper);
                
                // After windowed call returns:
                // - Result is in caller's a10:a11
                // - v_regs pointer is still in a6
                
                // Store result (a10:a11) to rd using a6 as v_regs base
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));       // v_regs[rd].lo = a10
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));   // v_regs[rd].hi = a11
                
                // Restore v_regs pointer to a11 for subsequent opcodes
                emit_mov_n(&ctx, 11, 6);  // a11 = v_regs
                break;
            }

            case 0x6C:  // MIN.F64: Rd = fmin(R1, R2)
            case 0x6D:  // MAX.F64: Rd = fmax(R1, R2)
            {
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                // Use store-helper: helper(v_regs, rd, rs1, rs2)
                // Args: a10=v_regs, a11=rd, a12=rs1, a13=rs2
                void* helper = (op == 0x6C) ? (void*)jit_helper_fmin_f64_store : (void*)jit_helper_fmax_f64_store;
                
                // Save v_regs to a6 FIRST before we overwrite a11
                emit_mov_n(&ctx, 6, 11);
                
                emit_mov_n(&ctx, 10, 11);   // a10 = v_regs
                
                // ИСПРАВЛЕНИЕ: emit_movi_n работает только для значений 0-15 (4-bit immediate)
                // Для rd, rs1, rs2 > 15 нужно использовать emit_load_u32_to_reg
                if (rd <= 15) {
                    emit_movi_n(&ctx, 11, rd);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rd);
                    emit_mov_n(&ctx, 11, 8);
                }
                
                if (rs1 <= 15) {
                    emit_movi_n(&ctx, 12, rs1);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rs1);
                    emit_mov_n(&ctx, 12, 8);
                }
                
                if (rs2 <= 15) {
                    emit_movi_n(&ctx, 13, rs2);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rs2);
                    emit_mov_n(&ctx, 13, 8);
                }
                
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                emit_callx8_a8(&ctx);
                
                // Restore v_regs pointer to a11
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x6E:  // ABS.F64: Rd = fabs(R1) - via helper with logging
            {
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Use store-helper which logs input/output
                // Args: a10=v_regs, a11=rd, a12=rs
                
                // Save v_regs to a6 FIRST before we overwrite a11
                emit_mov_n(&ctx, 6, 11);
                
                emit_mov_n(&ctx, 10, 11);   // a10 = v_regs
                
                // ИСПРАВЛЕНИЕ: emit_movi_n работает только для значений 0-15 (4-bit immediate)
                // Для rd, rs > 15 нужно использовать emit_load_u32_to_reg
                if (rd <= 15) {
                    emit_movi_n(&ctx, 11, rd);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rd);
                    emit_mov_n(&ctx, 11, 8);
                }
                
                if (rs <= 15) {
                    emit_movi_n(&ctx, 12, rs);
                } else {
                    emit_load_u32_to_a8(&ctx, &litpool, rs);
                    emit_mov_n(&ctx, 12, 8);
                }
                
                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)jit_helper_fabs_f64_store);
                emit_callx8_a8(&ctx);
                
                // Restore v_regs pointer to a11
                emit_mov_n(&ctx, 11, 6);
                
                break;
            }
            
            case 0x6F:  // SQRT.F64: Rd = sqrt(R1)
            {
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;

                void* helper = (void*)jit_helper_fsqrt_f64_bits;

                emit_s32i(&ctx, 11, 1, 48);
                emit_l32i(&ctx, 10, 11, (uint16_t)(rs1 * 8));
                emit_l32i(&ctx, 11, 11, (uint16_t)(rs1 * 8 + 4));

                emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)helper);
                emit_callx8_a8(&ctx);

                emit_l32i(&ctx, 12, 1, 4);
                emit_s32i(&ctx, 10, 12, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 12, (uint16_t)(rd * 8 + 4));
                emit_mov_n(&ctx, 11, 12);
                break;
            }

            case 0x70: // STORE.I8 Rs(u8), Ra(u8), offset(i16)
            case 0x71: { // STORE.U8 Rs(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // a6 = v_regs (save for restore)
                emit_mov_n(&ctx, 6, 11);
                // a8 = base pointer from v_regs[ra]
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                // a8 += off16
                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        // FIX: save base address before loading offset
                        emit_mov_n(&ctx, 7, 8);  // a7 = base address
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_add_n(&ctx, 8, 7, 8);  // a8 = base + offset
                    }
                }

                // a9 = value from v_regs[rs]
                emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                // Store byte
                emit_s8i(&ctx, 9, 8, 0);
                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x72: // STORE.I16 Rs(u8), Ra(u8), offset(i16) - unaligned-safe INLINE
            case 0x73: { // STORE.U16 Rs(u8), Ra(u8), offset(i16) - unaligned-safe INLINE
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // a6 = v_regs (save for restore)
                emit_mov_n(&ctx, 6, 11);
                // a8 = base pointer from v_regs[ra]
                emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));

                // a8 += off16
                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 8, 8, (int8_t)off16);
                    } else {
                        // Save base address before loading offset
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_mov_n(&ctx, 10, 8);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(ra * 8));
                        emit_add_n(&ctx, 8, 8, 10);
                    }
                }

                // a9 = value from v_regs[rs]
                emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                // Store 16-bit value using byte stores (unaligned-safe, little-endian)
                // s8i a9, a8, 0  - store low byte
                emit_s8i(&ctx, 9, 8, 0);
                // srli a10, a9, 8  - a10 = value >> 8 (high byte)
                emit_srli(&ctx, 10, 9, 8);
                // s8i a10, a8, 1  - store high byte
                emit_s8i(&ctx, 10, 8, 1);
                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0x7B: { // STORE.BOOL Rs(u8), Ra(u8), offset(i16)
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t off16;
                memcpy(&off16, pc, sizeof(off16));
                pc += sizeof(off16);

                // a6 = v_regs (save for restore)
                emit_mov_n(&ctx, 6, 11);
                // a7 = base pointer from v_regs[ra] (use a7 to preserve across normalization)
                emit_l32i(&ctx, 7, 6, (uint16_t)(ra * 8));

                // a7 += off16
                if (off16 != 0) {
                    if (off16 >= -128 && off16 <= 127) {
                        emit_addi(&ctx, 7, 7, (int8_t)off16);
                    } else {
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(int32_t)off16);
                        emit_add_n(&ctx, 7, 7, 8);  // a7 = base + offset
                    }
                }

                // a9 = value from v_regs[rs]
                emit_l32i(&ctx, 9, 6, (uint16_t)(rs * 8));
                // normalize to 0/1: if a9==0 -> 0, else 1
                emit_mov_n(&ctx, 8, 9); // a8 = value for beqz check
                emit_movi_n(&ctx, 10, 0);
                uint32_t br_zero = emit_beqz_n_a8_placeholder(&ctx);
                emit_movi_n(&ctx, 10, 1);
                uint32_t end_pos = (uint32_t)ctx.offset;

                // Store normalized bool (a10) to address in a7
                emit_s8i(&ctx, 10, 7, 0);
                // restore v_regs pointer
                emit_mov_n(&ctx, 11, 6);

                emit_flush_words(&ctx);
                {
                    uint32_t pc_after = br_zero + 2u;
                    int32_t delta = (int32_t)end_pos - (int32_t)pc_after;
                    patch_beqz_n_a8_at(ctx.buffer, br_zero, delta);
                }
                break;
            }

            case 0xFC: { // PREFIX: Extended Ops
                if (pc >= end) { ctx.error = true; break; }
                uint8_t ext_opcode = *pc++;

                switch (ext_opcode) {
                    case 0x00: { // MEMORY.INIT data_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
                        if (pc + 7 > end) { ctx.error = true; break; }
                        uint32_t data_seg_idx;
                        memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;
                        uint8_t rn = *pc++;

                        void* helper = (void*)jit_helper_memory_init;

                        // Preserve v_regs in a6
                        emit_mov_n(&ctx, 6, 11);

                        // a10 = instance
                        emit_l32i(&ctx, 10, 1, 4);
                        // a11 = data_seg_idx
                        emit_load_u32_to_a8(&ctx, &litpool, data_seg_idx);
                        emit_mov_n(&ctx, 11, 8);
                        // a12 = dest_addr (v_regs[rd].i32)
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rd * 8));
                        // a13 = src_offset (v_regs[rs].i32)
                        emit_l32i(&ctx, 13, 6, (uint16_t)(rs * 8));
                        // a14 = size (v_regs[rn].i32)
                        emit_l32i(&ctx, 14, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, helper);

                        // Restore v_regs pointer
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x01: { // DATA.DROP data_seg_idx(u32)
                        if (pc + 4 > end) { ctx.error = true; break; }
                        uint32_t data_seg_idx;
                        memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);

                        void* helper = (void*)jit_helper_data_drop;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4); // a10 = instance
                        emit_load_u32_to_a8(&ctx, &litpool, data_seg_idx);
                        emit_mov_n(&ctx, 11, 8);

                        emit_call_helper(&ctx, &litpool, helper);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x05: { // ELEM.DROP elem_seg_idx(u32)
                        if (pc + 4 > end) { ctx.error = true; break; }
                        pc += 4;
                        break;
                    }

                    case 0x04: { // TABLE.INIT table_idx(u8), elem_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
                        if (pc + 8 > end) { ctx.error = true; break; }
                        uint8_t table_idx = *pc++;
                        uint32_t elem_seg_idx;
                        memcpy(&elem_seg_idx, pc, sizeof(elem_seg_idx)); pc += sizeof(elem_seg_idx);
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;
                        uint8_t rn = *pc++;

                        void* helper = (void*)jit_helper_table_init;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4); // a10 = instance
                        emit_load_u32_to_a8(&ctx, &litpool, table_idx);
                        emit_mov_n(&ctx, 11, 8);
                        emit_load_u32_to_a8(&ctx, &litpool, elem_seg_idx);
                        emit_mov_n(&ctx, 12, 8);
                        emit_l32i(&ctx, 13, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 14, 6, (uint16_t)(rs * 8));
                        emit_l32i(&ctx, 15, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, helper);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x03: { // MEMORY.FILL Rd(u8), Rval(u8), Rn(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t rval = *pc++;
                        uint8_t rn = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 11, 6, (uint16_t)(rval * 8));
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, (void*)&memset);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x02: { // MEMORY.COPY Rd(u8), Rs(u8), Rn(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;
                        uint8_t rn = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8));
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, (void*)&memmove);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x06: { // HEAP_REALLOC Rd(u8), Rptr(u8), Rsize(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t rptr = *pc++;
                        uint8_t rsize = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4); // instance
                        emit_l32i(&ctx, 11, 6, (uint16_t)(rptr * 8));
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rsize * 8));

                        emit_call_helper(&ctx, &litpool, (void*)&espb_heap_realloc);

                        emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_s32i(&ctx, 0, 6, (uint16_t)(rd * 8 + 4));
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x09: { // HEAP_CALLOC Rd(u8), Rcount(u8), Rsize(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t rcount = *pc++;
                        uint8_t rsize = *pc++;

                        // total = rcount * rsize (use 0x22 mull sequence from MUL.I32)
                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 8, 6, (uint16_t)(rcount * 8));
                        emit_l32i(&ctx, 9, 6, (uint16_t)(rsize * 8));
                        emit_u8(&ctx, 0x90);  // (at << 4) | 0, at=a9
                        emit_u8(&ctx, 0x88);  // (as << 4) | ar, as=a8, ar=a8
                        emit_u8(&ctx, 0x82);  // (op2 << 4) | 0x2, op2=0x8 (mull)

                        // espb_heap_malloc(instance, total)
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_mov_n(&ctx, 11, 8);
                        emit_call_helper(&ctx, &litpool, (void*)&espb_heap_malloc);

                        // memset(ptr, 0, total)
                        emit_mov_n(&ctx, 12, 8); // save total in a12
                        emit_mov_n(&ctx, 8, 10); // save ptr in a8
                        emit_mov_n(&ctx, 10, 8);
                        emit_movi_n(&ctx, 11, 0);
                        emit_call_helper(&ctx, &litpool, (void*)&memset);

                        // store result pointer from a8
                        emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                        emit_s32i(&ctx, 0, 6, (uint16_t)(rd * 8 + 4));
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x16: { // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
                        if (pc + 5 > end) { ctx.error = true; break; }
                        uint8_t dst_table_idx = *pc++;
                        uint8_t src_table_idx = *pc++;
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;
                        uint8_t rn = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_load_u32_to_a8(&ctx, &litpool, dst_table_idx);
                        emit_mov_n(&ctx, 11, 8);
                        emit_load_u32_to_a8(&ctx, &litpool, src_table_idx);
                        emit_mov_n(&ctx, 12, 8);
                        emit_l32i(&ctx, 13, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 14, 6, (uint16_t)(rs * 8));
                        emit_l32i(&ctx, 15, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, (void*)jit_helper_table_copy);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x17: { // TABLE.FILL table_idx(u8), Rd(u8), Rval(u8), Rn(u8)
                        if (pc + 4 > end) { ctx.error = true; break; }
                        uint8_t table_idx = *pc++;
                        uint8_t rd = *pc++;
                        uint8_t rval = *pc++;
                        uint8_t rn = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_load_u32_to_a8(&ctx, &litpool, table_idx);
                        emit_mov_n(&ctx, 11, 8);
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 13, 6, (uint16_t)(rval * 8));
                        emit_l32i(&ctx, 14, 6, (uint16_t)(rn * 8));

                        emit_call_helper(&ctx, &litpool, (void*)jit_helper_table_fill);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x08: { // TABLE.SIZE Rd(u8), table_idx(u8)
                        if (pc + 2 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t table_idx = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_call_helper(&ctx, &litpool, (void*)jit_helper_table_size);

                        emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_s32i(&ctx, 0, 6, (uint16_t)(rd * 8 + 4));
                        emit_mov_n(&ctx, 11, 6);
                        (void)table_idx;
                        break;
                    }

                    case 0x07: { // HEAP_FREE Rp(u8)
                        if (pc + 1 > end) { ctx.error = true; break; }
                        uint8_t rp = *pc++;

                        // CRITICAL: Pre-load helper address BEFORE setting up arguments.
                        // emit_load_u32_to_a8 may trigger flush_literal_pool which inserts
                        // a JUMP instruction. If this happens after argument setup, the 
                        // argument load instructions get jumped over.
                        emit_load_u32_to_a8(&ctx, &litpool, (uint32_t)(uintptr_t)&jit_helper_heap_free);
                        emit_mov_n(&ctx, 7, 8);              // a7 = helper address (preserve)
                        
                        // Now setup arguments - no flush can happen here
                        emit_l32i(&ctx, 6, 1, 8);            // a6 = v_regs (from stack)
                        emit_l32i(&ctx, 10, 1, 4);           // a10 = instance
                        emit_l32i(&ctx, 11, 6, (uint16_t)(rp * 8)); // a11 = ptr
                        
                        // Call using preserved address
                        emit_mov_n(&ctx, 8, 7);              // a8 = helper address
                        emit_callx8_a8(&ctx);
                        
                        // Restore a11 = v_regs
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x18: { // TABLE.GET Rd(u8), table_idx(u8), Rs(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t table_idx = *pc++;
                        uint8_t rs = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_load_u32_to_a8(&ctx, &litpool, table_idx);
                        emit_mov_n(&ctx, 11, 8);
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rs * 8));
                        emit_call_helper(&ctx, &litpool, (void*)jit_helper_table_get);

                        emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_s32i(&ctx, 0, 6, (uint16_t)(rd * 8 + 4));
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x19: { // TABLE.SET table_idx(u8), Rd(u8), Rval(u8)
                        if (pc + 3 > end) { ctx.error = true; break; }
                        uint8_t table_idx = *pc++;
                        uint8_t rd = *pc++;
                        uint8_t rval = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_load_u32_to_a8(&ctx, &litpool, table_idx);
                        emit_mov_n(&ctx, 11, 8);
                        emit_l32i(&ctx, 12, 6, (uint16_t)(rd * 8));
                        emit_l32i(&ctx, 13, 6, (uint16_t)(rval * 8));
                        emit_call_helper(&ctx, &litpool, (void*)jit_helper_table_set);
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    case 0x0B: { // HEAP_MALLOC Rd(u8), Rs(u8)
                        if (pc + 2 > end) { ctx.error = true; break; }
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;

                        emit_mov_n(&ctx, 6, 11);
                        emit_l32i(&ctx, 10, 1, 4);
                        emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8));
                        emit_call_helper(&ctx, &litpool, (void*)&espb_heap_malloc);

                        // Store result (in a10) to v_regs[rd]
                        emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                        emit_s32i(&ctx, 0, 6, (uint16_t)(rd * 8 + 4));
                        emit_mov_n(&ctx, 11, 6);
                        break;
                    }

                    default:
                        ESP_LOGW(TAG, "Unsupported extended opcode 0xFC 0x%02X at offset %zu", ext_opcode, (size_t)(pc - start - 2));
                        ctx.error = true;
                        break;
                }
                break;
            }

            // ===== Pointer conversion operations =====
            case 0xBC: { // PTRTOINT Rd(u8), Rs(u8) - Convert PTR to I32
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // PTRTOINT simply copies the 32-bit pointer value to I32
                // Load lower 32 bits (pointer) from v_regs[rs]
                emit_l32i(&ctx, 8, 11, (uint16_t)(rs * 8));   // a8 = v_regs[rs].ptr
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8));   // v_regs[rd] = a8

                // Clear upper 32 bits
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 11, (uint16_t)(rd * 8 + 4));
                break;
            }

            // ===== ATOMIC I32 Operations (0xD7-0xDF) =====
            case 0xD7:   // ATOMIC.RMW.ADD.I32 Rd, Ra, Rv
            case 0xD8:   // ATOMIC.RMW.SUB.I32 Rd, Ra, Rv
            case 0xD9:   // ATOMIC.RMW.AND.I32 Rd, Ra, Rv
            case 0xDA:   // ATOMIC.RMW.OR.I32 Rd, Ra, Rv
            case 0xDB:   // ATOMIC.RMW.XOR.I32 Rd, Ra, Rv
            case 0xDC: { // ATOMIC.RMW.XCHG.I32 Rd, Ra, Rv
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rv = *pc++;

                // Select wrapper based on opcode
                void* helper = NULL;
                switch (op) {
                    case 0xD7: helper = (void*)jit_xtensa_atomic_fetch_add_4; break;
                    case 0xD8: helper = (void*)jit_xtensa_atomic_fetch_sub_4; break;
                    case 0xD9: helper = (void*)jit_xtensa_atomic_fetch_and_4; break;
                    case 0xDA: helper = (void*)jit_xtensa_atomic_fetch_or_4; break;
                    case 0xDB: helper = (void*)jit_xtensa_atomic_fetch_xor_4; break;
                    case 0xDC: helper = (void*)jit_xtensa_atomic_exchange_4; break;
                }

                // Preserve v_regs in a6
                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // a11 = value (v_regs[rv].i32)
                emit_l32i(&ctx, 11, 6, (uint16_t)(rv * 8));

                // Call wrapper: old_val = wrapper(addr, val)
                emit_call_helper(&ctx, &litpool, helper);

                // Store result (old value in a10) to v_regs[rd].i32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8 + 4));  // Clear high word

                // Restore v_regs
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xDD: { // ATOMIC.RMW.CMPXCHG.I32 Rd, Ra, Rexp, Rdes
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rexp = *pc++;
                uint8_t rdes = *pc++;

                // Preserve v_regs in a6
                emit_mov_n(&ctx, 6, 11);

                // For cmpxchg we need to pass expected as pointer, so store it on stack first
                emit_l32i(&ctx, 8, 6, (uint16_t)(rexp * 8));   // a8 = expected value
                emit_s32i(&ctx, 8, 1, 0);                       // store to [sp+0]

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // a11 = pointer to expected on stack
                emit_mov_n(&ctx, 11, 1);
                // a12 = desired (v_regs[rdes].i32)
                emit_l32i(&ctx, 12, 6, (uint16_t)(rdes * 8));

                // Call wrapper: bool jit_xtensa_atomic_compare_exchange_4(ptr, expected*, desired)
                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_compare_exchange_4);

                // Load the old/current value from stack to store in rd
                emit_l32i(&ctx, 8, 1, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xDE: { // ATOMIC.LOAD.I32 Rd, Ra
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;

                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));

                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_load_4);

                // Store result to v_regs[rd].i32
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_movi_n(&ctx, 8, 0);
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xDF: { // ATOMIC.STORE.I32 Rs(value), Ra(addr)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;  // value register (first in bytecode)
                uint8_t ra = *pc++;  // address register (second in bytecode)

                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // a11 = value (v_regs[rs].i32)
                emit_l32i(&ctx, 11, 6, (uint16_t)(rs * 8));

                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_store_4);

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            // ===== ATOMIC I64 Operations (0xEC-0xF6) =====
            case 0xEC: { // ATOMIC.LOAD.I64 Rd, Ra
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;

                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));

                // Call wrapper: uint64_t jit_xtensa_atomic_load_8(volatile void* ptr)
                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_load_8);

                // Store 64-bit result (a10:a11) to v_regs[rd]
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));      // low 32
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));  // high 32

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xED: { // ATOMIC.STORE.I64 Rs(value), Ra(addr)
                if (pc + 2 > end) { ctx.error = true; break; }
                uint8_t rs = *pc++;  // value register (first in bytecode)
                uint8_t ra = *pc++;  // address register (second in bytecode)

                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // For Xtensa windowed ABI, 64-bit arg after 32-bit ptr must be aligned to even register pair
                // Since addr is in a10, the 64-bit value goes in a12:a13 (skipping a11)
                emit_l32i(&ctx, 12, 6, (uint16_t)(rs * 8));      // low 32
                emit_l32i(&ctx, 13, 6, (uint16_t)(rs * 8 + 4));  // high 32

                // Call wrapper: void jit_xtensa_atomic_store_8(volatile void* ptr, uint64_t val)
                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_store_8);

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xEE: { // ATOMIC.FENCE
                emit_mov_n(&ctx, 6, 11);
                emit_call_helper(&ctx, &litpool, (void*)jit_helper_atomic_fence);
                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xF0:   // ATOMIC.RMW.ADD.I64 Rd, Ra, Rv
            case 0xF1:   // ATOMIC.RMW.SUB.I64 Rd, Ra, Rv
            case 0xF2:   // ATOMIC.RMW.AND.I64 Rd, Ra, Rv
            case 0xF3:   // ATOMIC.RMW.OR.I64 Rd, Ra, Rv
            case 0xF4:   // ATOMIC.RMW.XOR.I64 Rd, Ra, Rv
            case 0xF5: { // ATOMIC.RMW.XCHG.I64 Rd, Ra, Rv
                if (pc + 3 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rv = *pc++;

                // Use wrapper functions for correct ABI
                void* helper = NULL;
                switch (op) {
                    case 0xF0: helper = (void*)jit_xtensa_atomic_fetch_add_8; break;
                    case 0xF1: helper = (void*)jit_xtensa_atomic_fetch_sub_8; break;
                    case 0xF2: helper = (void*)jit_xtensa_atomic_fetch_and_8; break;
                    case 0xF3: helper = (void*)jit_xtensa_atomic_fetch_or_8; break;
                    case 0xF4: helper = (void*)jit_xtensa_atomic_fetch_xor_8; break;
                    case 0xF5: helper = (void*)jit_xtensa_atomic_exchange_8; break;
                }

                emit_mov_n(&ctx, 6, 11);

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // For Xtensa windowed ABI, 64-bit arg after 32-bit ptr must be aligned to even register pair
                // Since addr is in a10, the 64-bit value goes in a12:a13 (skipping a11)
                emit_l32i(&ctx, 12, 6, (uint16_t)(rv * 8));
                emit_l32i(&ctx, 13, 6, (uint16_t)(rv * 8 + 4));

                emit_call_helper(&ctx, &litpool, helper);

                // Store 64-bit result to v_regs[rd]
                emit_s32i(&ctx, 10, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 11, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            case 0xF6: { // ATOMIC.RMW.CMPXCHG.I64 Rd, Ra, Rexp, Rdes
                if (pc + 4 > end) { ctx.error = true; break; }
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rexp = *pc++;
                uint8_t rdes = *pc++;

                emit_mov_n(&ctx, 6, 11);

                // For cmpxchg we need to pass expected as pointer, so store it on stack first
                // Stack layout: [a1+0..7] = expected value (8 bytes)
                
                // Load expected value to temp regs and store to stack
                emit_l32i(&ctx, 8, 6, (uint16_t)(rexp * 8));       // a8 = expected low
                emit_l32i(&ctx, 9, 6, (uint16_t)(rexp * 8 + 4));   // a9 = expected high
                emit_s32i(&ctx, 8, 1, 0);                           // store low to [sp+0]
                emit_s32i(&ctx, 9, 1, 4);                           // store high to [sp+4]

                // a10 = address (v_regs[ra].ptr)
                emit_l32i(&ctx, 10, 6, (uint16_t)(ra * 8));
                // a11 = pointer to expected on stack
                emit_mov_n(&ctx, 11, 1);  // a11 = sp (points to expected)
                // For Xtensa windowed ABI, 64-bit arg must be aligned to even register pair
                // a12:a13 = desired (v_regs[rdes].i64)
                emit_l32i(&ctx, 12, 6, (uint16_t)(rdes * 8));
                emit_l32i(&ctx, 13, 6, (uint16_t)(rdes * 8 + 4));

                // Call wrapper: bool jit_xtensa_atomic_compare_exchange_8(ptr, expected*, desired)
                emit_call_helper(&ctx, &litpool, (void*)jit_xtensa_atomic_compare_exchange_8);

                // Result: expected value (possibly updated) is on stack, return value in a10 is success/fail
                // Load the old value from stack to store in rd
                emit_l32i(&ctx, 8, 1, 0);   // low from stack
                emit_l32i(&ctx, 9, 1, 4);   // high from stack
                emit_s32i(&ctx, 8, 6, (uint16_t)(rd * 8));
                emit_s32i(&ctx, 9, 6, (uint16_t)(rd * 8 + 4));

                emit_mov_n(&ctx, 11, 6);
                break;
            }

            default:
                ESP_LOGW(TAG, "Unsupported opcode 0x%02X at offset %zu", op, (size_t)(pc - start - 1));
                ctx.error = true;
                break;
        }
    }

    // Ensure all bytes are committed before final fixups
    emit_flush_words(&ctx);

    // Patch forward branches
    // Epilogue: Windowed ABI - emit BEFORE patching fixups so we know epilogue address
    emit_align4_with_nops(&ctx);
    uint32_t epilogue_native = (uint32_t)ctx.offset;
    emit_retw(&ctx);
    
    // Also record epilogue address in bc_to_native for target_bc == code_size
    bc_to_native[code_size] = epilogue_native;

    // Patch forward branch fixups
    for (uint32_t i = 0; i < fixup_count && !ctx.error; i++) {
        uint32_t tgt_bc = fixups[i].target_bc_off;
        uint32_t j_pos = fixups[i].j_pos_native;
        
        uint32_t tgt_native;
        if (tgt_bc == code_size) {
            // Special case: jump to epilogue (from END opcode)
            tgt_native = epilogue_native;
        } else if (tgt_bc > code_size || bc_to_native[tgt_bc] == XTENSA_BC_UNSET) {
            // Find nearest valid bc offsets for debugging
            uint32_t nearest_before = 0, nearest_after = code_size;
            for (uint32_t k = 0; k < code_size; k++) {
                if (bc_to_native[k] != XTENSA_BC_UNSET) {
                    if (k < tgt_bc && k > nearest_before) nearest_before = k;
                    if (k > tgt_bc && k < nearest_after) { nearest_after = k; break; }
                }
            }
            ESP_LOGE(TAG, "BR patch: unresolved target bc=%u (j_pos=%u, nearest_before=%u, nearest_after=%u)", 
                     (unsigned)tgt_bc, (unsigned)j_pos, (unsigned)nearest_before, (unsigned)nearest_after);
            
            // WORKAROUND: For dead code branches with invalid targets, generate an infinite loop (trap)
            // This is safer than jumping to a wrong location which could corrupt state
            // Dead code should never execute, so if it does, we want to catch it
            ESP_LOGW(TAG, "BR patch: dead code branch to invalid target bc=%u, generating trap (j to self)",
                     (unsigned)tgt_bc);
            // Patch jump to itself (infinite loop / trap) - delta = -3 for j to self
            patch_j_at(ctx.buffer, j_pos, -3);
            continue;  // Skip normal patching, go to next fixup
        }
        
        tgt_native = bc_to_native[tgt_bc];
        
        uint32_t after_j = j_pos + 3u;
        int32_t delta = (int32_t)tgt_native - (int32_t)after_j;
#if ESPB_JIT_DEBUG_OPTOCODES
        JIT_LOGI(TAG, "[FIXUP] j_pos=%u tgt_bc=%u tgt_native=%u delta=%d",
                 (unsigned)j_pos, (unsigned)tgt_bc, (unsigned)tgt_native, (int)delta);
#endif
        patch_j_at(ctx.buffer, j_pos, delta);
    }
    // Ensure any buffered bytes are committed to memory
    emit_flush_words(&ctx);

#if ESPB_JIT_DEBUG_OPTOCODES
    // Debug (BR_TABLE): dump bc_to_native mapping for selected targets
    ESP_LOGI(TAG, "bc_to_native for BR_TABLE targets:");
    uint32_t bc_targets[] = {202, 209, 216, 223};
    for (int t = 0; t < 4; t++) {
        uint32_t bc = bc_targets[t];
        if (bc < code_size) {
            uint32_t native = bc_to_native[bc];
            ESP_LOGI(TAG, "  bc=%u -> native=%u (0x%08X)", bc, native, native);
        }
    }

    // Debug (BR_TABLE): dump small area after fixups
    ESP_LOGI(TAG, "BR_TABLE dump AFTER fixups (1450-1490):");
    for (size_t dbg_off = 1450; dbg_off < 1490 && dbg_off < ctx.offset; dbg_off += 16) {
        size_t end_off = dbg_off + 16;
        if (end_off > ctx.offset) end_off = ctx.offset;
        char line[80];
        int pos = snprintf(line, sizeof(line), "[%04zu] ", dbg_off);
        for (size_t j = dbg_off; j < end_off && pos < 70; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", ctx.buffer[j]);
        }
        ESP_LOGI(TAG, "%s", line);
    }

    // Debug (BR_TABLE): dump target areas for jumps
    ESP_LOGI(TAG, "BR_TABLE targets dump (1470-1500, 1530-1550, 1580-1600, 1630-1660):");
    size_t ranges[][2] = {{1470, 1500}, {1530, 1550}, {1580, 1600}, {1630, 1660}};
    for (int r = 0; r < 4; r++) {
        for (size_t dbg_off = ranges[r][0]; dbg_off < ranges[r][1] && dbg_off < ctx.offset; dbg_off += 16) {
            size_t end_off = dbg_off + 16;
            if (end_off > ctx.offset) end_off = ctx.offset;
            if (end_off > ranges[r][1]) end_off = ranges[r][1];
            char line[80];
            int pos = snprintf(line, sizeof(line), "[%04zu] ", dbg_off);
            for (size_t j = dbg_off; j < end_off && pos < 70; j++) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", ctx.buffer[j]);
            }
            ESP_LOGI(TAG, "%s", line);
        }
    }
#endif

    if (ctx.error) {
        ESP_LOGE(TAG, "Inline JIT failed at bytecode offset %u (opcode 0x%02X)", (unsigned)last_off, (unsigned)last_op);
        heap_caps_free(fixups);
        heap_caps_free(bc_to_native);
        espb_exec_free(buffer);
        return ESPB_ERR_INVALID_STATE;
    }

    // NOTE: do NOT byte-read from EXEC IRAM on this target (can trigger LoadStoreError).
    // If you need debugging, implement a 32-bit word dump instead.
    
    // Sync code cache
    // NOTE: On ESP32, IRAM (0x4008xxxx) is not cache-backed; esp_cache_msync may fail.
    // Keep it disabled for now to avoid noisy errors; re-enable only if we place code in cacheable RAM.
    if (0) {
        if (ctx.offset > 0) {
            ESP_LOGI(TAG, "Syncing code cache: buffer=%p size=%zu", (void*)buffer, ctx.offset);
            xtensa_sync_code(buffer, ctx.offset);
        }
    }

    // Shrink-to-fit: release unused EXEC heap to avoid fragmentation.
    // Keep alignment to 4 bytes for safety.
    size_t used_size = (ctx.offset + 3u) & ~3u;
    if (used_size > 0 && used_size < ctx.capacity) {
        uint8_t *shrunk = (uint8_t*)espb_exec_realloc(buffer, used_size);
        if (shrunk) {
            buffer = shrunk;
            ctx.buffer = shrunk;
            ctx.capacity = used_size;
        } else {
            ESP_LOGW(TAG, "Failed to shrink JIT buffer (used=%zu, cap=%zu), keeping original", used_size, ctx.capacity);
        }
    }

    heap_caps_free(fixups);
    heap_caps_free(bc_to_native);

    *out_code = buffer;
    *out_size = ctx.offset;

#if ESPB_JIT_DEBUG
    ESP_LOGI(TAG, "Inline JIT compiled: %zu bytes at %p", ctx.offset, (void*)buffer);
#endif
    
#if ESPB_JIT_DEBUG_OPTOCODES
    // DEBUG: Dump JIT buffer to verify correct encoding (very verbose)
    // NOTE: IRAM on Xtensa ESP32 does NOT support byte reads! Must use 32-bit word reads.
    {
        // Dump first 64 bytes
        ESP_LOGI(TAG, "[jit-dump] First 64 bytes of JIT buffer:");
        for (size_t i = 0; i < 64 && i < ctx.offset; i += 16) {
            char line[80];
            int pos = snprintf(line, sizeof(line), "%03zu: ", i);
            for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
                size_t byte_pos = i + j;
                size_t word_pos = byte_pos & ~3u;
                uint32_t word = *(volatile uint32_t*)(buffer + word_pos);
                uint8_t byte_val = (uint8_t)(word >> (8u * (byte_pos & 3u)));
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", byte_val);
            }
            ESP_LOGI(TAG, "[jit-dump] %s", line);
        }

        // Dump area around offset 256-300 (where crash at 268 may happen)
        if (ctx.offset > 256) {
            ESP_LOGI(TAG, "[jit-dump] Bytes 256-300 (crash area):");
            for (size_t i = 256; i < 300 && i < ctx.offset; i += 16) {
                char line[80];
                int pos = snprintf(line, sizeof(line), "%03zu: ", i);
                for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
                    size_t byte_pos = i + j;
                    size_t word_pos = byte_pos & ~3u;
                    uint32_t word = *(volatile uint32_t*)(buffer + word_pos);
                    uint8_t byte_val = (uint8_t)(word >> (8u * (byte_pos & 3u)));
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", byte_val);
                }
                ESP_LOGI(TAG, "[jit-dump] %s", line);
            }
        }

        // Dump area around offset 300-400 and 500-600 (where crash may happen)
        if (ctx.offset >= 300) {
            ESP_LOGI(TAG, "[jit-dump] Bytes 300-400 (SUB/ZEXT/SHRU area):");
            for (size_t i = 300; i < 400 && i < ctx.offset; i += 16) {
                char line[80];
                int pos = snprintf(line, sizeof(line), "%03zu: ", i);
                for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
                    size_t byte_pos = i + j;
                    size_t word_pos = byte_pos & ~3u;
                    uint32_t word = *(volatile uint32_t*)(buffer + word_pos);
                    uint8_t byte_val = (uint8_t)(word >> (8u * (byte_pos & 3u)));
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", byte_val);
                }
                ESP_LOGI(TAG, "[jit-dump] %s", line);
            }
        }
        if (ctx.offset >= 500) {
            ESP_LOGI(TAG, "[jit-dump] Bytes 500-600 (post-loop area):");
            for (size_t i = 500; i < 600 && i < ctx.offset; i += 16) {
                char line[80];
                int pos = snprintf(line, sizeof(line), "%03zu: ", i);
                for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
                    size_t byte_pos = i + j;
                    size_t word_pos = byte_pos & ~3u;
                    uint32_t word = *(volatile uint32_t*)(buffer + word_pos);
                    uint8_t byte_val = (uint8_t)(word >> (8u * (byte_pos & 3u)));
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", byte_val);
                }
                ESP_LOGI(TAG, "[jit-dump] %s", line);
            }
        }
        // Dump bytes 900-1000 where SHRU.I64.IMM8 should be
        if (ctx.offset >= 900) {
            ESP_LOGI(TAG, "[jit-dump] Bytes 900-1000 (SHRU.I64.IMM8 area):");
            for (size_t i = 900; i < 1000 && i < ctx.offset; i += 16) {
                char line[80];
                int pos = snprintf(line, sizeof(line), "%03zu: ", i);
                for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
                    size_t byte_pos = i + j;
                    size_t word_pos = byte_pos & ~3u;
                    uint32_t word = *(volatile uint32_t*)(buffer + word_pos);
                    uint8_t byte_val = (uint8_t)(word >> (8u * (byte_pos & 3u)));
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", byte_val);
                }
                ESP_LOGI(TAG, "[jit-dump] %s", line);
            }
        }
    }
#endif
    return ESPB_OK;
}
