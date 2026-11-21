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
// Xtensa Native JIT Backend for ESP32/ESP32-S2/ESP32-S3
// Uses Windowed ABI (standard for ESP32)
//
// Based on RISC-V JIT architecture but adapted for Xtensa ISA.

#include "espb_interpreter_common_types.h"
#include "espb_jit.h"
#include "espb_jit_helpers.h"
#include "espb_jit_globals.h"
#include "espb_jit_import_call.h"

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "espb_exec_memory.h"
#include "esp_log.h"
#include "esp_memory_utils.h"  // esp_ptr_in_iram

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

#if ESPB_JIT_DEBUG
#define JIT_LOGI ESP_LOGI
#define JIT_LOGD ESP_LOGD
#define JIT_LOGW ESP_LOGW
#else
#define JIT_LOGI(tag, fmt, ...) ((void)0)
#define JIT_LOGD(tag, fmt, ...) ((void)0)
#define JIT_LOGW(tag, fmt, ...) ((void)0)
#endif

static const char* TAG = "espb_jit_xtensa";

// NOTE: .S trampolines removed; Xtensa JIT uses inline emitter backend (jit_xtensa_inline.c)
#if 0
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_start;
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_end;
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_lit_helper;
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_lit_symbol_idx;
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_lit_num_vregs;
extern const uint8_t espb_jit_xtensa_tpl_ld_global_addr_lit_rd;

extern const uint8_t espb_jit_xtensa_tpl_call_import_start;
extern const uint8_t espb_jit_xtensa_tpl_call_import_end;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_helper;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_import_idx;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_num_vregs;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_has_var;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_num_args;
extern const uint8_t espb_jit_xtensa_tpl_call_import_lit_arg_types_ptr;

extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_start;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_end;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_helper_ldga;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_symbol_idx;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_num_vregs;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_rd;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_helper_ci;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_import_idx;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_has_var;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_num_args;
extern const uint8_t espb_jit_xtensa_tpl_seq_ldga_ci_lit_arg_types_ptr;

// Universal ops trampoline (executes an op-list)
extern const uint8_t espb_jit_xtensa_tpl_ops_start;
extern const uint8_t espb_jit_xtensa_tpl_ops_end;
extern const uint8_t espb_jit_xtensa_tpl_ops_lit_ops_ptr;
extern const uint8_t espb_jit_xtensa_tpl_ops_lit_num_vregs;
extern const uint8_t espb_jit_xtensa_tpl_ops_lit_helper_ldga;
extern const uint8_t espb_jit_xtensa_tpl_ops_lit_helper_ci;
#endif

// Maximum JIT code size per function
#define XTENSA_JIT_MAX_CODE_SIZE (32 * 1024)

// Universal Xtensa JIT op-list (consumed by jit_xtensa_tramp_ops.S)
typedef enum {
    XTENSA_JIT_OP_END = 0,
    XTENSA_JIT_OP_LDGA = 1,
    XTENSA_JIT_OP_CALL_IMPORT = 2,
    XTENSA_JIT_OP_LDC_I32 = 3,
} EspbXtensaJitOpType;

typedef struct __attribute__((packed)) {
    uint8_t type;           // EspbXtensaJitOpType
    uint8_t rd_or_hasvar;   // LDGA: rd, CALL_IMPORT: has_var, LDC_I32: rd
    uint16_t u16_0;         // LDGA: symbol_idx, CALL_IMPORT: import_idx
    uint16_t u16_1;         // LDGA: 0, CALL_IMPORT: num_args
    uint16_t _pad;          // keep ptr aligned
    uint32_t ptr;           // CALL_IMPORT: arg_types_ptr, LDC_I32: imm32 value
} EspbXtensaJitOp;

// Maximum labels and patchpoints
#define XTENSA_JIT_MAX_LABELS 256
#define XTENSA_JIT_MAX_PATCHPOINTS 128
#define XTENSA_JIT_MAX_LITERALS 64

// Label for branch targets
typedef struct {
    size_t bytecode_offset;
    size_t native_offset;
} XtensaJitLabel;

// Patchpoint types
typedef enum {
    XTENSA_PATCH_BRANCH,    // Branch instruction
    XTENSA_PATCH_L32R,      // L32R literal pool
} XtensaPatchType;

// Patchpoint for later fixup
typedef struct {
    XtensaPatchType type;
    size_t patch_location;
    union {
        struct {
            size_t target_bytecode_offset;
            size_t source_bytecode_offset;
            bool is_conditional;
        } branch;
        struct {
            size_t literal_index;
        } l32r;
    };
} XtensaJitPatchpoint;

// Literal pool entry
typedef struct {
    uint32_t value;
    size_t pool_offset;
} XtensaJitLiteral;

// JIT compilation context
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;

    XtensaJitLabel labels[XTENSA_JIT_MAX_LABELS];
    size_t num_labels;

    XtensaJitPatchpoint patchpoints[XTENSA_JIT_MAX_PATCHPOINTS];
    size_t num_patchpoints;

    XtensaJitLiteral literals[XTENSA_JIT_MAX_LITERALS];
    size_t num_literals;
    size_t literal_pool_start;
    
    // Instance pointer for compilation
    EspbInstance *instance;
    const EspbFunctionBody *body;
} XtensaJitContext;

// Forward declarations
static void xtensa_emit_bytes(XtensaJitContext *ctx, const uint8_t *bytes, size_t len);

// Initialize context
static void xtensa_jit_ctx_init(XtensaJitContext *ctx, uint8_t *buffer, size_t capacity) {
    ctx->buffer = buffer;
    ctx->capacity = capacity;
    ctx->offset = 0;
    ctx->num_labels = 0;
    ctx->num_patchpoints = 0;
    ctx->num_literals = 0;
    ctx->literal_pool_start = 0;
}

// Emit raw bytes
static void xtensa_emit_bytes(XtensaJitContext *ctx, const uint8_t *bytes, size_t len) {
    if (ctx->offset + len > ctx->capacity) {
        ESP_LOGE(TAG, "Buffer overflow at offset %zu", ctx->offset);
        return;
    }
    memcpy(ctx->buffer + ctx->offset, bytes, len);
    ctx->offset += len;
}

// Emit 24-bit instruction (Xtensa narrow format, little-endian)
static void xtensa_emit_instr24(XtensaJitContext *ctx, uint32_t instr) {
    uint8_t bytes[3];
    bytes[0] = (uint8_t)(instr & 0xFF);
    bytes[1] = (uint8_t)((instr >> 8) & 0xFF);
    bytes[2] = (uint8_t)((instr >> 16) & 0xFF);
    xtensa_emit_bytes(ctx, bytes, 3);
}

// ===== Xtensa Instruction Emitters (Windowed ABI) =====

// ENTRY a1, framesize - create stack frame (Windowed ABI prologue)
static void xtensa_emit_entry(XtensaJitContext *ctx, uint8_t framesize_words) {
    // ENTRY a1, imm12*8 (framesize in bytes, must be multiple of 8)
    // Encoding: [imm12 0001 0110 0110] little-endian
    uint32_t instr = 0x036100 | ((framesize_words & 0xFFF) << 12);
    xtensa_emit_instr24(ctx, instr);
}

// RETW - return (Windowed ABI epilogue)
static void xtensa_emit_retw(XtensaJitContext *ctx) {
    // RETW encoding: 0x000090
    xtensa_emit_instr24(ctx, 0x000090);
}

// MOVI aT, imm12 - load immediate (-2048..2047)
static void xtensa_emit_movi(XtensaJitContext *ctx, uint8_t aT, int16_t imm12) {
    // MOVI at, imm12
    uint32_t instr = 0x00a002 | (aT << 4) | ((imm12 & 0xFFF) << 12);
    xtensa_emit_instr24(ctx, instr);
}

// L32I aT, aS, offset - load 32-bit from [aS + offset*4]
static void xtensa_emit_l32i(XtensaJitContext *ctx, uint8_t aT, uint8_t aS, uint8_t offset) {
    // L32I at, as, imm8 (offset in words: 0-255 -> 0-1020 bytes)
    uint32_t instr = 0x002200 | (aS << 12) | (aT << 8) | (offset << 16);
    xtensa_emit_instr24(ctx, instr);
}

// S32I aT, aS, offset - store 32-bit to [aS + offset*4]
static void xtensa_emit_s32i(XtensaJitContext *ctx, uint8_t aT, uint8_t aS, uint8_t offset) {
    // S32I at, as, imm8
    uint32_t instr = 0x006200 | (aS << 12) | (aT << 8) | (offset << 16);
    xtensa_emit_instr24(ctx, instr);
}

// S8I aT, aS, offset - store byte to [aS + offset]
static void xtensa_emit_s8i(XtensaJitContext *ctx, uint8_t aT, uint8_t aS, uint16_t offset) {
    // S8I at, as, imm8
    uint32_t instr = 0x004200 | (aS << 12) | (aT << 8) | ((offset & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// ADD aR, aS, aT - aR = aS + aT
static void xtensa_emit_add(XtensaJitContext *ctx, uint8_t aR, uint8_t aS, uint8_t aT) {
    // ADD ar, as, at
    uint32_t instr = 0x000800 | (aR << 12) | (aS << 8) | (aT << 4);
    xtensa_emit_instr24(ctx, instr);
}

// SUB aR, aS, aT - aR = aS - aT
static void xtensa_emit_sub(XtensaJitContext *ctx, uint8_t aR, uint8_t aS, uint8_t aT) {
    // SUB ar, as, at
    uint32_t instr = 0x0000c0 | (aR << 12) | (aS << 8) | (aT << 4);
    xtensa_emit_instr24(ctx, instr);
}

// OR aR, aS, aT - aR = aS | aT
static void xtensa_emit_or(XtensaJitContext *ctx, uint8_t aR, uint8_t aS, uint8_t aT) {
    // OR ar, as, at
    uint32_t instr = 0x000020 | (aR << 12) | (aS << 8) | (aT << 4);
    xtensa_emit_instr24(ctx, instr);
}

// XOR aR, aS, aT - aR = aS ^ aT
static void xtensa_emit_xor(XtensaJitContext *ctx, uint8_t aR, uint8_t aS, uint8_t aT) {
    // XOR ar, as, at
    uint32_t instr = 0x000030 | (aR << 12) | (aS << 8) | (aT << 4);
    xtensa_emit_instr24(ctx, instr);
}

// ADDI aT, aS, imm8 - aT = aS + imm8 (-128..127)
static void xtensa_emit_addi(XtensaJitContext *ctx, uint8_t aT, uint8_t aS, int8_t imm8) {
    // ADDI at, as, imm8
    uint32_t instr = 0x00c002 | (aT << 4) | (aS << 8) | ((imm8 & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// SRAI aR, aT, sa - aR = aT >> sa (arithmetic, sign-extend)
static void xtensa_emit_srai(XtensaJitContext *ctx, uint8_t aR, uint8_t aT, uint8_t sa) {
    // SRAI ar, at, sa
    uint32_t instr = 0x001000 | (aT << 12) | (aR << 8) | ((sa & 0x1F) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// L32R aT, label - load 32-bit from literal pool (PC-relative)
static void xtensa_emit_l32r(XtensaJitContext *ctx, uint8_t aT, int16_t offset_words) {
    // L32R at, label (offset in words, 16-bit signed)
    uint32_t instr = 0x000001 | (aT << 4) | ((offset_words & 0xFFFF) << 8);
    xtensa_emit_instr24(ctx, instr);
}

// BNEZ aS, offset - branch if aS != 0
static void xtensa_emit_bnez(XtensaJitContext *ctx, uint8_t aS, int8_t offset) {
    // BNEZ as, label (offset in bytes, signed 8-bit)
    uint32_t instr = 0x005600 | (aS << 12) | ((offset & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// BEQZ aS, offset - branch if aS == 0
static void xtensa_emit_beqz(XtensaJitContext *ctx, uint8_t aS, int8_t offset) {
    // BEQZ as, label
    uint32_t instr = 0x001600 | (aS << 12) | ((offset & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// BLT aS, aT, offset - branch if aS < aT (signed)
static void xtensa_emit_blt(XtensaJitContext *ctx, uint8_t aS, uint8_t aT, int8_t offset) {
    // BLT as, at, label
    uint32_t instr = 0x002700 | (aS << 12) | (aT << 8) | ((offset & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// BLTU aS, aT, offset - branch if aS < aT (unsigned)
static void xtensa_emit_bltu(XtensaJitContext *ctx, uint8_t aS, uint8_t aT, int8_t offset) {
    // BLTU as, at, label
    uint32_t instr = 0x003700 | (aS << 12) | (aT << 8) | ((offset & 0xFF) << 16);
    xtensa_emit_instr24(ctx, instr);
}

// J offset - unconditional jump
static void xtensa_emit_j(XtensaJitContext *ctx, int32_t offset) {
    // J offset18 (offset in bytes, signed 18-bit)
    uint32_t instr = 0x000006 | ((offset & 0x3FFFF) << 6);
    xtensa_emit_instr24(ctx, instr);
}

// CALL8 offset - call subroutine (Windowed ABI, 8-register rotation)
static void xtensa_emit_call8(XtensaJitContext *ctx, int32_t offset) {
    // CALL8 offset18 (offset in bytes, must be aligned)
    uint32_t instr = 0x000005 | ((offset & 0x3FFFF) << 6);
    xtensa_emit_instr24(ctx, instr);
}

// CALLX8 aS - call subroutine at address in aS (Windowed ABI)
static void xtensa_emit_callx8(XtensaJitContext *ctx, uint8_t aS) {
    // CALLX8 as
    uint32_t instr = 0x0000c0 | (aS << 12);
    xtensa_emit_instr24(ctx, instr);
}

// ===== Label Management =====

static void xtensa_jit_add_label(XtensaJitContext *ctx, size_t bytecode_offset) {
    for (size_t i = 0; i < ctx->num_labels; i++) {
        if (ctx->labels[i].bytecode_offset == bytecode_offset) {
            ctx->labels[i].native_offset = ctx->offset;
            return;
        }
    }
    if (ctx->num_labels < XTENSA_JIT_MAX_LABELS) {
        ctx->labels[ctx->num_labels].bytecode_offset = bytecode_offset;
        ctx->labels[ctx->num_labels].native_offset = ctx->offset;
        ctx->num_labels++;
    }
}

static size_t xtensa_jit_find_label(XtensaJitContext *ctx, size_t bytecode_offset) {
    for (size_t i = 0; i < ctx->num_labels; i++) {
        if (ctx->labels[i].bytecode_offset == bytecode_offset) {
            return ctx->labels[i].native_offset;
        }
    }
    return (size_t)-1;
}

// ===== Literal Pool =====

static size_t xtensa_jit_add_literal(XtensaJitContext *ctx, uint32_t value) {
    // Check if already exists
    for (size_t i = 0; i < ctx->num_literals; i++) {
        if (ctx->literals[i].value == value) {
            return i;
        }
    }
    
    if (ctx->num_literals >= XTENSA_JIT_MAX_LITERALS) {
        ESP_LOGE(TAG, "Literal pool overflow");
        return 0;
    }
    
    size_t idx = ctx->num_literals++;
    ctx->literals[idx].value = value;
    ctx->literals[idx].pool_offset = 0;
    return idx;
}

static void xtensa_jit_emit_literal_pool(XtensaJitContext *ctx) {
    // Align to 4 bytes
    while (ctx->offset % 4 != 0) {
        uint8_t nop = 0;
        xtensa_emit_bytes(ctx, &nop, 1);
    }
    
    ctx->literal_pool_start = ctx->offset;
    
    for (size_t i = 0; i < ctx->num_literals; i++) {
        ctx->literals[i].pool_offset = ctx->offset;
        uint32_t val = ctx->literals[i].value;
        xtensa_emit_bytes(ctx, (uint8_t*)&val, 4);
    }
}

static void xtensa_emit_load_imm32(XtensaJitContext *ctx, uint8_t aT, uint32_t imm32) {
    int32_t imm = (int32_t)imm32;
    if (imm >= -2048 && imm < 2048) {
        xtensa_emit_movi(ctx, aT, (int16_t)imm);
    } else {
        size_t lit_idx = xtensa_jit_add_literal(ctx, imm32);
        
        if (ctx->num_patchpoints < XTENSA_JIT_MAX_PATCHPOINTS) {
            XtensaJitPatchpoint *pp = &ctx->patchpoints[ctx->num_patchpoints++];
            pp->type = XTENSA_PATCH_L32R;
            pp->patch_location = ctx->offset;
            pp->l32r.literal_index = lit_idx;
        }
        
        xtensa_emit_l32r(ctx, aT, 0);
    }
}

static void xtensa_emit_call_helper(XtensaJitContext *ctx, uintptr_t func_addr) {
    // Load address into temp register via L32R, then CALLX8
    xtensa_emit_load_imm32(ctx, 8, (uint32_t)func_addr);
    xtensa_emit_callx8(ctx, 8);
}

// ===== Patching =====

static void xtensa_jit_patch_l32r(XtensaJitContext *ctx) {
    for (size_t i = 0; i < ctx->num_patchpoints; i++) {
        XtensaJitPatchpoint *pp = &ctx->patchpoints[i];
        if (pp->type != XTENSA_PATCH_L32R) continue;
        
        size_t lit_idx = pp->l32r.literal_index;
        if (lit_idx >= ctx->num_literals) continue;
        
        size_t lit_offset = ctx->literals[lit_idx].pool_offset;
        size_t instr_pc = pp->patch_location + 3;
        
        int32_t offset_bytes = (int32_t)(lit_offset - instr_pc);
        int16_t offset_words = (int16_t)(offset_bytes / 4);
        
        uint8_t *instr = ctx->buffer + pp->patch_location;
        uint8_t aT = (instr[0] >> 4) & 0x0F;
        
        uint32_t new_instr = 0x000001 | (aT << 4) | ((offset_words & 0xFFFF) << 8);
        instr[0] = (uint8_t)(new_instr & 0xFF);
        instr[1] = (uint8_t)((new_instr >> 8) & 0xFF);
        instr[2] = (uint8_t)((new_instr >> 16) & 0xFF);
    }
}

static void xtensa_jit_patch_branches(XtensaJitContext *ctx) {
    for (size_t i = 0; i < ctx->num_patchpoints; i++) {
        XtensaJitPatchpoint *pp = &ctx->patchpoints[i];
        if (pp->type != XTENSA_PATCH_BRANCH) continue;
        
        size_t target_native = xtensa_jit_find_label(ctx, pp->branch.target_bytecode_offset);
        
        if (target_native == (size_t)-1) {
            ESP_LOGE(TAG, "Branch target not found for bytecode offset %zu", pp->branch.target_bytecode_offset);
            continue;
        }
        
        size_t instr_pc = pp->patch_location + 3;
        int32_t offset_bytes = (int32_t)(target_native - instr_pc);
        
        if (pp->branch.is_conditional) {
            // BNEZ/BEQZ: 8-bit offset
            if (offset_bytes < -128 || offset_bytes > 127) {
                ESP_LOGE(TAG, "Branch offset too large: %ld bytes", (long)offset_bytes);
                continue;
            }
            
            uint8_t *instr = ctx->buffer + pp->patch_location;
            uint8_t aS = (instr[1] >> 4) & 0x0F;
            uint32_t new_instr = 0x005600 | (aS << 12) | ((offset_bytes & 0xFF) << 16);
            instr[0] = (uint8_t)(new_instr & 0xFF);
            instr[1] = (uint8_t)((new_instr >> 8) & 0xFF);
            instr[2] = (uint8_t)((new_instr >> 16) & 0xFF);
        } else {
            // J: 18-bit offset
            if (offset_bytes < -(1<<17) || offset_bytes >= (1<<17)) {
                ESP_LOGE(TAG, "J offset too large: %ld bytes", (long)offset_bytes);
                continue;
            }
            
            uint8_t *instr = ctx->buffer + pp->patch_location;
            uint32_t new_instr = 0x000006 | ((offset_bytes & 0x3FFFF) << 6);
            instr[0] = (uint8_t)(new_instr & 0xFF);
            instr[1] = (uint8_t)((new_instr >> 8) & 0xFF);
            instr[2] = (uint8_t)((new_instr >> 16) & 0xFF);
        }
    }
}

// ===== Helper functions =====

static inline void patch_u32(void* base, size_t off, uint32_t v) {
    memcpy((uint8_t*)base + off, &v, sizeof(v));
}

static inline size_t align_up4(size_t x) {
    return (x + 3u) & ~3u;
}

static inline size_t align_up16(size_t x) {
    return (x + 15u) & ~15u;
}

static inline void jit_xtensa_sync_code(void* code, size_t size) {
    if (!code || size == 0) return;

    // If code is in IRAM, no I-cache sync should be necessary and some
    // cache implementations will reject such addresses.
    if (esp_ptr_in_iram(code)) {
        return;
    }

    // For other executable regions, ensure D$ writeback + I$ invalidate.
    esp_cache_msync(code, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_INST);
}

static bool parse_call_import_operands(const uint8_t* start,
                                      const uint8_t* end,
                                      size_t at_off,
                                      uint16_t* out_import_idx,
                                      uint8_t* out_has_var,
                                      uint8_t* out_num_args,
                                      const uint8_t** out_arg_types,
                                      size_t* out_len)
{
    if (!start || !end || start + at_off >= end) return false;
    if (start[at_off] != 0x09) return false;
    if (start + at_off + 3 > end) return false;

    uint16_t import_idx = (uint16_t)start[at_off + 1] | ((uint16_t)start[at_off + 2] << 8);
    uint8_t has_var = 0;
    uint8_t num_args = 0;
    const uint8_t* arg_types = NULL;

    size_t len = 3;
    size_t off2 = at_off + 3;
    if (start + off2 < end && start[off2] == 0xAA) {
        if (start + off2 + 2 > end) return false;
        has_var = 1;
        num_args = start[off2 + 1];
        if (num_args > 16) return false;
        if (start + off2 + 2 + num_args > end) return false;
        arg_types = &start[off2 + 2];
        len += 2u + (size_t)num_args;
    }

    if (out_import_idx) *out_import_idx = import_idx;
    if (out_has_var) *out_has_var = has_var;
    if (out_num_args) *out_num_args = num_args;
    if (out_arg_types) *out_arg_types = arg_types;
    if (out_len) *out_len = len;
    return true;
}

// ===== Main compilation entry point =====

// Forward declaration for inline JIT
EspbResult espb_jit_compile_function_xtensa_inline(
    EspbInstance* instance,
    uint32_t func_idx,
    const EspbFunctionBody* body,
    void** out_code,
    size_t* out_size
);

EspbResult espb_jit_compile_function(EspbInstance *instance,
                                    uint32_t func_idx,
                                    const EspbFunctionBody *body,
                                    void **out_code,
                                    size_t *out_size)
{
    if (!instance || !body || !out_code || !out_size) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    // Switch to inline JIT (no ops-trampoline)
    JIT_LOGI(TAG, "Redirecting to inline Xtensa JIT for func_idx=%u", (unsigned)func_idx);
    return espb_jit_compile_function_xtensa_inline(instance, func_idx, body, out_code, out_size);

    // Old ops-trampoline code below (kept for reference, not executed)
#if 0
    // Check if already compiled
    if (body->is_jit_compiled) {
        *out_code = body->jit_code;
        *out_size = body->jit_code_size;
        return ESPB_OK;
    }

    if (!body->code || body->code_size == 0) {
        return ESPB_ERR_INVALID_OPCODE;
    }

    JIT_LOGI(TAG, "Starting native Xtensa JIT compilation for func_idx=%u", (unsigned)func_idx);

    *out_code = NULL;
    *out_size = 0;

    const uint8_t* start = body->code;
    const uint8_t* end = body->code + body->code_size;
    const uint16_t num_vregs = body->header.num_virtual_regs;

    // ---------------------------------------------------------------------
    // Recognize simple patterns and use pre-made trampolines:
    //   1) [NOP*] LD_GLOBAL_ADDR [NOP*] END
    //   2) [NOP*] CALL_IMPORT [NOP*] END
    //   3) [NOP*] LD_GLOBAL_ADDR [NOP*] CALL_IMPORT [NOP*] END
    // ---------------------------------------------------------------------

    // Skip leading NOPs
    size_t off = 0;
    while (start + off < end && (start[off] == 0x00 || start[off] == 0x01)) off += 1;

    // Optional LD_GLOBAL_ADDR
    bool has_ldga = false;
    uint8_t ldga_rd = 0;
    uint16_t ldga_symbol_idx = 0;
    if (start + off + 4 <= end && start[off] == 0x1D) {
        has_ldga = true;
        ldga_rd = start[off + 1];
        ldga_symbol_idx = (uint16_t)start[off + 2] | ((uint16_t)start[off + 3] << 8);
        off += 4;
        while (start + off < end && (start[off] == 0x00 || start[off] == 0x01)) off += 1;
    }

    // Optional CALL_IMPORT
    bool has_ci = false;
    uint16_t ci_import_idx = 0;
    uint8_t ci_has_var = 0;
    uint8_t ci_num_args = 0;
    const uint8_t* ci_arg_types = NULL;
    if (start + off < end && start[off] == 0x09) {
        size_t ci_len = 0;
        if (!parse_call_import_operands(start, end, off, &ci_import_idx, &ci_has_var, &ci_num_args, &ci_arg_types, &ci_len)) {
            return ESPB_ERR_INVALID_OPCODE;
        }
        has_ci = true;
        off += ci_len;
        while (start + off < end && (start[off] == 0x00 || start[off] == 0x01)) off += 1;
    }

    // Expect END
    if (start + off < end && start[off] == 0x0F) {
        // (1) only LD_GLOBAL_ADDR
        if (has_ldga && !has_ci) {
            const uint8_t* tpl_start = &espb_jit_xtensa_tpl_ld_global_addr_start;
            const uint8_t* tpl_end = &espb_jit_xtensa_tpl_ld_global_addr_end;
            size_t tpl_size = (size_t)(tpl_end - tpl_start);
            if (tpl_size == 0 || tpl_size > 1024) return ESPB_ERR_INVALID_STATE;

            uint8_t* exec_buffer = (uint8_t*)espb_exec_alloc(tpl_size);
            if (!exec_buffer) return ESPB_ERR_OUT_OF_MEMORY;
            memcpy(exec_buffer, tpl_start, tpl_size);

            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ld_global_addr_lit_helper - tpl_start), (uint32_t)(uintptr_t)&espb_jit_ld_global_addr);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ld_global_addr_lit_symbol_idx - tpl_start), (uint32_t)ldga_symbol_idx);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ld_global_addr_lit_num_vregs - tpl_start), (uint32_t)num_vregs);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ld_global_addr_lit_rd - tpl_start), (uint32_t)ldga_rd);

            jit_xtensa_sync_code(exec_buffer, tpl_size);
            *out_code = exec_buffer;
            *out_size = tpl_size;
            
            JIT_LOGI(TAG, "Compiled LD_GLOBAL_ADDR trampoline: %zu bytes", tpl_size);
            return ESPB_OK;
        }

        // (2) only CALL_IMPORT
        if (!has_ldga && has_ci) {
            const uint8_t* tpl_start = &espb_jit_xtensa_tpl_call_import_start;
            const uint8_t* tpl_end = &espb_jit_xtensa_tpl_call_import_end;
            size_t tpl_size = (size_t)(tpl_end - tpl_start);
            if (tpl_size == 0 || tpl_size > 2048) return ESPB_ERR_INVALID_STATE;

            size_t arg_blob_off = align_up4(tpl_size);
            size_t arg_blob_size = (ci_has_var != 0) ? (size_t)ci_num_args : 0u;
            size_t total_size = arg_blob_off + arg_blob_size;

            uint8_t* exec_buffer = (uint8_t*)espb_exec_alloc(total_size);
            if (!exec_buffer) return ESPB_ERR_OUT_OF_MEMORY;
            memset(exec_buffer, 0, total_size);
            memcpy(exec_buffer, tpl_start, tpl_size);

            uint32_t arg_types_ptr = 0;
            if (ci_has_var != 0 && arg_blob_size > 0) {
                memcpy(exec_buffer + arg_blob_off, ci_arg_types, arg_blob_size);
                arg_types_ptr = (uint32_t)(uintptr_t)(exec_buffer + arg_blob_off);
            }

            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_helper - tpl_start), (uint32_t)(uintptr_t)&espb_jit_call_import);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_import_idx - tpl_start), (uint32_t)ci_import_idx);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_num_vregs - tpl_start), (uint32_t)num_vregs);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_has_var - tpl_start), (uint32_t)ci_has_var);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_num_args - tpl_start), (uint32_t)ci_num_args);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_call_import_lit_arg_types_ptr - tpl_start), arg_types_ptr);

            jit_xtensa_sync_code(exec_buffer, total_size);
            *out_code = exec_buffer;
            *out_size = total_size;
            
            JIT_LOGI(TAG, "Compiled CALL_IMPORT trampoline: %zu bytes", total_size);
            return ESPB_OK;
        }

        // (3) LD_GLOBAL_ADDR then CALL_IMPORT
        if (has_ldga && has_ci) {
            const uint8_t* tpl_start = &espb_jit_xtensa_tpl_seq_ldga_ci_start;
            const uint8_t* tpl_end = &espb_jit_xtensa_tpl_seq_ldga_ci_end;
            size_t tpl_size = (size_t)(tpl_end - tpl_start);
            if (tpl_size == 0 || tpl_size > 4096) return ESPB_ERR_INVALID_STATE;

            size_t arg_blob_off = align_up4(tpl_size);
            size_t arg_blob_size = (ci_has_var != 0) ? (size_t)ci_num_args : 0u;
            size_t total_size = arg_blob_off + arg_blob_size;

            uint8_t* exec_buffer = (uint8_t*)espb_exec_alloc(total_size);
            if (!exec_buffer) return ESPB_ERR_OUT_OF_MEMORY;
            memset(exec_buffer, 0, total_size);
            memcpy(exec_buffer, tpl_start, tpl_size);

            uint32_t arg_types_ptr = 0;
            if (ci_has_var != 0 && arg_blob_size > 0) {
                memcpy(exec_buffer + arg_blob_off, ci_arg_types, arg_blob_size);
                arg_types_ptr = (uint32_t)(uintptr_t)(exec_buffer + arg_blob_off);
            }

            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_helper_ldga - tpl_start), (uint32_t)(uintptr_t)&espb_jit_ld_global_addr);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_symbol_idx - tpl_start), (uint32_t)ldga_symbol_idx);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_num_vregs - tpl_start), (uint32_t)num_vregs);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_rd - tpl_start), (uint32_t)ldga_rd);

            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_helper_ci - tpl_start), (uint32_t)(uintptr_t)&espb_jit_call_import);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_import_idx - tpl_start), (uint32_t)ci_import_idx);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_has_var - tpl_start), (uint32_t)ci_has_var);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_num_args - tpl_start), (uint32_t)ci_num_args);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_seq_ldga_ci_lit_arg_types_ptr - tpl_start), arg_types_ptr);

            jit_xtensa_sync_code(exec_buffer, total_size);
            *out_code = exec_buffer;
            *out_size = total_size;
            
            JIT_LOGI(TAG, "Compiled LD_GLOBAL_ADDR + CALL_IMPORT trampoline: %zu bytes", total_size);
            return ESPB_OK;
        }
    }

    // ---------------------------------------------------------------------
    // Universal fallback: linear sequence of LD_GLOBAL_ADDR / CALL_IMPORT ... END
    // This covers typical codegen patterns like repeated printf calls.
    // ---------------------------------------------------------------------
    {
        size_t scan_off = 0;
        size_t op_count = 0;
        size_t arg_types_total = 0;

        // Pass 1: count ops and arg_types bytes
        while (start + scan_off < end) {
            uint8_t op = start[scan_off];
            if (op == 0x00 || op == 0x01) { scan_off++; continue; }
            if (op == 0x0F) break; // END
            
            // Debug: log unsupported opcode
            if (op != 0x1D && op != 0x09 && op != 0x18) {
                ESP_LOGW(TAG, "Pass1: Unsupported opcode 0x%02X at offset %zu", op, scan_off);
                break;
            }

            if (op == 0x18) {
                if (start + scan_off + 5 > end) break;
                op_count++;
                scan_off += 5;
                continue;
            }

            if (op == 0x1D) {
                if (start + scan_off + 4 > end) break;
                op_count++;
                scan_off += 4;
                continue;
            }

            if (op == 0x09) {
                uint16_t t_import_idx = 0; uint8_t t_has_var = 0; uint8_t t_num_args = 0;
                const uint8_t* t_arg_types = NULL; size_t t_len = 0;
                if (!parse_call_import_operands(start, end, scan_off, &t_import_idx, &t_has_var, &t_num_args, &t_arg_types, &t_len)) {
                    break;
                }
                op_count++;
                if (t_has_var != 0 && t_num_args > 0) {
                    arg_types_total += (size_t)t_num_args;
                }
                scan_off += t_len;
                continue;
            }

            // unknown opcode -> not supported by ops trampoline
            break;
        }

        if (start + scan_off < end && start[scan_off] == 0x0F && op_count > 0) {
            // Build code = [template][ops array][arg types blob]
            const uint8_t* tpl_start = &espb_jit_xtensa_tpl_ops_start;
            const uint8_t* tpl_end = &espb_jit_xtensa_tpl_ops_end;
            size_t tpl_size = (size_t)(tpl_end - tpl_start);
            if (tpl_size == 0 || tpl_size > 4096) {
                return ESPB_ERR_INVALID_STATE;
            }

            size_t ops_off = align_up4(tpl_size);
            size_t ops_size = (op_count + 1u) * sizeof(EspbXtensaJitOp);
            size_t blob_off = align_up4(ops_off + ops_size);
            size_t total_size = blob_off + arg_types_total;

            uint8_t* exec_buffer = (uint8_t*)espb_exec_alloc(total_size);
            if (!exec_buffer) return ESPB_ERR_OUT_OF_MEMORY;
            memset(exec_buffer, 0, total_size);
            memcpy(exec_buffer, tpl_start, tpl_size);

            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ops_lit_ops_ptr - tpl_start), (uint32_t)(uintptr_t)(exec_buffer + ops_off));
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ops_lit_num_vregs - tpl_start), (uint32_t)num_vregs);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ops_lit_helper_ldga - tpl_start), (uint32_t)(uintptr_t)&espb_jit_ld_global_addr);
            patch_u32(exec_buffer, (size_t)(&espb_jit_xtensa_tpl_ops_lit_helper_ci - tpl_start), (uint32_t)(uintptr_t)&espb_jit_call_import);

            EspbXtensaJitOp* ops = (EspbXtensaJitOp*)(exec_buffer + ops_off);
            uint8_t* blob = exec_buffer + blob_off;
            size_t blob_cur = 0;

            // Pass 2: fill ops
            scan_off = 0;
            size_t op_i = 0;
            JIT_LOGI(TAG, "OPS: Building op-list, scanning bytecode...");
            while (start + scan_off < end) {
                uint8_t op = start[scan_off];
                if (op == 0x00 || op == 0x01) { scan_off++; continue; }
                if (op == 0x0F) break;
                
                // Debug: log unsupported opcode
                if (op != 0x1D && op != 0x09 && op != 0x18) {
                    ESP_LOGW(TAG, "Pass2: Unsupported opcode 0x%02X at offset %zu", op, scan_off);
                    break;
                }

                if (op == 0x18) {
                    uint8_t rd = start[scan_off + 1];
                    uint32_t imm32;
                    memcpy(&imm32, &start[scan_off + 2], 4);
                    ops[op_i] = (EspbXtensaJitOp){
                        .type = XTENSA_JIT_OP_LDC_I32,
                        .rd_or_hasvar = rd,
                        .u16_0 = 0,
                        .u16_1 = 0,
                        ._pad = 0,
                        .ptr = imm32,
                    };
                    JIT_LOGI(TAG, "OPS[%zu]: LDC_I32 rd=%u imm32=%u (0x%08X)", op_i, rd, imm32, imm32);
                    op_i++;
                    scan_off += 5;
                    continue;
                }

                if (op == 0x1D) {
                    uint8_t rd = start[scan_off + 1];
                    uint16_t sym = (uint16_t)start[scan_off + 2] | ((uint16_t)start[scan_off + 3] << 8);
                    ops[op_i] = (EspbXtensaJitOp){
                        .type = XTENSA_JIT_OP_LDGA,
                        .rd_or_hasvar = rd,
                        .u16_0 = sym,
                        .u16_1 = 0,
                        ._pad = 0,
                        .ptr = 0,
                    };
                    JIT_LOGI(TAG, "OPS[%zu]: LDGA rd=%u sym=%u", op_i, rd, sym);
                    op_i++;
                    scan_off += 4;
                    continue;
                }

                if (op == 0x09) {
                    uint16_t t_import_idx = 0; uint8_t t_has_var = 0; uint8_t t_num_args = 0;
                    const uint8_t* t_arg_types = NULL; size_t t_len = 0;
                    if (!parse_call_import_operands(start, end, scan_off, &t_import_idx, &t_has_var, &t_num_args, &t_arg_types, &t_len)) {
                        break;
                    }

                    // RISC-V compatibility: if no 0xAA extension, read num_args from import signature
                    if (t_has_var == 0 && t_num_args == 0) {
                        if (t_import_idx < instance->module->num_imports) {
                            const EspbImportDesc* imp = &instance->module->imports[t_import_idx];
                            if (imp->kind == ESPB_IMPORT_KIND_FUNC) {
                                uint16_t sig_idx = imp->desc.func.type_idx;
                                if (sig_idx < instance->module->num_signatures) {
                                    t_num_args = instance->module->signatures[sig_idx].num_params;
                                }
                            }
                        }
                    }

                    uint32_t arg_ptr = 0;
                    if (t_has_var != 0 && t_num_args > 0 && t_arg_types != NULL) {
                        memcpy(blob + blob_cur, t_arg_types, (size_t)t_num_args);
                        arg_ptr = (uint32_t)(uintptr_t)(blob + blob_cur);
                        ESP_LOGI(TAG, "  → arg_types copied to blob+%zu: [0]=0x%02X [1]=0x%02X", 
                                 blob_cur, t_arg_types[0], t_num_args > 1 ? t_arg_types[1] : 0);
                        blob_cur += (size_t)t_num_args;
                    }

                    ops[op_i] = (EspbXtensaJitOp){
                        .type = XTENSA_JIT_OP_CALL_IMPORT,
                        .rd_or_hasvar = t_has_var,
                        .u16_0 = t_import_idx,
                        .u16_1 = (uint16_t)t_num_args,
                        ._pad = 0,
                        .ptr = arg_ptr,
                    };
                    JIT_LOGI(TAG, "OPS[%zu]: CALL_IMPORT idx=%u has_var=%u num_args=%u", 
                             op_i, t_import_idx, t_has_var, t_num_args);
                    op_i++;

                    scan_off += t_len;
                    continue;
                }

                break;
            }

            ops[op_i] = (EspbXtensaJitOp){ .type = XTENSA_JIT_OP_END };
            JIT_LOGI(TAG, "OPS[%zu]: END", op_i);
            op_i++;

            if (op_i != op_count + 1u) {
                espb_exec_free(exec_buffer);
                // fallthrough to interpreter
            } else {
                jit_xtensa_sync_code(exec_buffer, total_size);
                *out_code = exec_buffer;
                *out_size = total_size;
                JIT_LOGI(TAG, "Compiled OPS trampoline: %zu bytes (ops=%zu, arg_blob=%zu)", total_size, op_count, arg_types_total);
                return ESPB_OK;
            }
        }
    }

    // Not supported by current Xtensa JIT
    JIT_LOGI(TAG, "Xtensa JIT: Complex bytecode pattern for func_idx=%u, will use interpreter fallback", (unsigned)func_idx);
    JIT_LOGD(TAG, "  Pattern: has_ldga=%d, has_ci=%d, code_size=%u", has_ldga, has_ci, (unsigned)body->code_size);
    return ESPB_ERR_UNSUPPORTED;
#endif // Old ops-trampoline code
}
