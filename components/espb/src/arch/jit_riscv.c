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
#include "espb_interpreter_common_types.h"
#include <alloca.h>
#include "espb_api.h"
#include "espb_interpreter_runtime_oc.h"
#include "espb_jit_dispatcher.h"
#include "espb_jit_import_call.h"
#include "espb_jit_indirect_ptr.h"
#include "espb_jit_globals.h"
#include "espb_jit_helpers.h"
#include "espb_exec_memory.h"
#include "espb_heap_manager.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_cache.h"  // from esp_mm
#include "esp_memory_utils.h"  // for esp_ptr_executable, esp_ptr_in_iram, esp_ptr_in_dram

ffi_type* espb_type_to_ffi_type(EspbValueType es_type) {
    switch (es_type) {
        case ESPB_TYPE_VOID: return &ffi_type_void;
        case ESPB_TYPE_I8:   return &ffi_type_sint8;
        case ESPB_TYPE_U8:   return &ffi_type_uint8;
        case ESPB_TYPE_I16:  return &ffi_type_sint16;
        case ESPB_TYPE_U16:  return &ffi_type_uint16;
        case ESPB_TYPE_I32:  return &ffi_type_sint32;
        case ESPB_TYPE_U32:  return &ffi_type_uint32;
        case ESPB_TYPE_I64:  return &ffi_type_sint64;
        case ESPB_TYPE_U64:  return &ffi_type_uint64;
        case ESPB_TYPE_F32:  return &ffi_type_float;
        case ESPB_TYPE_F64:  return &ffi_type_double;
        case ESPB_TYPE_PTR:  return &ffi_type_pointer;
        case ESPB_TYPE_BOOL: return &ffi_type_sint32;
        default: return NULL;
    }
}

// Helper functions for soft-float emulation in JIT code
// These are called from JIT-compiled code to perform float operations

// Convert U32 to F64
static inline double jit_helper_cvt_u32_f64(uint32_t val) {
    return (double)val;
}

// Convert F64 to I32 (truncate toward zero, like C cast)
__attribute__((noinline))
static int32_t jit_helper_cvt_f64_i32(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    return (int32_t)a;
}

// Convert I64 to F32 (return raw f32 bits, avoids hard-float ABI)
__attribute__((noinline))
static uint32_t jit_helper_cvt_i64_f32_bits(uint64_t v) {
    float f = (float)(int64_t)v;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Convert I64 to F64 (return raw f64 bits)
__attribute__((noinline))
static uint64_t jit_helper_cvt_i64_f64_bits(uint64_t v) {
    double d = (double)(int64_t)v;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Convert U64 to F64
__attribute__((noinline))
static double jit_helper_cvt_u64_f64(uint64_t val) {
    // printf("[CVT.U64.F64] Converting %llu to double\n", (unsigned long long)val);
    double result = (double)val;
    // printf("[CVT.U64.F64] Result: %f\n", result);
    return result;
}

// Divide two F64 values
static inline double jit_helper_div_f64(double a, double b) {
    return a / b;
}

// Add two F64 values
static inline double jit_helper_add_f64(double a, double b) {
    return a + b;
}

// Subtract two F64 values
static inline double jit_helper_sub_f64(double a, double b) {
    return a - b;
}

// Multiply two F64 values
static inline double jit_helper_mul_f64(double a, double b) {
    return a * b;
}

// --- F32 helpers (operate on raw IEEE-754 bits) ---
// We avoid relying on hard-float ABI: JIT passes/returns u32 in integer regs.
static inline uint32_t jit_helper_fadd_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = a + b;
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fsub_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = a - b;
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fmul_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = a * b;
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fdiv_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = a / b;
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fneg_f32_bits(uint32_t a_bits) {
    return a_bits ^ 0x80000000u;
}

static inline uint32_t jit_helper_fabs_f32_bits(uint32_t a_bits) {
    return a_bits & 0x7FFFFFFFu;
}

static inline uint32_t jit_helper_fsqrt_f32_bits(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    float r = sqrtf(a);
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fmin_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = fminf(a, b);
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

static inline uint32_t jit_helper_fmax_f32_bits(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    float r = fmaxf(a, b);
    uint32_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

// F32 -> F64 promotion (operate on raw bits)
__attribute__((noinline))
static uint64_t jit_helper_fpromote_f32_to_f64_bits(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    double d = (double)a;
    uint64_t d_bits;
    memcpy(&d_bits, &d, sizeof(d));
    return d_bits;
}

// F64 -> F32 rounding (demotion)
__attribute__((noinline))
static uint32_t jit_helper_fpround_f64_to_f32_bits(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    float f = (float)a;
    uint32_t f_bits;
    memcpy(&f_bits, &f, sizeof(f));
    return f_bits;
}

// Convert U32 to F32
__attribute__((noinline))
static uint32_t jit_helper_cvt_u32_f32_bits(uint32_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Convert U64 to F32
__attribute__((noinline))
static uint32_t jit_helper_cvt_u64_f32_bits(uint64_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Convert U64 to F64
__attribute__((noinline))
static uint64_t jit_helper_cvt_u64_f64_bits(uint64_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Convert U32 to F64
__attribute__((noinline))
static uint64_t jit_helper_cvt_u32_f64_bits(uint32_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Convert I32 to F32
__attribute__((noinline))
static uint32_t jit_helper_cvt_i32_f32_bits(int32_t val) {
    float f = (float)val;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Convert I32 to F64
__attribute__((noinline))
static uint64_t jit_helper_cvt_i32_f64_bits(int32_t val) {
    double d = (double)val;
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Convert F32 to U32 (truncate toward zero)
__attribute__((noinline))
static uint32_t jit_helper_cvt_f32_u32(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    return (uint32_t)a;
}

// Convert F32 to U64 (truncate toward zero)
__attribute__((noinline))
static uint64_t jit_helper_cvt_f32_u64(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    return (uint64_t)a;
}

// Convert F32 to I32 (truncate toward zero)
__attribute__((noinline))
static int32_t jit_helper_cvt_f32_i32(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    return (int32_t)a;
}

// Convert F32 to I64 (truncate toward zero)
__attribute__((noinline))
static int64_t jit_helper_cvt_f32_i64(uint32_t a_bits) {
    float a;
    memcpy(&a, &a_bits, sizeof(a));
    return (int64_t)a;
}

// Convert F64 to U32 (truncate toward zero)
__attribute__((noinline))
static uint32_t jit_helper_cvt_f64_u32(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    return (uint32_t)a;
}

// Convert F64 to U64 (truncate toward zero)
__attribute__((noinline))
static uint64_t jit_helper_cvt_f64_u64(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    return (uint64_t)a;
}

// Convert F64 to I64 (truncate toward zero)
__attribute__((noinline))
static int64_t jit_helper_cvt_f64_i64(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    return (int64_t)a;
}

// Convert F64 to I32 (already exists above but renamed for consistency)
// (jit_helper_cvt_f64_i32 already defined above)

// --- F64 helpers (operate on raw IEEE-754 bits) ---
__attribute__((noinline))
static uint64_t jit_helper_fabs_f64_bits(uint64_t a_bits) {
    return a_bits & 0x7FFFFFFFFFFFFFFFULL;
}

__attribute__((noinline))
static uint64_t jit_helper_fsqrt_f64_bits(uint64_t a_bits) {
    double a;
    memcpy(&a, &a_bits, sizeof(a));
    double r = sqrt(a);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fmin_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmin(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

__attribute__((noinline))
static uint64_t jit_helper_fmax_f64_bits(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    double r = fmax(a, b);
    uint64_t r_bits;
    memcpy(&r_bits, &r, sizeof(r));
    return r_bits;
}

// Integer unary helpers (defined semantics, no UB on INT32_MIN)
static inline uint32_t jit_helper_neg_i32(uint32_t a_bits) {
    // two's complement wrapping
    return (uint32_t)(0u - a_bits);
}

static inline uint32_t jit_helper_abs_i32(uint32_t a_bits) {
    int32_t a = (int32_t)a_bits;
    if (a == INT32_MIN) return (uint32_t)INT32_MIN;
    int32_t r = (a < 0) ? -a : a;
    return (uint32_t)r;
}

// Forward declaration of libgcc function
extern uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);

// Unsigned 64-bit division wrapper with zero check
__attribute__((noinline))
static uint64_t jit_helper_divu64(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) return 0; // Avoid division by zero
    return __udivdi3(dividend, divisor);
}

// Signed 64-bit division
__attribute__((noinline))
static int64_t jit_helper_divs64(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0;
    // Check for overflow: INT64_MIN / -1
    if (dividend == INT64_MIN && divisor == -1) return INT64_MIN;
    return dividend / divisor;
}

// Signed 64-bit remainder
__attribute__((noinline))
static int64_t jit_helper_rems64(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0;
     if (dividend == INT64_MIN && divisor == -1) return 0;
    return dividend % divisor;
}

// Unsigned 64-bit remainder
__attribute__((noinline))
static uint64_t jit_helper_remu64(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) return 0;
    return dividend % divisor;
}

// 64-bit multiplication
__attribute__((noinline))
static uint64_t jit_helper_mul64(uint64_t a, uint64_t b) {
    return a * b;
}

// --- I64 bitwise/shift helpers (v1.7 opcodes 0x38..0x3E) ---
__attribute__((noinline))
static uint64_t jit_helper_and_i64(uint64_t a, uint64_t b) { return a & b; }

__attribute__((noinline))
static uint64_t jit_helper_or_i64(uint64_t a, uint64_t b) { return a | b; }

__attribute__((noinline))
static uint64_t jit_helper_xor_i64(uint64_t a, uint64_t b) { return a ^ b; }

__attribute__((noinline))
static uint64_t jit_helper_not_i64(uint64_t a) { return ~a; }

__attribute__((noinline))
static uint64_t jit_helper_shl_i64(uint64_t a, uint32_t sh) { return a << (sh & 63u); }

__attribute__((noinline))
static int64_t jit_helper_shr_i64(int64_t a, uint32_t sh) { return a >> (sh & 63u); }

__attribute__((noinline))
static uint64_t jit_helper_ushr_i64(uint64_t a, uint32_t sh) { return a >> (sh & 63u); }

// External compiler-rt/libgcc function for uint64_t to double conversion
// This function is provided by the compiler's runtime library
extern double __floatundidf(uint64_t val);

// ===== Memory management helpers =====
__attribute__((noinline))
static EspbResult jit_helper_memory_init(EspbInstance *instance, uint32_t data_seg_idx, 
                                         uint32_t dest_addr, uint32_t src_offset, uint32_t size) {
    if (!instance || !instance->module) return ESPB_ERR_INVALID_STATE;
    
    const EspbModule *module = instance->module;
    if (data_seg_idx >= module->num_data_segments) {
        return ESPB_ERR_INVALID_OPERAND;
    }
    
    const EspbDataSegment *segment = &module->data_segments[data_seg_idx];
    
    // Bounds check
    if ((uint64_t)dest_addr + size > instance->memory_size_bytes || 
        (uint64_t)src_offset + size > segment->data_size) {
        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
    }
    
    // Copy data
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
    
    // Drop segment by setting size to 0 (cast away const for modification)
    EspbDataSegment *segment = (EspbDataSegment*)&module->data_segments[data_seg_idx];
    segment->data_size = 0;
    
    return ESPB_OK;
}

// ===== I64 comparison helpers =====
__attribute__((noinline))
static uint32_t jit_helper_cmp_lts_i64(int64_t a, int64_t b) {
    return (a < b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gt_i64(int64_t a, int64_t b) {
    return (a > b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_le_i64(int64_t a, int64_t b) {
    return (a <= b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ge_i64(int64_t a, int64_t b) {
    return (a >= b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ltu_i64(uint64_t a, uint64_t b) {
    return (a < b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gtu_i64(uint64_t a, uint64_t b) {
    return (a > b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_leu_i64(uint64_t a, uint64_t b) {
    return (a <= b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_geu_i64(uint64_t a, uint64_t b) {
    return (a >= b) ? 1 : 0;
}

// ===== F32 comparison helpers (operate on raw bits) =====
__attribute__((noinline))
static uint32_t jit_helper_cmp_eq_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a == b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ne_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a != b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_lt_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a < b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gt_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a > b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_le_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a <= b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ge_f32(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a >= b) ? 1 : 0;
}

// ===== F64 comparison helpers (operate on raw bits) =====
__attribute__((noinline))
static uint32_t jit_helper_cmp_eq_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a == b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ne_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a != b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_lt_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a < b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_gt_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a > b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_le_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a <= b) ? 1 : 0;
}

__attribute__((noinline))
static uint32_t jit_helper_cmp_ge_f64(uint64_t a_bits, uint64_t b_bits) {
    double a, b;
    memcpy(&a, &a_bits, sizeof(a));
    memcpy(&b, &b_bits, sizeof(b));
    return (a >= b) ? 1 : 0;
}

// Wrapper functions for 32-bit atomic operations
// ESP32-C3 (RV32IMAC) does NOT support AMO instructions, only LR/SC
// So we use GCC __atomic_* built-ins which compile to LR/SC sequences
static uint32_t jit_atomic_fetch_add_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_add((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_fetch_sub_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_sub((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_fetch_and_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_and((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_fetch_or_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_or((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_fetch_xor_4(volatile void* ptr, uint32_t val) {
    return __atomic_fetch_xor((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_exchange_4(volatile void* ptr, uint32_t val) {
    return __atomic_exchange_n((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static bool jit_atomic_compare_exchange_4(volatile void* ptr, uint32_t* expected, uint32_t desired) {
    return __atomic_compare_exchange_n((volatile uint32_t*)ptr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static uint32_t jit_atomic_load_4(volatile void* ptr) {
    return __atomic_load_n((volatile uint32_t*)ptr, __ATOMIC_SEQ_CST);
}

static void jit_atomic_store_4(volatile void* ptr, uint32_t val) {
    __atomic_store_n((volatile uint32_t*)ptr, val, __ATOMIC_SEQ_CST);
}

// Wrapper functions for 64-bit atomic operations
static uint64_t jit_atomic_fetch_add_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_add((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_fetch_sub_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_sub((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_fetch_and_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_and((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_fetch_or_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_or((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_fetch_xor_8(volatile void* ptr, uint64_t val) {
    return __atomic_fetch_xor((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_exchange_8(volatile void* ptr, uint64_t val) {
    return __atomic_exchange_n((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

static bool jit_atomic_compare_exchange_8(volatile void* ptr, uint64_t* expected, uint64_t desired) {
    return __atomic_compare_exchange_n((volatile uint64_t*)ptr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static uint64_t jit_atomic_load_8(volatile void* ptr) {
    return __atomic_load_n((volatile uint64_t*)ptr, __ATOMIC_SEQ_CST);
}

static void jit_atomic_store_8(volatile void* ptr, uint64_t val) {
    __atomic_store_n((volatile uint64_t*)ptr, val, __ATOMIC_SEQ_CST);
}

// Helper for 64-bit signed division (called from JIT code)
static int64_t jit_divs_i64(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0; // Should not happen, checked at compile time
    if (dividend == INT64_MIN && divisor == -1) return INT64_MIN; // Overflow case
    return dividend / divisor;
}

// Helper for 64-bit unsigned division
static uint64_t jit_divu_i64(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) return 0; // Should not happen, checked at compile time
    return dividend / divisor;
}

// Helper for 64-bit signed remainder
static int64_t jit_rems_i64(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0; // Should not happen, checked at compile time
    if (dividend == INT64_MIN && divisor == -1) return 0; // Overflow case
    return dividend % divisor;
}

// Helper for 64-bit unsigned remainder
static uint64_t jit_remu_i64(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) return 0; // Should not happen, checked at compile time
    return dividend % divisor;
}

// Helper for TABLE.SIZE operation
static uint32_t jit_helper_table_size(EspbInstance* instance) {
    if (!instance) return 0;
    return instance->table_size;
}

// Helper for TABLE.GET operation
static uint32_t jit_helper_table_get(EspbInstance* instance, uint32_t table_idx, uint32_t index) {
    (void)table_idx; // Используется единая таблица
    if (!instance || !instance->table_data || index >= instance->table_size) return 0;
    return (uint32_t)(uintptr_t)instance->table_data[index];
}

// Helper for TABLE.INIT operation (как в интерпретаторе)
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

// Helper for TABLE.COPY operation
static void jit_helper_table_copy(EspbInstance* instance, uint32_t dst_table_idx, uint32_t src_table_idx,
                                   uint32_t dst_offset, uint32_t src_offset, uint32_t count) {
    (void)dst_table_idx;
    (void)src_table_idx;
    // В текущей реализации используется одна таблица
    
    if (!instance || !instance->table_data || count == 0) return;
    
    // Проверяем и расширяем таблицу если нужно для обоих offset'ов
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
    
    // Копируем элементы (нужно учитывать направление копирования)
    if (dst_offset <= src_offset) {
        // Копируем вперёд
        for (uint32_t i = 0; i < count; i++) {
            if (src_offset + i < instance->table_size && dst_offset + i < instance->table_size) {
                instance->table_data[dst_offset + i] = instance->table_data[src_offset + i];
            }
        }
    } else {
        // Копируем назад (чтобы избежать перезаписи при перекрытии)
        for (uint32_t i = count; i > 0; i--) {
            if (src_offset + i - 1 < instance->table_size && dst_offset + i - 1 < instance->table_size) {
                instance->table_data[dst_offset + i - 1] = instance->table_data[src_offset + i - 1];
            }
        }
    }
}

// Helper for TABLE.GROW operation
static int32_t jit_helper_table_grow(EspbInstance* instance, uint32_t table_idx, uint32_t init_value, uint32_t delta) {
    (void)table_idx; // Используется единая таблица
    if (!instance || delta == 0) return instance ? (int32_t)instance->table_size : -1;
    
    uint32_t old_size = instance->table_size;
    uint32_t new_size = old_size + delta;
    
    // Проверяем максимальный размер
    if (new_size > instance->table_max_size) return -1;
    
    // Реаллокация таблицы
    void** new_table = (void**)realloc(instance->table_data, new_size * sizeof(void*));
    if (!new_table) return -1;
    
    // Инициализируем новые элементы
    void* init_val = (void*)(uintptr_t)init_value;
    for (uint32_t i = old_size; i < new_size; i++) {
        new_table[i] = init_val;
    }
    
    instance->table_data = new_table;
    instance->table_size = new_size;
    
    return (int32_t)old_size;
}

// Helper for TABLE.FILL operation
static void jit_helper_table_fill(EspbInstance* instance, uint32_t table_idx, uint32_t start_index, 
                                   uint32_t fill_value, uint32_t count) {
    (void)table_idx; // Используется единая таблица
    if (!instance || !instance->table_data) return;
    
    // Проверяем и расширяем таблицу если нужно
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
    
    // Заполняем диапазон таблицы
    void* value = (void*)(uintptr_t)fill_value;
    for (uint32_t i = 0; i < count; i++) {
        if (start_index + i < instance->table_size) {
            instance->table_data[start_index + i] = value;
        }
    }
}

// Helper for TABLE.SET operation
static void jit_helper_table_set(EspbInstance* instance, uint32_t table_idx, uint32_t index, uint32_t value) {
    // В текущей реализации ESPB используется одна таблица (table_size, table_data)
    // table_idx игнорируется, используется единая таблица
    (void)table_idx;
    
    if (!instance || !instance->table_data) return;
    if (index >= instance->table_size) {
        // Автоматическое расширение таблицы (как в интерпретаторе)
        uint32_t required_size = index + 1;
        if (required_size > instance->table_max_size) return;
        
        // Реаллокация таблицы (table_data это void**)
        void** new_table = (void**)realloc(instance->table_data, required_size * sizeof(void*));
        if (!new_table) return;
        
        // Инициализируем новые элементы нулями
        for (uint32_t i = instance->table_size; i < required_size; i++) {
            new_table[i] = NULL;
        }
        
        instance->table_data = new_table;
        instance->table_size = required_size;
    }
    // Преобразуем uint32_t в void*
    instance->table_data[index] = (void*)(uintptr_t)value;
}

// NOTE: Старый "прямой" FFI путь для CALL_IMPORT из JIT удалён.
// Теперь CALL_IMPORT компилируется как вызов общего helper'а `espb_jit_call_import()`
// (семантика идентична интерпретатору: callbacks, immeta, async out params, blocking imports).

// Структура для хранения информации о метке перехода
typedef struct {
    size_t bytecode_offset;  // Позиция в байт-коде ESPB
    size_t native_offset;    // Позиция в нативном коде RISC-V
} JitLabel;

// Структура для отложенной фиксации перехода
typedef struct {
    size_t patch_location;   // Где в нативном коде нужно пропатчить (offset в буфере)
    size_t source_bytecode_offset; // Откуда переход (в байт-коде)
    size_t target_bytecode_offset; // Куда должен вести переход (в байт-коде)
    bool is_conditional;     // true для BR_IF, false для BR
    uint8_t condition_reg;   // Для BR_IF: регистр условия
} JitPatchpoint;

// Контекст JIT-компиляции
typedef struct {
    uint8_t* buffer;
    size_t   capacity;
    size_t   offset;
    // Для отложенной фиксации переходов
    JitPatchpoint* patchpoints;
    size_t num_patchpoints;
    size_t patchpoints_capacity;
    // Таблица меток: bytecode_offset -> native_offset
    JitLabel* labels;
    size_t num_labels;
    size_t labels_capacity;

#ifdef JIT_STATS
    size_t helper_call_count;
    size_t helper_call_fallback_abs_count;
#endif

    // CMP+BR_IF ОПТИМИЗАЦИЯ: отслеживание последнего CMP
    uint8_t last_cmp_result_reg;  // Регистр с результатом последнего CMP (0xFF = нет)
    bool last_cmp_in_t0;           // Результат CMP находится в t0 (не сохранён в память)
} JitContext;

// Forward decls for peephole helpers (emit_* defined later)
static void emit_lw_phys(JitContext* ctx, uint8_t rd, int16_t offset, uint8_t rs1);
static void emit_sw_phys(JitContext* ctx, uint8_t rs2, int16_t offset, uint8_t rs1);

// Peephole reg-cache (локальный, без изменения JitContext)
// Кешируем только x5(t0) и x6(t1), т.к. остальные temporaries активно используются в других опкодах.
//
// Дополнительно: делаем selective-flush на BR/BR_IF по live-in анализу target/fallthrough basic block.
typedef struct {
    // I32 cache in x5/x6
    bool x5_valid;
    bool x6_valid;
    bool x5_dirty;
    bool x6_dirty;
    uint8_t x5_vreg;
    uint8_t x6_vreg;

    // I64 cache in x5(lo)/x6(hi) for one vreg
    bool i64_valid;
    bool i64_dirty;
    uint8_t i64_vreg;
} PeepholeRegCache;

static inline void ph_reset(PeepholeRegCache* ph) {
    ph->x5_valid = false;
    ph->x6_valid = false;
    ph->x5_dirty = false;
    ph->x6_dirty = false;
    ph->x5_vreg = 0xFF;
    ph->x6_vreg = 0xFF;

    ph->i64_valid = false;
    ph->i64_dirty = false;
    ph->i64_vreg = 0xFF;
}

static inline int ph_find(const PeepholeRegCache* ph, uint8_t vreg) {
    // если x5/x6 сейчас заняты под i64 кэш — не используем их как i32 кэш
    if (ph->i64_valid) return -1;
    if (ph->x5_valid && ph->x5_vreg == vreg) return 5;
    if (ph->x6_valid && ph->x6_vreg == vreg) return 6;
    return -1;
}

static inline void ph_set(PeepholeRegCache* ph, int phys, uint8_t vreg, bool dirty) {
    // invalidate duplicates
    if (phys == 5) {
        if (ph->x6_valid && ph->x6_vreg == vreg) { ph->x6_valid = false; ph->x6_dirty = false; ph->x6_vreg = 0xFF; }
        ph->x5_valid = true;
        ph->x5_dirty = dirty;
        ph->x5_vreg = vreg;
    } else if (phys == 6) {
        if (ph->x5_valid && ph->x5_vreg == vreg) { ph->x5_valid = false; ph->x5_dirty = false; ph->x5_vreg = 0xFF; }
        ph->x6_valid = true;
        ph->x6_dirty = dirty;
        ph->x6_vreg = vreg;
    }
}

static inline void ph_set_i64(PeepholeRegCache* ph, uint8_t vreg, bool dirty) {
    // i64 uses x5(lo)/x6(hi) exclusively, so invalidate i32 cache
    ph->x5_valid = false;
    ph->x6_valid = false;
    ph->x5_dirty = false;
    ph->x6_dirty = false;
    ph->x5_vreg = 0xFF;
    ph->x6_vreg = 0xFF;

    ph->i64_valid = true;
    ph->i64_dirty = dirty;
    ph->i64_vreg = vreg;
}

static inline bool ph_has_i64(const PeepholeRegCache* ph, uint8_t vreg) {
    return ph->i64_valid && (ph->i64_vreg == vreg);
}

static inline uint8_t ph_ensure_loaded(JitContext* ctx, PeepholeRegCache* ph, uint8_t vreg, uint8_t target_phys) {
    int found = ph_find(ph, vreg);
    if (found == 5 || found == 6) return (uint8_t)found;

    // load into target
    emit_lw_phys(ctx, target_phys, (int16_t)(vreg * 8), 18);
    ph_set(ph, target_phys, vreg, false);
    return target_phys;
}

static inline void ph_kill_phys(PeepholeRegCache* ph, uint8_t phys) {
    if (phys == 5) { ph->x5_valid = false; ph->x5_dirty = false; ph->x5_vreg = 0xFF; }
    if (phys == 6) { ph->x6_valid = false; ph->x6_dirty = false; ph->x6_vreg = 0xFF; }
}

static inline void ph_flush(JitContext* ctx, PeepholeRegCache* ph) {
    // write back only dirty cached values
    if (ph->i64_valid && ph->i64_dirty && ph->i64_vreg != 0xFF) {
        // x5 = lo, x6 = hi
        emit_sw_phys(ctx, 5, (int16_t)(ph->i64_vreg * 8), 18);
        emit_sw_phys(ctx, 6, (int16_t)(ph->i64_vreg * 8 + 4), 18);
        ph->i64_dirty = false;
    }

    if (ph->x5_valid && ph->x5_dirty && ph->x5_vreg != 0xFF) {
        emit_sw_phys(ctx, 5, (int16_t)(ph->x5_vreg * 8), 18);
        ph->x5_dirty = false;
    }
    if (ph->x6_valid && ph->x6_dirty && ph->x6_vreg != 0xFF) {
        emit_sw_phys(ctx, 6, (int16_t)(ph->x6_vreg * 8), 18);
        ph->x6_dirty = false;
    }
}

// ===== Live-in analysis for selective flush on BR/BR_IF =====
// Conservative scanner: returns true if vreg is READ in the basic block starting at start_off
// before being definitely overwritten. Unknown opcodes => assume READ.
static bool bb_reads_vreg_before_write(const uint8_t* bytecode_start, const uint8_t* bytecode_end, size_t start_off, uint8_t vreg) {
    const uint8_t* pc = bytecode_start + start_off;
    while (pc < bytecode_end) {
        uint8_t op = *pc++;
        switch (op) {
            case 0x00: // NOP (padding)
            case 0x01: // NOP (alternative)
                // No registers affected
                break;
            
            case 0x05: // UNREACHABLE
                return false; // terminates execution
            
            case 0x02: { // BR
                pc += 2;
                return false; // terminator: no further reads
            }
            case 0x03: { // BR_IF
                pc += 1 + 2;
                return false;
            }
            case 0x0F: { // END
                return false;
            }

            // MOVs: read rs, write rd
            case 0x10: // MOV.I8
            case 0x11: // MOV.I16
            case 0x12: { // MOV.32
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x13: { // MOV.64
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // LDC / LD addr: write rd
            case 0x16: pc += 1 + 2; { uint8_t rd = *(pc-3); if (rd==vreg) return false; } break;
            case 0x18: pc += 1 + 4; { uint8_t rd = *(pc-5); if (rd==vreg) return false; } break;
            case 0x19: pc += 1 + 8; { uint8_t rd = *(pc-9); if (rd==vreg) return false; } break;
            case 0x1A: pc += 1 + 4; { uint8_t rd = *(pc-5); if (rd==vreg) return false; } break;
            case 0x1B: pc += 1 + 8; { uint8_t rd = *(pc-9); if (rd==vreg) return false; } break;
            case 0x1C: pc += 1 + 4; { uint8_t rd = *(pc-5); if (rd==vreg) return false; } break;
            case 0x1D: pc += 1 + 2; { uint8_t rd = *(pc-3); if (rd==vreg) return false; } break;
            case 0x1E: pc += 1 + 2; { uint8_t rd = *(pc-3); if (rd==vreg) return false; } break;

            // I32 ALU rr: read rs1,rs2 write rd
            case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x26: case 0x27:
            case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: {
                uint8_t rd = *pc++; uint8_t rs1 = *pc++; uint8_t rs2 = *pc++;
                if (rs1 == vreg || rs2 == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x2E: { // NOT.I32: read rs write rd
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            // I64 ALU rr and bitwise/shift: read rs1,rs2 write rd
            case 0x30: case 0x31: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: {
                uint8_t rd = *pc++; uint8_t rs1 = *pc++; uint8_t rs2 = *pc++;
                if (rs1 == vreg || rs2 == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x3E: { // NOT.I64: read rs write rd
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // I32 IMM8: read rs write rd
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49:
            case 0x4A: case 0x4B: {
                uint8_t rd = *pc++; uint8_t rs = *pc++; pc += 1;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // Float conversion: FPROMOTE F32->F64 reads rs, writes rd
            case 0xA5: {
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            case 0xAC: { // CVT.F64.I32 reads rs, writes rd
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            case 0xB4: // CVT.I64.F32
            case 0xB5: { // CVT.I64.F64
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            
            // Type conversion (truncate with sign-extend): read rs write rd
            case 0x92: // TRUNC.I64.I8
            case 0x93: // TRUNC.I32.I16
            case 0x94: // TRUNC.I32.I8 alias
            case 0x95: // TRUNC.I16.I8
            case 0x9C: // SEXT.I8.I16
            case 0x9E: { // SEXT.I8.I64
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            
            // Float conversion opcodes: read rs write rd
            case 0xA4: // FPROUND (F64->F32)
            case 0xA6: // CVT.F32.U32
            case 0xA7: // CVT.F32.U64
            case 0xA8: // CVT.F64.U32
            case 0xA9: // CVT.F64.U64
            case 0xAA: // CVT.F32.I32
            case 0xAB: // CVT.F32.I64
            case 0xAD: // CVT.F64.I64
            case 0xAE: // CVT.U32.F32
            case 0xB0: // CVT.U64.F32
            case 0xB2: // CVT.I32.F32
            case 0xB3: { // CVT.I32.F64
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            
            // SELECT opcodes: read cond, rtrue, rfalse; write rd
            case 0xBE: // SELECT.I32
            case 0xBF: { // SELECT.I64
                uint8_t rd = *pc++; uint8_t rcond = *pc++; uint8_t rtrue = *pc++; uint8_t rfalse = *pc++;
                if (rcond == vreg || rtrue == vreg || rfalse == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // I64 IMM8: read rs write rd
            case 0x50: // ADD.I64.IMM8
            case 0x51: // SUB.I64.IMM8
            case 0x52: // MUL.I64.IMM8
            case 0x58: { // SHRU.I64.IMM8
                uint8_t rd = *pc++; uint8_t rs = *pc++; pc += 1;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // F32 ops: 0x60..0x65 are binary, 0x66..0x67 are unary
            case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: {
                uint8_t rd = *pc++; uint8_t rs1 = *pc++; uint8_t rs2 = *pc++;
                if (rs1 == vreg || rs2 == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x66: // ABS.F32
            case 0x67: { // SQRT.F32
                uint8_t rd = *pc++; uint8_t rs = *pc++;
                if (rs == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            // LOAD: reads ra, writes rd
            case 0x80: // LOAD.I8
            case 0x81: // LOAD.I8U
            case 0x82: // LOAD.I16S
            case 0x83: // LOAD.U16
            case 0x89: // LOAD.BOOL
            case 0x84: { // LOAD.I32
                uint8_t rd = *pc++; uint8_t ra = *pc++; pc += 2;
                if (ra == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x86: { // LOAD.F32
                uint8_t rd = *pc++; uint8_t ra = *pc++; pc += 2;
                if (ra == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x87: { // LOAD.F64
                uint8_t rd = *pc++; uint8_t ra = *pc++; pc += 2;
                if (ra == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x88: { // LOAD.PTR
                uint8_t rd = *pc++; uint8_t ra = *pc++; pc += 2;
                if (ra == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            case 0x85: { // LOAD.I64
                uint8_t rd = *pc++; uint8_t ra = *pc++; pc += 2;
                if (ra == vreg) return true;
                if (rd == vreg) return false;
                break;
            }
            // STORE: reads rs,ra
            case 0x70: // STORE.I8
            case 0x71: // STORE.U8
            case 0x72: // STORE.I16
            case 0x73: // STORE.U16
            case 0x7B: // STORE.BOOL
            case 0x74: { // STORE.I32
                uint8_t rs = *pc++; uint8_t ra = *pc++; pc += 2;
                if (rs == vreg || ra == vreg) return true;
                break;
            }
            case 0x78: { // STORE.F32
                uint8_t rs = *pc++; uint8_t ra = *pc++; pc += 2;
                if (rs == vreg || ra == vreg) return true;
                break;
            }
            case 0x79: { // STORE.F64
                uint8_t rs = *pc++; uint8_t ra = *pc++; pc += 2;
                if (rs == vreg || ra == vreg) return true;
                break;
            }
            case 0x76: { // STORE.I64
                uint8_t rs = *pc++; uint8_t ra = *pc++; pc += 2;
                if (rs == vreg || ra == vreg) return true;
                break;
            }

            // CMP* are pure reads (write rd), so for liveness we only care if vreg read
            case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7: case 0xC8: case 0xC9:
            case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xE8: case 0xE9: case 0xEA: case 0xEB: {
                uint8_t rd = *pc++; uint8_t rs1 = *pc++; uint8_t rs2 = *pc++;
                if (rs1 == vreg || rs2 == vreg) return true;
                if (rd == vreg) return false;
                break;
            }

            default:
                // unknown -> conservative assume it reads vreg
                return true;
        }
    }
    return false;
}

static inline void ph_flush_selective_for_branch(JitContext* ctx, PeepholeRegCache* ph,
                                                const uint8_t* bytecode_start, const uint8_t* bytecode_end,
                                                size_t target_off, size_t fallthrough_off) {
    // i64 cached value
    if (ph->i64_valid && ph->i64_dirty && ph->i64_vreg != 0xFF) {
        bool need = bb_reads_vreg_before_write(bytecode_start, bytecode_end, target_off, ph->i64_vreg) ||
                    bb_reads_vreg_before_write(bytecode_start, bytecode_end, fallthrough_off, ph->i64_vreg);
        if (need) {
            emit_sw_phys(ctx, 5, (int16_t)(ph->i64_vreg * 8), 18);
            emit_sw_phys(ctx, 6, (int16_t)(ph->i64_vreg * 8 + 4), 18);
        }
        ph->i64_dirty = false;
    }

    if (ph->x5_valid && ph->x5_dirty && ph->x5_vreg != 0xFF) {
        bool need = bb_reads_vreg_before_write(bytecode_start, bytecode_end, target_off, ph->x5_vreg) ||
                    bb_reads_vreg_before_write(bytecode_start, bytecode_end, fallthrough_off, ph->x5_vreg);
        if (need) {
            emit_sw_phys(ctx, 5, (int16_t)(ph->x5_vreg * 8), 18);
        }
        ph->x5_dirty = false;
    }

    if (ph->x6_valid && ph->x6_dirty && ph->x6_vreg != 0xFF) {
        bool need = bb_reads_vreg_before_write(bytecode_start, bytecode_end, target_off, ph->x6_vreg) ||
                    bb_reads_vreg_before_write(bytecode_start, bytecode_end, fallthrough_off, ph->x6_vreg);
        if (need) {
            emit_sw_phys(ctx, 6, (int16_t)(ph->x6_vreg * 8), 18);
        }
        ph->x6_dirty = false;
    }
}

// Helper функция для вызова ESPB функций из JIT кода
// Сигнатура: void jit_call_espb_function(EspbInstance* instance, uint32_t local_func_idx, Value* v_regs)
// Примечание: local_func_idx - это локальный индекс (без учета импортов), как в инструкции CALL
void jit_call_espb_function(EspbInstance* instance, uint32_t local_func_idx, Value* v_regs) {
    if (!instance || !v_regs) {
        return;
    }
    
    // ОПТИМИЗАЦИЯ #4: Используем thread-local storage для ExecutionContext
    // чтобы избежать создания нового контекста при каждом вызове
    static __thread ExecutionContext* temp_exec_ctx = NULL;
    if (!temp_exec_ctx) {
        temp_exec_ctx = init_execution_context();
        if (!temp_exec_ctx) {
            printf("[jit] ERROR: Failed to create ExecutionContext\n");
            return;
        }
    }
    
    const EspbModule* module = instance->module;
    
    // Проверяем валидность локального индекса
    if (local_func_idx >= module->num_functions) {
        return;
    }
    
    // Преобразуем локальный индекс в глобальный (добавляем num_imported_funcs)
    uint32_t num_imported_funcs = module->num_imported_funcs;
    uint32_t global_func_idx = local_func_idx + num_imported_funcs;
    
    // Определяем сигнатуру функции
    uint16_t sig_idx = module->function_signature_indices[local_func_idx];
    EspbFuncSignature* sig = &module->signatures[sig_idx];
    uint8_t num_args = sig->num_params;
    if (num_args > 8) num_args = 8; // Ограничиваем первыми 8 аргументами

    EspbFunctionBody *callee_body = &((EspbModule*)module)->function_bodies[local_func_idx];
    
    // ИНТЕГРАЦИЯ HOT: Проверяем флаг HOT перед попыткой компиляции
    bool is_hot = (callee_body->header.flags & ESPB_FUNC_FLAG_HOT) != 0;
    
    if (!is_hot) {
        // Не-HOT функция - сразу вызываем через интерпретатор (не пытаемся компилировать)
        Value args[8];
        for (uint8_t i = 0; i < num_args; i++) {
            args[i] = v_regs[i];
        }
        
        Value result;
        memset(&result, 0, sizeof(result));
        espb_call_function(instance, temp_exec_ctx, global_func_idx, args, &result);
        
        if (sig->num_returns > 0) {
            v_regs[0] = result;
        }
        return;
    }
    
    // FAST PATH: если HOT функция уже JIT-скомпилирована, вызываем напрямую, минуя диспетчер
    if (callee_body->is_jit_compiled && callee_body->jit_code != NULL) {
        typedef void (*JitFunc)(EspbInstance*, Value*);
        JitFunc jit_func = (JitFunc)callee_body->jit_code;

        // Локальный регистровый фрейм для вызываемой функции (семантика CALL)
        // КРИТИЧНО: нельзя выделять 256 Value на каждый рекурсивный вызов (переполнит стек)
        uint16_t needed_regs = callee_body->header.num_virtual_regs;
        if (needed_regs == 0 || needed_regs > 256) needed_regs = 256;

        // Fast-path frame: используем alloca (быстрее для рекурсии, лучше локальность, чем heap)
        Value *callee_regs = (Value*)alloca((size_t)needed_regs * sizeof(Value));
        if (!callee_regs) {
            goto slow_path;
        }

        // ОПТИМИЗАЦИЯ: не всегда нужно занулять все num_virtual_regs.
        uint16_t max_used = (uint16_t)callee_body->header.max_reg_used + 1;
        uint16_t zero_regs = needed_regs;
        if (max_used > 0 && max_used < zero_regs) zero_regs = max_used;
        if (zero_regs < num_args) zero_regs = num_args;
        if (zero_regs == 0) zero_regs = 1;

        memset(callee_regs, 0, (size_t)zero_regs * sizeof(Value));
        for (uint8_t i = 0; i < num_args; i++) {
            callee_regs[i] = v_regs[i];
        }

        jit_func(instance, callee_regs);

        if (sig->num_returns > 0) {
            v_regs[0] = callee_regs[0];
        }

        return;

slow_path:
        ;
    }

    // SLOW PATH: функция ещё не скомпилирована — используем диспетчер (компиляция + вызов)
    Value args[8];
    for (uint8_t i = 0; i < num_args; i++) {
        args[i] = v_regs[i];
    }

    Value result;
    EspbResult call_res = espb_execute_function_jit_only(instance, temp_exec_ctx, global_func_idx, args, &result);
    if (call_res != ESPB_OK) {
        // Fallback на интерпретатор, если JIT не смог скомпилировать функцию
        printf("[jit] JIT failed for func_idx=%u (error %d), falling back to interpreter\n", global_func_idx, call_res);
        call_res = espb_execute_function(instance, temp_exec_ctx, global_func_idx, args, &result);
        if (call_res != ESPB_OK) {
            printf("[jit] ERROR: Interpreter also failed for func_idx=%u with error %d\n", global_func_idx, call_res);
            memset(&result, 0, sizeof(result));
        }
    }

    if (sig->num_returns > 0) {
        v_regs[0] = result;
    }
}

// ОТКАТ ОПТИМИЗАЦИИ #5: Регистровая оптимизация неэффективна
// 
// Проблема: Многие инструкции (ALLOCA, CALL_IMPORT, и др.) читают v_regs[] напрямую,
// поэтому мы ВСЕГДА должны держать актуальные данные в памяти. Это приводит к тому что:
// 1. Нужно всегда писать в v_regs[] (store)
// 2. Физические регистры дублируют данные
// 3. Получается БОЛЬШЕ операций вместо меньше!
//
// Результат: 5013 → 5048 циклов (хуже!)
//
// ВСЕ виртуальные регистры работают через память v_regs[]

static uint8_t map_vreg_to_phys(uint8_t vreg) {
    (void)vreg;
    return 0;  // Все регистры в памяти
}

static inline void jit_icache_sync(void* addr, size_t size) {
    // Ensure generated code is visible to instruction fetch.
    esp_cache_msync(addr, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

static void emit_instr(JitContext* ctx, uint32_t instr) {
    if (ctx->offset + 4 > ctx->capacity) {
        printf("JIT ERROR: Buffer overflow at offset %zu!\n", ctx->offset);
        return;
    }
    
    // Валидация RISC-V инструкции
    uint32_t opcode = instr & 0x7F;
    
    // Проверяем что opcode валиден (должен быть одним из стандартных)
    bool valid = false;
    switch (opcode) {
        case 0b0110011: // R-type (ADD, SUB, MUL, DIV, etc.)
        case 0b0010011: // I-type (ADDI, SLLI, etc.)
        case 0b0000011: // Load
        case 0b0100011: // Store
        case 0b1100011: // Branch
        case 0b1101111: // JAL
        case 0b1100111: // JALR
        case 0b0110111: // LUI
        case 0b0010111: // AUIPC
        case 0b0101111: // AMO (Atomic Memory Operations)
            valid = true;
            break;
        default:
            if (instr == 0 || instr == 0xFFFFFFFF) {
                printf("JIT ERROR: Invalid instruction 0x%08x (opcode 0x%02x) at offset %zu\n", 
                       instr, opcode, ctx->offset);
            }
            break;
    }
    
    memcpy(ctx->buffer + ctx->offset, &instr, sizeof(instr));
    ctx->offset += sizeof(instr);
}

// ===== RVC (RISC-V Compressed) 16-bit instructions =====
// All ESP32 RISC-V chips support 'C' extension (RV32IMC/RV32IMAC)

static void emit_instr16(JitContext* ctx, uint16_t instr) {
    if (ctx->offset + 2 > ctx->capacity) {
        printf("JIT ERROR: Buffer overflow at offset %zu!\n", ctx->offset);
        return;
    }
    memcpy(ctx->buffer + ctx->offset, &instr, sizeof(instr));
    ctx->offset += sizeof(instr);
}

// c.li rd, imm - Load immediate [-32..31] into rd (rd != x0)
// Format: [15:13]=010 [12]=imm[5] [11:7]=rd [6:2]=imm[4:0] [1:0]=01
static inline void emit_c_li(JitContext* ctx, uint8_t rd, int8_t imm) {
    // imm is 6-bit signed [-32..31]
    uint16_t imm5 = (imm >> 5) & 1;         // bit 5 (sign extension)
    uint16_t imm4_0 = imm & 0x1F;           // bits 4:0
    uint16_t instr = (0b010 << 13) | (imm5 << 12) | (rd << 7) | (imm4_0 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.mv rd, rs2 - Move rs2 to rd (rd != x0, rs2 != x0)
// Format: [15:12]=1000 [11:7]=rd [6:2]=rs2 [1:0]=10
static inline void emit_c_mv(JitContext* ctx, uint8_t rd, uint8_t rs2) {
    uint16_t instr = (0b1000 << 12) | (rd << 7) | (rs2 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.addi rd, imm - Add immediate [-32..31] to rd (rd != x0, imm != 0)
// Format: [15:13]=000 [12]=imm[5] [11:7]=rd [6:2]=imm[4:0] [1:0]=01
static inline void emit_c_addi(JitContext* ctx, uint8_t rd, int8_t imm) {
    uint16_t imm5 = (imm >> 5) & 1;
    uint16_t imm4_0 = imm & 0x1F;
    uint16_t instr = (0b000 << 13) | (imm5 << 12) | (rd << 7) | (imm4_0 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.add rd, rs2 - Add rs2 to rd (rd != x0, rs2 != x0)  
// Format: [15:12]=1001 [11:7]=rd [6:2]=rs2 [1:0]=10
static inline void emit_c_add(JitContext* ctx, uint8_t rd, uint8_t rs2) {
    uint16_t instr = (0b1001 << 12) | (rd << 7) | (rs2 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.jr rs1 - Jump to address in rs1 (rs1 != x0)
// Format: [15:12]=1000 [11:7]=rs1 [6:2]=00000 [1:0]=10
static inline void emit_c_jr(JitContext* ctx, uint8_t rs1) {
    uint16_t instr = (0b1000 << 12) | (rs1 << 7) | (0 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.jalr rs1 - Jump and link to address in rs1 (rs1 != x0)
// Format: [15:12]=1001 [11:7]=rs1 [6:2]=00000 [1:0]=10
static inline void emit_c_jalr(JitContext* ctx, uint8_t rs1) {
    uint16_t instr = (0b1001 << 12) | (rs1 << 7) | (0 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.slli rd, shamt - Shift left logical immediate (rd != x0, shamt != 0)
// Format: [15:13]=000 [12]=shamt[5] [11:7]=rd [6:2]=shamt[4:0] [1:0]=10
static inline void emit_c_slli(JitContext* ctx, uint8_t rd, uint8_t shamt) {
    uint16_t shamt5 = (shamt >> 5) & 1;
    uint16_t shamt4_0 = shamt & 0x1F;
    uint16_t instr = (0b000 << 13) | (shamt5 << 12) | (rd << 7) | (shamt4_0 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.lui rd, imm - Load upper immediate (rd != x0, rd != x2, imm != 0)
// Format: [15:13]=011 [12]=imm[17] [11:7]=rd [6:2]=imm[16:12] [1:0]=01
// Note: imm is bits [17:12], scaled by 0x1000
static inline void emit_c_lui(JitContext* ctx, uint8_t rd, int8_t imm6) {
    // imm6 is 6-bit signed representing bits [17:12] of the immediate
    uint16_t imm17 = (imm6 >> 5) & 1;
    uint16_t imm16_12 = imm6 & 0x1F;
    uint16_t instr = (0b011 << 13) | (imm17 << 12) | (rd << 7) | (imm16_12 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.lwsp rd, offset(sp) - Load word from stack (rd != x0, offset is 6-bit scaled by 4)
// Format: [15:13]=010 [12]=offset[5] [11:7]=rd [6:4]=offset[4:2] [3:2]=offset[7:6] [1:0]=10
// Offset range: 0..252 (must be multiple of 4)
static inline void emit_c_lwsp(JitContext* ctx, uint8_t rd, uint8_t offset) {
    // offset bits: [5] [4:2] [7:6] -> scaled offset = offset * 4
    uint16_t off5 = (offset >> 5) & 1;
    uint16_t off4_2 = (offset >> 2) & 0x7;
    uint16_t off7_6 = (offset >> 6) & 0x3;
    uint16_t instr = (0b010 << 13) | (off5 << 12) | (rd << 7) | (off4_2 << 4) | (off7_6 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.swsp rs2, offset(sp) - Store word to stack (offset is 6-bit scaled by 4)
// Format: [15:13]=110 [12:9]=offset[5:2] [8:7]=offset[7:6] [6:2]=rs2 [1:0]=10
// Offset range: 0..252 (must be multiple of 4)
static inline void emit_c_swsp(JitContext* ctx, uint8_t rs2, uint8_t offset) {
    // offset bits: [5:2] [7:6]
    uint16_t off5_2 = (offset >> 2) & 0xF;
    uint16_t off7_6 = (offset >> 6) & 0x3;
    uint16_t instr = (0b110 << 13) | (off5_2 << 9) | (off7_6 << 7) | (rs2 << 2) | 0b10;
    emit_instr16(ctx, instr);
}

// c.lw rd', offset(rs1') - Load word (rd' and rs1' are x8-x15 only)
// Format: [15:13]=010 [12:10]=offset[5:3] [9:7]=rs1' [6:5]=offset[2|6] [4:2]=rd' [1:0]=00
// Offset range: 0..124 (must be multiple of 4)
static inline void emit_c_lw(JitContext* ctx, uint8_t rd_prime, uint8_t rs1_prime, uint8_t offset) {
    // rd_prime and rs1_prime are 3-bit (represent x8-x15)
    uint16_t off5_3 = (offset >> 3) & 0x7;
    uint16_t off2 = (offset >> 2) & 0x1;
    uint16_t off6 = (offset >> 6) & 0x1;
    uint16_t instr = (0b010 << 13) | (off5_3 << 10) | (rs1_prime << 7) | (off6 << 6) | (off2 << 5) | (rd_prime << 2) | 0b00;
    emit_instr16(ctx, instr);
}

// c.sw rs2', offset(rs1') - Store word (rs2' and rs1' are x8-x15 only)
// Format: [15:13]=110 [12:10]=offset[5:3] [9:7]=rs1' [6:5]=offset[2|6] [4:2]=rs2' [1:0]=00
// Offset range: 0..124 (must be multiple of 4)
static inline void emit_c_sw(JitContext* ctx, uint8_t rs2_prime, uint8_t rs1_prime, uint8_t offset) {
    uint16_t off5_3 = (offset >> 3) & 0x7;
    uint16_t off2 = (offset >> 2) & 0x1;
    uint16_t off6 = (offset >> 6) & 0x1;
    uint16_t instr = (0b110 << 13) | (off5_3 << 10) | (rs1_prime << 7) | (off6 << 6) | (off2 << 5) | (rs2_prime << 2) | 0b00;
    emit_instr16(ctx, instr);
}

// c.sub rd', rs2' - Subtract (rd' = rd' - rs2'), rd' and rs2' are x8-x15
// Format: [15:10]=100011 [9:7]=rd' [6:5]=00 [4:2]=rs2' [1:0]=01
static inline void emit_c_sub(JitContext* ctx, uint8_t rd_prime, uint8_t rs2_prime) {
    uint16_t instr = (0b100011 << 10) | (rd_prime << 7) | (0b00 << 5) | (rs2_prime << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.and rd', rs2' - AND (rd' = rd' & rs2'), rd' and rs2' are x8-x15
// Format: [15:10]=100011 [9:7]=rd' [6:5]=11 [4:2]=rs2' [1:0]=01
static inline void emit_c_and(JitContext* ctx, uint8_t rd_prime, uint8_t rs2_prime) {
    uint16_t instr = (0b100011 << 10) | (rd_prime << 7) | (0b11 << 5) | (rs2_prime << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.or rd', rs2' - OR (rd' = rd' | rs2'), rd' and rs2' are x8-x15
// Format: [15:10]=100011 [9:7]=rd' [6:5]=10 [4:2]=rs2' [1:0]=01
static inline void emit_c_or(JitContext* ctx, uint8_t rd_prime, uint8_t rs2_prime) {
    uint16_t instr = (0b100011 << 10) | (rd_prime << 7) | (0b10 << 5) | (rs2_prime << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.xor rd', rs2' - XOR (rd' = rd' ^ rs2'), rd' and rs2' are x8-x15
// Format: [15:10]=100011 [9:7]=rd' [6:5]=01 [4:2]=rs2' [1:0]=01
static inline void emit_c_xor(JitContext* ctx, uint8_t rd_prime, uint8_t rs2_prime) {
    uint16_t instr = (0b100011 << 10) | (rd_prime << 7) | (0b01 << 5) | (rs2_prime << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.srli rd', shamt - Shift right logical immediate (rd' is x8-x15)
// Format: [15:13]=100 [12]=shamt[5] [11:10]=00 [9:7]=rd' [6:2]=shamt[4:0] [1:0]=01
static inline void emit_c_srli(JitContext* ctx, uint8_t rd_prime, uint8_t shamt) {
    uint16_t shamt5 = (shamt >> 5) & 1;
    uint16_t shamt4_0 = shamt & 0x1F;
    uint16_t instr = (0b100 << 13) | (shamt5 << 12) | (0b00 << 10) | (rd_prime << 7) | (shamt4_0 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.srai rd', shamt - Shift right arithmetic immediate (rd' is x8-x15)
// Format: [15:13]=100 [12]=shamt[5] [11:10]=01 [9:7]=rd' [6:2]=shamt[4:0] [1:0]=01
static inline void emit_c_srai(JitContext* ctx, uint8_t rd_prime, uint8_t shamt) {
    uint16_t shamt5 = (shamt >> 5) & 1;
    uint16_t shamt4_0 = shamt & 0x1F;
    uint16_t instr = (0b100 << 13) | (shamt5 << 12) | (0b01 << 10) | (rd_prime << 7) | (shamt4_0 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.andi rd', imm - AND immediate (rd' is x8-x15, imm is 6-bit signed)
// Format: [15:13]=100 [12]=imm[5] [11:10]=10 [9:7]=rd' [6:2]=imm[4:0] [1:0]=01
static inline void emit_c_andi(JitContext* ctx, uint8_t rd_prime, int8_t imm) {
    uint16_t imm5 = (imm >> 5) & 1;
    uint16_t imm4_0 = imm & 0x1F;
    uint16_t instr = (0b100 << 13) | (imm5 << 12) | (0b10 << 10) | (rd_prime << 7) | (imm4_0 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.beqz rs1', offset - Branch if rs1' == 0 (rs1' is x8-x15)
// Format: [15:13]=110 [12:10]=offset[8|4:3] [9:7]=rs1' [6:2]=offset[7:6|2:1|5] [1:0]=01
// Offset range: -256..+254 (9-bit signed, scaled by 2)
static inline void emit_c_beqz(JitContext* ctx, uint8_t rs1_prime, int16_t offset) {
    uint16_t off8 = (offset >> 8) & 1;
    uint16_t off4_3 = (offset >> 3) & 0x3;
    uint16_t off7_6 = (offset >> 6) & 0x3;
    uint16_t off2_1 = (offset >> 1) & 0x3;
    uint16_t off5 = (offset >> 5) & 1;
    uint16_t instr = (0b110 << 13) | (off8 << 12) | (off4_3 << 10) | (rs1_prime << 7) | (off7_6 << 5) | (off2_1 << 3) | (off5 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.bnez rs1', offset - Branch if rs1' != 0 (rs1' is x8-x15)
// Format: [15:13]=111 [12:10]=offset[8|4:3] [9:7]=rs1' [6:2]=offset[7:6|2:1|5] [1:0]=01
static inline void emit_c_bnez(JitContext* ctx, uint8_t rs1_prime, int16_t offset) {
    uint16_t off8 = (offset >> 8) & 1;
    uint16_t off4_3 = (offset >> 3) & 0x3;
    uint16_t off7_6 = (offset >> 6) & 0x3;
    uint16_t off2_1 = (offset >> 1) & 0x3;
    uint16_t off5 = (offset >> 5) & 1;
    uint16_t instr = (0b111 << 13) | (off8 << 12) | (off4_3 << 10) | (rs1_prime << 7) | (off7_6 << 5) | (off2_1 << 3) | (off5 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// c.j offset - Unconditional jump (no link)
// Format: [15:13]=101 [12:2]=offset[11|4|9:8|10|6|7|3:1|5] [1:0]=01
// Offset range: -2048..+2046 (12-bit signed, scaled by 2)
static inline void emit_c_j(JitContext* ctx, int16_t offset) {
    uint16_t off11 = (offset >> 11) & 1;
    uint16_t off4 = (offset >> 4) & 1;
    uint16_t off9_8 = (offset >> 8) & 0x3;
    uint16_t off10 = (offset >> 10) & 1;
    uint16_t off6 = (offset >> 6) & 1;
    uint16_t off7 = (offset >> 7) & 1;
    uint16_t off3_1 = (offset >> 1) & 0x7;
    uint16_t off5 = (offset >> 5) & 1;
    uint16_t instr = (0b101 << 13) | (off11 << 12) | (off4 << 11) | (off9_8 << 9) | (off10 << 8) | (off6 << 7) | (off7 << 6) | (off3_1 << 3) | (off5 << 2) | 0b01;
    emit_instr16(ctx, instr);
}

// Helper: Check if offset fits c.beqz/c.bnez range (-256..+254)
static inline bool offset_fits_cb(int16_t offset) {
    return offset >= -256 && offset <= 254 && (offset & 1) == 0;
}

// Helper: Check if offset fits c.j range (-2048..+2046)
static inline bool offset_fits_cj(int16_t offset) {
    return offset >= -2048 && offset <= 2046 && (offset & 1) == 0;
}

// Helper: Check if immediate fits in 6-bit signed range [-32..31]
static inline bool imm_fits_6bit(int32_t imm) {
    return imm >= -32 && imm <= 31;
}

// Helper: Check if register is in compressed range x8-x15 (returns 3-bit encoding)
static inline bool reg_is_compressed(uint8_t reg, uint8_t* reg_prime) {
    if (reg >= 8 && reg <= 15) {
        *reg_prime = reg - 8;
        return true;
    }
    return false;
}

// Helper: Check if offset is valid for c.lwsp/c.swsp (0..252, multiple of 4)
static inline bool offset_fits_lwsp(uint32_t offset) {
    return (offset <= 252) && ((offset & 3) == 0);
}

// Helper: Check if offset is valid for c.lw/c.sw (0..124, multiple of 4)
static inline bool offset_fits_clw(uint32_t offset) {
    return (offset <= 124) && ((offset & 3) == 0);
}

// ===== End of RVC instructions =====

static void emit_addi_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, int16_t imm) {
    // Use compressed c.addi when: rd == rs1, rd != 0, imm != 0, and imm fits in 6 bits
    if (rd == rs1 && rd != 0 && imm != 0 && imm_fits_6bit(imm)) {
        emit_c_addi(ctx, rd, (int8_t)imm);
        return;
    }
    // Use compressed c.li when: rs1 == 0, rd != 0, and imm fits in 6 bits (load immediate)
    if (rs1 == 0 && rd != 0 && imm_fits_6bit(imm)) {
        emit_c_li(ctx, rd, (int8_t)imm);
        return;
    }
    // Fall back to regular 32-bit addi
    uint32_t imm_bits = ((uint32_t)imm & 0xFFF) << 20;
    emit_instr(ctx, imm_bits | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0010011);
}

// Forward declarations for large offset helpers
static void emit_lui_phys(JitContext* ctx, uint8_t rd, uint32_t imm);
static void emit_add_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2);

static void emit_sw_phys(JitContext* ctx, uint8_t rs2, int16_t offset, uint8_t rs1) {
    if (offset < -2048 || offset > 2047) {
        uint32_t abs_off = (offset < 0) ? -offset : offset;
        emit_lui_phys(ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
        emit_addi_phys(ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
        if (offset < 0) { emit_instr(ctx, (0x40 << 25) | (28 << 20) | (0 << 15) | (0 << 12) | (28 << 7) | 0b0110011); }
        emit_add_phys(ctx, 28, rs1, 28);
        emit_instr(ctx, (0 << 25) | (rs2 << 20) | (28 << 15) | (0b010 << 12) | (0 << 7) | 0b0100011);
        return;
    }
    // Try compressed c.swsp when storing to stack pointer (sp = x2)
    if (rs1 == 2 && offset >= 0 && offset_fits_lwsp((uint32_t)offset)) {
        emit_c_swsp(ctx, rs2, (uint8_t)offset);
        return;
    }
    // Try compressed c.sw when both registers are x8-x15 and offset fits
    uint8_t rs2_prime, rs1_prime;
    if (offset >= 0 && reg_is_compressed(rs2, &rs2_prime) && reg_is_compressed(rs1, &rs1_prime) && offset_fits_clw((uint32_t)offset)) {
        emit_c_sw(ctx, rs2_prime, rs1_prime, (uint8_t)offset);
        return;
    }
    // Fall back to regular 32-bit sw
    uint32_t off = (uint32_t)offset & 0xFFF;
    uint32_t imm11_5 = (off >> 5) & 0x7F;
    uint32_t imm4_0 = off & 0x1F;
    emit_instr(ctx, (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (imm4_0 << 7) | 0b0100011);
}

static void emit_sb_phys(JitContext* ctx, uint8_t rs2, int16_t offset, uint8_t rs1) {
    uint32_t off = (uint32_t)offset & 0xFFF;
    uint32_t imm11_5 = (off >> 5) & 0x7F;
    uint32_t imm4_0 = off & 0x1F;
    emit_instr(ctx, (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | (imm4_0 << 7) | 0b0100011);
}

static void emit_lw_phys(JitContext* ctx, uint8_t rd, int16_t offset, uint8_t rs1) {
    if (offset < -2048 || offset > 2047) {
        uint32_t abs_off = (offset < 0) ? -offset : offset;
        emit_lui_phys(ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
        emit_addi_phys(ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
        if (offset < 0) { emit_instr(ctx, (0x40 << 25) | (28 << 20) | (0 << 15) | (0 << 12) | (28 << 7) | 0b0110011); }
        emit_add_phys(ctx, 28, rs1, 28);
        emit_instr(ctx, (0 << 20) | (28 << 15) | (0b010 << 12) | (rd << 7) | 0b0000011);
        return;
    }
    // Try compressed c.lwsp when loading from stack pointer (sp = x2)
    if (rs1 == 2 && rd != 0 && offset >= 0 && offset_fits_lwsp((uint32_t)offset)) {
        emit_c_lwsp(ctx, rd, (uint8_t)offset);
        return;
    }
    // Try compressed c.lw when both registers are x8-x15 and offset fits
    uint8_t rd_prime, rs1_prime;
    if (offset >= 0 && reg_is_compressed(rd, &rd_prime) && reg_is_compressed(rs1, &rs1_prime) && offset_fits_clw((uint32_t)offset)) {
        emit_c_lw(ctx, rd_prime, rs1_prime, (uint8_t)offset);
        return;
    }
    // Fall back to regular 32-bit lw
    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
    emit_instr(ctx, imm_bits | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0000011);
}

static void emit_sd_phys(JitContext* ctx, uint8_t rs2, int16_t offset, uint8_t rs1) {
    uint32_t off = (uint32_t)offset & 0xFFF;
    uint32_t imm11_5 = (off >> 5) & 0x7F;
    uint32_t imm4_0 = off & 0x1F;
    emit_instr(ctx, (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b011 << 12) | (imm4_0 << 7) | 0b0100011);
}

static void emit_ld_phys(JitContext* ctx, uint8_t rd, int16_t offset, uint8_t rs1) {
    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
    emit_instr(ctx, imm_bits | (rs1 << 15) | (0b011 << 12) | (rd << 7) | 0b0000011);
}

static void emit_lui_phys(JitContext* ctx, uint8_t rd, uint32_t imm) {
    // c.lui can load imm[17:12] into rd (scaled by 0x1000)
    // Effective range: imm in [-131072, 131071] * 0x1000, excluding 0
    // Check if imm[31:18] and imm[11:0] are all zeros or all ones (sign extended)
    // and imm != 0, rd != 0, rd != 2 (sp)
    uint32_t upper = imm >> 12;  // bits [31:12]
    if (rd != 0 && rd != 2 && upper != 0) {
        // c.lui uses 6-bit signed immediate for bits [17:12]
        // Check if upper fits in 6-bit signed: [-32..31] but != 0
        int32_t imm6 = (int32_t)upper;
        if (imm6 >= -32 && imm6 <= 31 && imm6 != 0) {
            emit_c_lui(ctx, rd, (int8_t)imm6);
            return;
        }
    }
    emit_instr(ctx, (imm & 0xFFFFF000) | (rd << 7) | 0b0110111);
}

static void emit_jalr_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, int16_t imm) {
    // Use compressed c.jr when: rd == 0, imm == 0, rs1 != 0 (just jump, no link)
    if (rd == 0 && imm == 0 && rs1 != 0) {
        emit_c_jr(ctx, rs1);
        return;
    }
    // Use compressed c.jalr when: rd == 1 (ra), imm == 0, rs1 != 0 (jump and link to ra)
    if (rd == 1 && imm == 0 && rs1 != 0) {
        emit_c_jalr(ctx, rs1);
        return;
    }
    uint32_t imm_bits = ((uint32_t)imm & 0xFFF) << 20;
    emit_instr(ctx, imm_bits | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b1100111);
}

static void emit_call_helper(JitContext* ctx, uintptr_t func_addr) {
    // NOTE: s1(x9) and s2(x18) are callee-saved by ABI, so helper should preserve them.
    // We rely on this instead of manually saving/restoring, which was causing issues.

#ifdef JIT_STATS
    ctx->helper_call_count++;
#endif

    // Fast path: PC-relative auipc+jalr (2 instructions) if reachable.
    // Fallback: absolute lui+addi+jalr (3 instructions).
    uintptr_t call_pc = (uintptr_t)(ctx->buffer + ctx->offset);
    int64_t rel = (int64_t)func_addr - (int64_t)call_pc;

    if (rel > (int64_t)INT32_MAX || rel < (int64_t)INT32_MIN) {
#ifdef JIT_STATS
        ctx->helper_call_fallback_abs_count++;
#endif
        uint32_t hi20 = (uint32_t)((func_addr + 0x800) >> 12);
        uint32_t lo12 = (uint32_t)(func_addr & 0xFFF);
        emit_instr(ctx, (hi20 << 12) | (5 << 7) | 0x37); // lui t0
        emit_instr(ctx, (lo12 << 20) | (5 << 15) | (0x0 << 12) | (5 << 7) | 0x13); // addi t0,t0,lo12
        emit_jalr_phys(ctx, 1, 5, 0);
        // Fall through to restore s1/s2
    }
    else {
        int64_t hi20 = (rel + 0x800) >> 12;
        int64_t lo12 = rel - (hi20 << 12);
        uint32_t auipc_imm = ((uint32_t)hi20 & 0xFFFFF) << 12;
        emit_instr(ctx, auipc_imm | (5u << 7) | 0b0010111);
        emit_jalr_phys(ctx, 1, 5, (int16_t)lo12);
    }

    // s1/s2 preserved by callee (ABI), no need to restore
}


static void emit_add_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    // Use compressed c.mv when: rs1 == 0 (move rs2 to rd), rd != 0, rs2 != 0
    if (rs1 == 0 && rd != 0 && rs2 != 0) {
        emit_c_mv(ctx, rd, rs2);
        return;
    }
    // Use compressed c.mv when: rs2 == 0 (move rs1 to rd), rd != 0, rs1 != 0
    if (rs2 == 0 && rd != 0 && rs1 != 0) {
        emit_c_mv(ctx, rd, rs1);
        return;
    }
    // Use compressed c.add when: rd == rs1, rd != 0, rs2 != 0
    if (rd == rs1 && rd != 0 && rs2 != 0) {
        emit_c_add(ctx, rd, rs2);
        return;
    }
    // ADD rd, rs1, rs2: opcode=0110011, funct3=000, funct7=0000000
    emit_instr(ctx, (0b0000000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0110011);
}

static void emit_sub_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    // Use compressed c.sub when: rd == rs1, both rd and rs2 are x8-x15
    uint8_t rd_prime, rs2_prime;
    if (rd == rs1 && reg_is_compressed(rd, &rd_prime) && reg_is_compressed(rs2, &rs2_prime)) {
        emit_c_sub(ctx, rd_prime, rs2_prime);
        return;
    }
    // SUB rd, rs1, rs2: opcode=0110011, funct3=000, funct7=0100000
    emit_instr(ctx, (0b0100000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0110011);
}

static void emit_beq_phys(JitContext* ctx, uint8_t rs1, uint8_t rs2, int16_t offset) {
    // BEQ rs1, rs2, offset: opcode=1100011, funct3=000
    uint32_t imm = (uint32_t)offset & 0x1FFE;
    uint32_t imm12 = (imm >> 12) & 0x1;
    uint32_t imm11 = (imm >> 11) & 0x1;
    uint32_t imm10_5 = (imm >> 5) & 0x3F;
    uint32_t imm4_1 = (imm >> 1) & 0xF;
    
    emit_instr(ctx, (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) | 
               (0b000 << 12) | (imm4_1 << 8) | (imm11 << 7) | 0b1100011);
}

static uint32_t encode_branch_instr(uint8_t funct3, uint8_t rs1, uint8_t rs2, int16_t offset) {
    // Branch: opcode=1100011
    // offset must be 13-bit signed, multiple of 2
    uint32_t imm = (uint32_t)offset & 0x1FFE;
    uint32_t imm12 = (imm >> 12) & 0x1;
    uint32_t imm11 = (imm >> 11) & 0x1;
    uint32_t imm10_5 = (imm >> 5) & 0x3F;
    uint32_t imm4_1 = (imm >> 1) & 0xF;
    return (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) |
           ((uint32_t)funct3 << 12) | (imm4_1 << 8) | (imm11 << 7) | 0b1100011;
}

static void emit_bne_phys(JitContext* ctx, uint8_t rs1, uint8_t rs2, int16_t offset) {
    // BNE: funct3=001
    emit_instr(ctx, encode_branch_instr(0b001, rs1, rs2, offset));
}

static void emit_bltu_phys(JitContext* ctx, uint8_t rs1, uint8_t rs2, int16_t offset) {
    // BLTU: funct3=110
    emit_instr(ctx, encode_branch_instr(0b110, rs1, rs2, offset));
}

// SLLI: Shift Left Logical Immediate
static void emit_slli_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t shamt) {
    // Use compressed c.slli when: rd == rs1, rd != 0, shamt != 0, shamt < 32
    if (rd == rs1 && rd != 0 && shamt != 0 && shamt < 32) {
        emit_c_slli(ctx, rd, shamt);
        return;
    }
    // SLLI rd, rs1, shamt: opcode=0010011, funct3=001, imm[11:5]=0000000, imm[4:0]=shamt
    uint32_t imm = (shamt & 0x1F);  // 5-bit shift amount for RV32
    emit_instr(ctx, (0b0000000 << 25) | (imm << 20) | (rs1 << 15) | (0b001 << 12) | (rd << 7) | 0b0010011);
}

// SRLI: Shift Right Logical Immediate
static void emit_srli_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t shamt) {
    // Use compressed c.srli when: rd == rs1, rd is x8-x15, shamt != 0, shamt < 32
    uint8_t rd_prime;
    if (rd == rs1 && shamt != 0 && shamt < 32 && reg_is_compressed(rd, &rd_prime)) {
        emit_c_srli(ctx, rd_prime, shamt);
        return;
    }
    // SRLI rd, rs1, shamt: opcode=0010011, funct3=101, imm[11:5]=0000000
    uint32_t imm = (shamt & 0x1F);
    emit_instr(ctx, (0b0000000 << 25) | (imm << 20) | (rs1 << 15) | (0b101 << 12) | (rd << 7) | 0b0010011);
}

// SRAI: Shift Right Arithmetic Immediate
static void emit_srai_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t shamt) {
    // Use compressed c.srai when: rd == rs1, rd is x8-x15, shamt != 0, shamt < 32
    uint8_t rd_prime;
    if (rd == rs1 && shamt != 0 && shamt < 32 && reg_is_compressed(rd, &rd_prime)) {
        emit_c_srai(ctx, rd_prime, shamt);
        return;
    }
    // SRAI rd, rs1, shamt: opcode=0010011, funct3=101, imm[11:5]=0100000
    uint32_t imm = (shamt & 0x1F);
    emit_instr(ctx, (0b0100000 << 25) | (imm << 20) | (rs1 << 15) | (0b101 << 12) | (rd << 7) | 0b0010011);
}

// AND: Bitwise AND
static void emit_and_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    // Use compressed c.and when: rd == rs1, both rd and rs2 are x8-x15
    uint8_t rd_prime, rs2_prime;
    if (rd == rs1 && reg_is_compressed(rd, &rd_prime) && reg_is_compressed(rs2, &rs2_prime)) {
        emit_c_and(ctx, rd_prime, rs2_prime);
        return;
    }
    // AND rd, rs1, rs2: opcode=0110011, funct3=111, funct7=0000000
    emit_instr(ctx, (0b0000000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b111 << 12) | (rd << 7) | 0b0110011);
}

// OR: Bitwise OR
static void emit_or_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    // Use compressed c.or when: rd == rs1, both rd and rs2 are x8-x15
    uint8_t rd_prime, rs2_prime;
    if (rd == rs1 && reg_is_compressed(rd, &rd_prime) && reg_is_compressed(rs2, &rs2_prime)) {
        emit_c_or(ctx, rd_prime, rs2_prime);
        return;
    }
    // OR rd, rs1, rs2: opcode=0110011, funct3=110, funct7=0000000
    emit_instr(ctx, (0b0000000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b110 << 12) | (rd << 7) | 0b0110011);
}

// SLTU: Set Less Than Unsigned
static void emit_sltu_phys(JitContext* ctx, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    // SLTU rd, rs1, rs2: opcode=0110011, funct3=011, funct7=0000000
    emit_instr(ctx, (0b0000000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b011 << 12) | (rd << 7) | 0b0110011);
}

// LHU: Load Halfword Unsigned
static void emit_lhu_phys(JitContext* ctx, uint8_t rd, int16_t offset, uint8_t rs1) {
    // LHU rd, offset(rs1): opcode=0000011, funct3=101
    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
    emit_instr(ctx, imm_bits | (rs1 << 15) | (0b101 << 12) | (rd << 7) | 0b0000011);
}

// AMOADD.W: Atomic Memory Operation - Add Word
static void emit_amoadd_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // AMOADD.W rd, rs2, (rs1): opcode=0101111, funct3=010, funct5=00000, aq=1, rl=1 (SEQ_CST)
    emit_instr(ctx, (0b00000 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// AMOSUB.W: Atomic Memory Operation - Sub Word
static void emit_amosub_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // funct5=00001 for SUB
    emit_instr(ctx, (0b00001 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// AMOAND.W: Atomic Memory Operation - And Word
static void emit_amoand_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // funct5=01100 for AND
    emit_instr(ctx, (0b01100 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// AMOOR.W: Atomic Memory Operation - Or Word
static void emit_amoor_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // funct5=01000 for OR
    emit_instr(ctx, (0b01000 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// AMOXOR.W: Atomic Memory Operation - Xor Word
static void emit_amoxor_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // funct5=00100 for XOR
    emit_instr(ctx, (0b00100 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// AMOSWAP.W: Atomic Memory Operation - Swap Word (Exchange)
static void emit_amoswap_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // funct5=00001 for SWAP
    emit_instr(ctx, (0b00001 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// LR.W: Load Reserved Word
static void emit_lr_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs1) {
    // LR.W rd, (rs1): funct5=00010, aq=1, rl=1 for SEQ_CST
    emit_instr(ctx, (0b00010 << 27) | (1 << 26) | (1 << 25) | (0 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// SC.W: Store Conditional Word
static void emit_sc_w_phys(JitContext* ctx, uint8_t rd, uint8_t rs2, uint8_t rs1) {
    // SC.W rd, rs2, (rs1): funct5=00011, aq=1, rl=1 for SEQ_CST
    emit_instr(ctx, (0b00011 << 27) | (1 << 26) | (1 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0101111);
}

// NOTE: JAL encoding helper is used for BR patching.
static uint32_t encode_jal_instr(uint8_t rd, int32_t offset) {
    // JAL rd, offset: opcode=1101111
    // offset в формате 21-bit signed, кратен 2
    uint32_t imm = (uint32_t)offset & 0x1FFFFE;
    uint32_t imm20 = (imm >> 20) & 0x1;
    uint32_t imm19_12 = (imm >> 12) & 0xFF;
    uint32_t imm11 = (imm >> 11) & 0x1;
    uint32_t imm10_1 = (imm >> 1) & 0x3FF;
    return (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | (imm19_12 << 12) | (rd << 7) | 0b1101111;
}

static void emit_jal_phys(JitContext* ctx, uint8_t rd, int32_t offset) {
    emit_instr(ctx, encode_jal_instr(rd, offset));
}

// Функции для работы с метками и patchpoints
static void jit_context_init(JitContext* ctx, uint8_t* buffer, size_t capacity) {
    ctx->buffer = buffer;
    ctx->capacity = capacity;
    ctx->offset = 0;
    ctx->patchpoints = NULL;
    ctx->num_patchpoints = 0;
    ctx->patchpoints_capacity = 0;
    ctx->labels = NULL;
    ctx->num_labels = 0;
    ctx->labels_capacity = 0;

#ifdef JIT_STATS
    ctx->helper_call_count = 0;
    ctx->helper_call_fallback_abs_count = 0;
#endif

    ctx->last_cmp_result_reg = 0xFF;
    ctx->last_cmp_in_t0 = false;
}

static void jit_context_add_label(JitContext* ctx, size_t bytecode_offset) {
    // Проверяем, есть ли уже метка для этого offset
    for (size_t i = 0; i < ctx->num_labels; i++) {
        if (ctx->labels[i].bytecode_offset == bytecode_offset) {
            ctx->labels[i].native_offset = ctx->offset;
            return;
        }
    }
    
    // Добавляем новую метку
    if (ctx->num_labels >= ctx->labels_capacity) {
        size_t new_capacity = ctx->labels_capacity == 0 ? 16 : ctx->labels_capacity * 2;
        JitLabel* new_labels = (JitLabel*)realloc(ctx->labels, new_capacity * sizeof(JitLabel));
        if (!new_labels) return;
        ctx->labels = new_labels;
        ctx->labels_capacity = new_capacity;
    }
    
    ctx->labels[ctx->num_labels].bytecode_offset = bytecode_offset;
    ctx->labels[ctx->num_labels].native_offset = ctx->offset;
    ctx->num_labels++;
}

static size_t jit_context_find_label(JitContext* ctx, size_t bytecode_offset) {
    for (size_t i = 0; i < ctx->num_labels; i++) {
        if (ctx->labels[i].bytecode_offset == bytecode_offset) {
            return ctx->labels[i].native_offset;
        }
    }
    return SIZE_MAX; // Не найдено
}

static void jit_context_add_patchpoint(JitContext* ctx, size_t patch_location,
                                       size_t source_bytecode_offset, 
                                       size_t target_bytecode_offset, bool is_conditional, 
                                       uint8_t condition_reg) {
    if (ctx->num_patchpoints >= ctx->patchpoints_capacity) {
        size_t new_capacity = ctx->patchpoints_capacity == 0 ? 16 : ctx->patchpoints_capacity * 2;
        JitPatchpoint* new_patchpoints = (JitPatchpoint*)realloc(ctx->patchpoints, 
                                                                 new_capacity * sizeof(JitPatchpoint));
        if (!new_patchpoints) return;
        ctx->patchpoints = new_patchpoints;
        ctx->patchpoints_capacity = new_capacity;
    }
    
    ctx->patchpoints[ctx->num_patchpoints].patch_location = patch_location;
    ctx->patchpoints[ctx->num_patchpoints].source_bytecode_offset = source_bytecode_offset;
    ctx->patchpoints[ctx->num_patchpoints].target_bytecode_offset = target_bytecode_offset;
    ctx->patchpoints[ctx->num_patchpoints].is_conditional = is_conditional;
    ctx->patchpoints[ctx->num_patchpoints].condition_reg = condition_reg;
    ctx->num_patchpoints++;
}

/* removed: helper stub table experiment (kept auipc+jalr path) */
#if 0
static void jit_context_emit_helper_stubs_and_patch(JitContext* ctx) {
    // Emit stubs at the end of generated function code, then patch all JAL callsites.
    // Stub layout (RV32) - must preserve caller's ra across helper call.
    // We cannot keep it in t-regs because helper call clobbers caller-saved regs.
    // Use stack (keep 16-byte alignment):
    //   addi  sp, sp, -16
    //   sw    ra, 12(sp)
    //   auipc t0, hi20(target - pc)
    //   jalr  ra, t0, lo12
    //   lw    ra, 12(sp)
    //   addi  sp, sp, 16
    //   ret (jalr x0, ra, 0)

    // 1) Emit stubs
    for (size_t i = 0; i < ctx->num_helper_stubs; i++) {
        ctx->helper_stubs[i].stub_native_offset = ctx->offset;

        uintptr_t stub_pc = (uintptr_t)(ctx->buffer + ctx->offset);
        int64_t rel = (int64_t)ctx->helper_stubs[i].func_addr - (int64_t)stub_pc;

        // Preserve caller ra on stack, keep 16-byte alignment
        emit_addi_phys(ctx, 2, 2, -16);
        emit_sw_phys(ctx, 1, 12, 2);

        // Prefer PC-relative (auipc+jalr). If out-of-range, fall back to absolute (lui+addi+jalr).
        if (rel > (int64_t)INT32_MAX || rel < (int64_t)INT32_MIN) {
            uint32_t hi20 = (uint32_t)((ctx->helper_stubs[i].func_addr + 0x800) >> 12);
            uint32_t lo12 = (uint32_t)(ctx->helper_stubs[i].func_addr & 0xFFF);
            emit_instr(ctx, (hi20 << 12) | (5 << 7) | 0x37); // lui t0
            emit_instr(ctx, (lo12 << 20) | (5 << 15) | (0x0 << 12) | (5 << 7) | 0x13); // addi t0,t0,lo12
            emit_jalr_phys(ctx, 1, 5, 0);
        } else {
            int64_t hi20 = (rel + 0x800) >> 12;
            int64_t lo12 = rel - (hi20 << 12);
            uint32_t auipc_imm = ((uint32_t)hi20 & 0xFFFFF) << 12;
            emit_instr(ctx, auipc_imm | (5u << 7) | 0b0010111); // auipc t0, hi20
            emit_jalr_phys(ctx, 1, 5, (int16_t)lo12);
        }

        // Restore caller ra and stack
        emit_lw_phys(ctx, 1, 12, 2);
        emit_addi_phys(ctx, 2, 2, 16);

        // ret
        emit_jalr_phys(ctx, 0, 1, 0);
    }

    // 2) Patch callsites
    for (size_t c = 0; c < ctx->num_helper_call_sites; c++) {
        uintptr_t target = ctx->helper_call_sites[c].func_addr;
        size_t target_stub_off = SIZE_MAX;
        for (size_t i = 0; i < ctx->num_helper_stubs; i++) {
            if (ctx->helper_stubs[i].func_addr == target) {
                target_stub_off = ctx->helper_stubs[i].stub_native_offset;
                break;
            }
        }
        if (target_stub_off == SIZE_MAX) continue;

        size_t call_loc = ctx->helper_call_sites[c].patch_location;
        int32_t jal_off = (int32_t)(target_stub_off - call_loc);

        // JAL range is ±1 MiB (21-bit signed, *2). If too far, keep old placeholder (will crash).
        // Given we emit stubs at end, should fit for typical functions.
        if (jal_off < -1048576 || jal_off >= 1048576) {
            printf("[jit] ERROR: helper stub JAL out of range (off=%ld)\n", (long)jal_off);
            continue;
        }

        uint32_t new_instr = encode_jal_instr(1, jal_off);
        memcpy(ctx->buffer + call_loc, &new_instr, sizeof(new_instr));
    }
}
#endif

static void jit_context_patch_branches(JitContext* ctx, const uint8_t* bytecode, size_t bytecode_size) {
    // Проходим по всем patchpoints и фиксируем переходы
    for (size_t i = 0; i < ctx->num_patchpoints; i++) {
        JitPatchpoint* pp = &ctx->patchpoints[i];
        
        // target_bytecode_offset уже содержит вычисленный целевой offset в байт-коде
        size_t target_bytecode = pp->target_bytecode_offset;
        if (target_bytecode >= bytecode_size) {
            printf("[jit] ERROR: Branch target out of bounds (target=%zu, size=%zu) at native offset %zu\n", 
                   target_bytecode, bytecode_size, pp->patch_location);
            // Patch with EBREAK to crash instead of infinite loop
            uint32_t ebreak_instr = 0x00100073; 
            memcpy(ctx->buffer + pp->patch_location, &ebreak_instr, sizeof(ebreak_instr));
            continue;
        }
        
        // Находим нативную позицию цели
        size_t target_native = jit_context_find_label(ctx, target_bytecode);
        if (target_native == SIZE_MAX) {
            printf("[jit] ERROR: Label not found for bytecode offset %zu (source=%zu) at native offset %zu\n", 
                   target_bytecode, pp->source_bytecode_offset, pp->patch_location);
            // DEBUG: Dump all existing labels to find what's missing
            printf("[jit] DEBUG: Total labels: %zu, bytecode_size: %zu\n", ctx->num_labels, bytecode_size);
            printf("[jit] DEBUG: Existing labels around target:\n");
            for (size_t j = 0; j < ctx->num_labels; j++) {
                if (ctx->labels[j].bytecode_offset >= target_bytecode - 20 && 
                    ctx->labels[j].bytecode_offset <= target_bytecode + 20) {
                    printf("[jit] DEBUG:   label[%zu]: bytecode_offset=%zu, native_offset=%zu%s\n",
                           j, ctx->labels[j].bytecode_offset, ctx->labels[j].native_offset,
                           ctx->labels[j].bytecode_offset == target_bytecode ? " <-- TARGET" : "");
                }
            }
            // Also show what opcode is at target position in bytecode
            if (target_bytecode < bytecode_size) {
                printf("[jit] DEBUG: Opcode at target offset %zu: 0x%02X\n", 
                       target_bytecode, bytecode[target_bytecode]);
            }
            // Patch with EBREAK to crash instead of infinite loop
            uint32_t ebreak_instr = 0x00100073;
            memcpy(ctx->buffer + pp->patch_location, &ebreak_instr, sizeof(ebreak_instr));
            continue;
        }
        
        // Вычисляем смещение от текущей позиции (patch_location указывает на инструкцию перехода)
        int32_t offset = (int32_t)(target_native - pp->patch_location);
        
        if (pp->is_conditional) {
            // BR_IF: нужно пропатчить BNE инструкцию
            // Проверяем, что offset помещается в 13 бит
            if (offset >= -4096 && offset < 4096) {
                // Пересчитываем BNE с правильным offset
                uint32_t imm = (uint32_t)offset & 0x1FFE;
                uint32_t imm12 = (imm >> 12) & 0x1;
                uint32_t imm11 = (imm >> 11) & 0x1;
                uint32_t imm10_5 = (imm >> 5) & 0x3F;
                uint32_t imm4_1 = (imm >> 1) & 0xF;
                
                // Читаем текущую инструкцию для получения rs1 и rs2
                uint32_t old_instr;
                memcpy(&old_instr, ctx->buffer + pp->patch_location, sizeof(old_instr));
                uint8_t rs1 = (old_instr >> 15) & 0x1F;
                uint8_t rs2 = (old_instr >> 20) & 0x1F;
                
                uint32_t new_instr = (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | 
                                    (rs1 << 15) | (0b001 << 12) | (imm4_1 << 8) | 
                                    (imm11 << 7) | 0b1100011;
                memcpy(ctx->buffer + pp->patch_location, &new_instr, sizeof(new_instr));
            } else {
                printf("[jit] ERROR: Conditional branch offset too large: %ld at native offset %zu\n", (long)offset, pp->patch_location);
                // Patch with EBREAK
                uint32_t ebreak_instr = 0x00100073;
                memcpy(ctx->buffer + pp->patch_location, &ebreak_instr, sizeof(ebreak_instr));
            }
        } else {
            // BR: нужно пропатчить JAL инструкцию
            if (offset >= -1048576 && offset < 1048576) {
                // Пересчитываем JAL с правильным offset
                uint32_t imm = (uint32_t)offset & 0x1FFFFE;
                uint32_t imm20 = (imm >> 20) & 0x1;
                uint32_t imm19_12 = (imm >> 12) & 0xFF;
                uint32_t imm11 = (imm >> 11) & 0x1;
                uint32_t imm10_1 = (imm >> 1) & 0x3FF;
                
                // Читаем текущую инструкцию для получения rd
                uint32_t old_instr;
                memcpy(&old_instr, ctx->buffer + pp->patch_location, sizeof(old_instr));
                uint8_t rd = (old_instr >> 7) & 0x1F;
                
                uint32_t new_instr = (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | 
                                    (imm19_12 << 12) | (rd << 7) | 0b1101111;
                memcpy(ctx->buffer + pp->patch_location, &new_instr, sizeof(new_instr));
            } else {
                printf("[jit] ERROR: Unconditional branch offset too large: %ld at native offset %zu\n", (long)offset, pp->patch_location);
                // Patch with EBREAK
                uint32_t ebreak_instr = 0x00100073;
                memcpy(ctx->buffer + pp->patch_location, &ebreak_instr, sizeof(ebreak_instr));
            }
        }
    }
}

static void jit_context_free(JitContext* ctx) {
    if (ctx->patchpoints) {
        free(ctx->patchpoints);
        ctx->patchpoints = NULL;
    }
    if (ctx->labels) {
        free(ctx->labels);
        ctx->labels = NULL;
    }
}

EspbResult espb_jit_compile_function(EspbInstance* instance, uint32_t func_idx, const EspbFunctionBody *body, void **out_code, size_t *out_size) {
    if (!instance || !body || !out_code || !out_size) {
        return ESPB_ERR_INVALID_OPERAND;
    }

    if (body->is_jit_compiled) {
        *out_code = body->jit_code;
        *out_size = body->jit_code_size;
        return ESPB_OK;
    }

    const uint8_t* bytecode = body->code;
    const uint8_t* end = bytecode + body->code_size;
    
    // Reduce to 20x and cap at 32KB to avoid heap exhaustion on large functions
    size_t jit_buffer_size = body->code_size * 20;
    const size_t MAX_JIT_BUFFER = 32 * 1024;
    if (jit_buffer_size > MAX_JIT_BUFFER) jit_buffer_size = MAX_JIT_BUFFER;
    if (jit_buffer_size == 0) {
        *out_code = NULL;
        *out_size = 0;
        return ESPB_OK;
    }

    uint8_t* exec_buffer = (uint8_t*)espb_exec_alloc(jit_buffer_size);
    if (!exec_buffer) {
        printf("JIT ERROR: Failed to allocate %zu bytes of executable memory\n", jit_buffer_size);
        return ESPB_ERR_MEMORY_ALLOC;
    }
    
    // Проверка что память executable (используем ESP-IDF API)
    if (!esp_ptr_executable(exec_buffer) || esp_ptr_in_dram(exec_buffer)) {
        printf("JIT: Failed to allocate executable memory (got %p)\n", exec_buffer);
        heap_caps_free(exec_buffer);
        return ESPB_ERR_MEMORY_ALLOC;
    }
    
    // Проверяем выравнивание (RISC-V требует 4-byte alignment)
    if (((uintptr_t)exec_buffer & 0x3) != 0) {
        printf("JIT WARNING: exec_buffer not 4-byte aligned: %p\n", exec_buffer);
    }

    JitContext ctx;
    jit_context_init(&ctx, exec_buffer, jit_buffer_size);
    
    // CMP+BR_IF: Инициализация трекера
    ctx.last_cmp_result_reg = 0xFF;  // Нет последнего CMP
    ctx.last_cmp_in_t0 = false;

    // ✅ Извлекаем JIT метаданные из заголовка
    const EspbFuncHeader *header = &body->header;
    uint8_t flags = header->flags;
    uint8_t max_reg_used = header->max_reg_used;
    uint16_t frame_size = header->frame_size;
    uint16_t num_virtual_regs = header->num_virtual_regs;
    
    
    // ✅ Вычисляем размер стек фрейма с учетом spill area и saved registers
    // Базовый размер: ra(4) + fp(4) + s1(4) + s2(4) + s9(4) + frame_size (spill area)
    bool is_leaf = (flags & ESPB_FUNC_FLAG_IS_LEAF) != 0;
    bool no_spill = (flags & ESPB_FUNC_FLAG_NO_SPILL) != 0;  // 🔥 регистры без spill

    // NO_SPILL fast-path eligibility: only for leaf + i32-only + small vreg count
    bool i32_only = true;
    if (no_spill) {
        // быстрый скан байткода на наличие 64-bit/F64 опкодов
        const uint8_t* scan = body->code;
        const uint8_t* scan_end = body->code + body->code_size;
        while (scan < scan_end) {
            uint8_t op = *scan++;
            switch (op) {
                case 0x85: // LOAD.I64
                case 0x76: // STORE.I64
                case 0x19: // LDC.I64.IMM
                case 0x1B: // LDC.F64.IMM
                case 0x30: // ADD.I64
                case 0x31: // SUB.I64
                case 0x32: // MUL.I64
                case 0x33: // DIVS.I64
                case 0x34: // REMS.I64
                case 0x36: // DIVU.I64
                case 0x37: // REMU.I64
                case 0x38: // AND.I64
                case 0x39: // OR.I64
                case 0x3A: // XOR.I64
                case 0x3B: // SHL.I64
                case 0x3C: // SHR.I64
                case 0x3D: // USHR.I64
                case 0x3E: // NOT.I64
                case 0x68: // ADD.F64
                case 0x69: // SUB.F64
                case 0x6A: // MUL.F64
                case 0x6B: // DIV.F64
                case 0xAF: // CVT.U32.F64
                case 0xB1: // CVT.U64.F64
                case 0x9B: // ZEXT.I32.I64
                case 0xA0: // SEXT.I32.I64
                case 0xA1:
                case 0x90: // TRUNC.I64.I32
                    i32_only = false;
                    scan = scan_end;
                    break;
                default:
                    // skip immediate payloads (minimal, only handle known immediate-length ops)
                    if (op == 0x18) { // LDC.I32.IMM: op, rd, imm32
                        scan += 1 + 4;
                    } else if (op == 0x02) { // BR: op, off16
                        scan += 2;
                    } else if (op == 0x03) { // BR_IF: op, r, off16
                        scan += 1 + 2;
                    } else if (op == 0x0A) { // CALL: op, u16
                        scan += 2;
                    } else if (op == 0x0F) { // END
                        // no payload
                    } else {
                        // fallback: most ops are 3-reg (rd,rs1,rs2)
                        scan += 3;
                    }
                    break;
            }
        }
    }

    // КРИТИЧНО: Проверяем max_reg_used, а не num_virtual_regs!
    // После компакции max_reg_used показывает реально используемые регистры (например, R8-R11),
    // а num_virtual_regs включает резерв для временных R16-R23
    // 
    // Fast Path для I64: работает с парами регистров (lo/hi)
    // Ограничение: max_reg_used <= 7 для I64 (8 vregs × 2 = 16 физических регистров)
    //              max_reg_used <= 15 для I32 (16 vregs = 16 физических регистров)
    // ВРЕМЕННО ОТКЛЮЧЕНО: NO_SPILL fast-path вызывает crash на RISC-V
    // TODO: исправить проблему с регистрами при vregs > физических регистров
    bool no_spill_fastpath = false;  // DISABLED - causes Store access fault on RISC-V
    (void)i32_only; // suppress unused warning when NO_SPILL disabled
#if 0  // TEMPORARILY DISABLED
    if (i32_only) {
        no_spill_fastpath = (no_spill && is_leaf && max_reg_used <= 15);
        if (no_spill_fastpath) {
            printf("[JIT-OPT] NO_SPILL fast-path enabled (leaf+i32-only, max_reg=%u, vregs=%u)\n", max_reg_used, num_virtual_regs);
        }
    } else {
        // Для I64/F64: ограничение в 2 раза строже (нужны пары регистров)
        // Используем до 20 физических регистров (a0-a7, s2-s11) = 10 виртуальных I64 регистров
        // ОГРАНИЧЕНИЕ: max_reg_used <= 9 (безопасный диапазон callee-saved регистров)
        no_spill_fastpath = (no_spill && is_leaf && max_reg_used <= 9);
        if (no_spill_fastpath) {
            printf("[JIT-OPT] NO_SPILL fast-path enabled (leaf+mixed-types, max_reg=%u, vregs=%u) [I64 mode]\n", max_reg_used, num_virtual_regs);
        }
    }
#endif

    // Fast-path mapping helper:
    // v0..v7  -> a0..a7 (x10..x17)
    // v8..v15 -> s3..s10 (x19..x26)
    // returns 0 if out of range
    // (C file: must be a macro-style helper)
    #define FP_MAP_VREG(vreg) (((vreg) <= 7) ? (uint8_t)(10 + (vreg)) : (((vreg) >= 8 && (vreg) <= 15) ? (uint8_t)(19 + ((vreg) - 8)) : (uint8_t)0))
    
    // Маппинг для I64 Fast Path: виртуальный регистр → пара физических (lo, hi)
    // v0-v4 → {a0,a1}, {a2,a3}, {a4,a5}, {a6,a7}, {s2,s3}  (phys 10-19)
    // v5-v9 → {s4,s5}, {s6,s7}, {s8,s9}, {s10,s11}, {t3,t4} (phys 20-29)
    #define FP_I64_LO(v) ((v) < 5 ? (10 + (v) * 2) : (20 + ((v) - 5) * 2))
    #define FP_I64_HI(v) ((v) < 5 ? (10 + (v) * 2 + 1) : (20 + ((v) - 5) * 2 + 1))
    
    // КРИТИЧНО: нужно выделить достаточно места для:
    // 1. Сохраненных callee-saved регистров: s0, s1, s2 = 12 байт
    // 2. ra если не leaf = 4 байта
    // 3. frame_size для spills
    // 4. Временное пространство для CALL_IMPORT (до 60 байт!)
    bool stable_cache_enabled = !no_spill_fastpath;

    uint16_t saved_regs_size = 12; // s0, s1, s2
    if (stable_cache_enabled) {
        saved_regs_size += 16; // s4..s7 (stable cache regs, 2-entry cache)
    }
    if (!is_leaf) {
        saved_regs_size += 4; // +4 для ra
    }

    // NO_SPILL fast-path: save s3..s10 (8 regs * 4)
    if (no_spill_fastpath) {
        saved_regs_size += 32;
    }
    
    // Для функций с вызовами нужно больше места (для сохранения caller-saved регистров)
    uint16_t temp_space = (flags & ESPB_FUNC_FLAG_HAS_CALLS) ? 64 : 0;
    
    uint16_t total_frame_size = saved_regs_size + frame_size + temp_space;
    
    // Выравниваем до 16 байт (RISC-V ABI требует)
    total_frame_size = (total_frame_size + 15) & ~15;
    

    // --- PROLOGUE ---
    // Сохраняем стек фрейм
    emit_addi_phys(&ctx, 2, 2, -(int16_t)total_frame_size); // sp -= frame_size

    int offset = total_frame_size;

    // NO_SPILL fast-path: сохраняем s3..s10 (callee-saved)
    // Загрузка vregs в физические регистры будет ПОЗЖЕ, после того как s2 (v_regs ptr) установлен.
    if (no_spill_fastpath) {
        for (uint8_t r = 19; r <= 26; r++) {
            offset -= 4;
            emit_sw_phys(&ctx, r, offset, 2);
        }
    }
    
    // Сохраняем ra только если функция НЕ leaf
    if (!is_leaf) {
        offset -= 4;
        emit_sw_phys(&ctx, 1, offset, 2); // sw ra, offset(sp)
    }
    
    // Сохраняем fp (s0)
    offset -= 4;
    emit_sw_phys(&ctx, 8, offset, 2); // sw s0, offset(sp)
    
    // Устанавливаем fp = sp
    emit_addi_phys(&ctx, 8, 2, 0); // mv s0, sp
    
    // Сохраняем указатель на instance (a0)
    // s1 = instance, s2 = v_regs
    offset -= 4;
    emit_sw_phys(&ctx, 9, offset, 2);  // sw s1, offset(sp)
    
    
    // Сохраняем s2 (обязательно по ABI!)
    offset -= 4;
    emit_sw_phys(&ctx, 18, offset, 2); // sw s2, offset(sp)

    if (stable_cache_enabled) {
        // Save stable cache regs s4..s7 (x20..x23)
        offset -= 4;
        emit_sw_phys(&ctx, 20, offset, 2);
        offset -= 4;
        emit_sw_phys(&ctx, 21, offset, 2);
        offset -= 4;
        emit_sw_phys(&ctx, 22, offset, 2);
        offset -= 4;
        emit_sw_phys(&ctx, 23, offset, 2);
    }
    
    emit_addi_phys(&ctx, 9, 10, 0);  // s1 = a0 (instance)
    emit_addi_phys(&ctx, 18, 11, 0); // s2 = a1 (v_regs)

    // NO_SPILL fast-path: загружаем v0..max_reg_used из v_regs[] один раз в физические регистры
    // КРИТИЧНО: После компакции используемые регистры могут быть в диапазоне R8-R15,
    // поэтому загружаем все регистры от 0 до max_reg_used (включительно)
    if (no_spill_fastpath) {
        if (i32_only) {
            // I32: один физический регистр на виртуальный
            for (uint8_t v = 0; v <= max_reg_used; v++) {
                uint8_t phys = FP_MAP_VREG(v);
                if (phys != 0) {
                    emit_lw_phys(&ctx, phys, v * 8, 18);  // Загружаем младшие 32 бита
                }
            }
        } else {
            // I64: пара физических регистров на виртуальный
            // Маппинг: v[i] → {phys[i*2], phys[i*2+1]}
            for (uint8_t v = 0; v <= max_reg_used && v <= 7; v++) {
                uint8_t phys_lo, phys_hi;
                
                if (v < 4) {
                    phys_lo = 10 + v * 2;      // a0, a2, a4, a6
                    phys_hi = 10 + v * 2 + 1;  // a1, a3, a5, a7
                } else {
                    phys_lo = 19 + (v - 4) * 2;      // s3, s5, s7, s9
                    phys_hi = 19 + (v - 4) * 2 + 1;  // s4, s6, s8, s10
                }
                
                // Загружаем пару регистров (lo из offset+0, hi из offset+4)
                emit_lw_phys(&ctx, phys_lo, v * 8 + 0, 18);  // Младшие 32 бита
                emit_lw_phys(&ctx, phys_hi, v * 8 + 4, 18);  // Старшие 32 бита
            }
        }
    }

    const uint8_t* pc = bytecode;
    const uint8_t* bytecode_start = bytecode;
    
    // NOTE: we do not stop at the first END; the bytecode can contain multiple blocks/returns.
    // We must scan the whole function so that all branch targets get labels for patching.
    bool encountered_end = false;
    
    // ОТЛАДКА: Выводим информацию о байткоде
            for (size_t i = (body->code_size > 16 ? body->code_size - 16 : 0); i < body->code_size; i++) {
    }
    printf("\n");
    
    // Peephole cache: живёт в пределах линейного участка
    PeepholeRegCache ph;
    ph_reset(&ph);

    // Phase 3: stable typed cache across helper calls (2-entry for better hit-rate).
    // slot0: s4/s5 (x20/x21), slot1: s6/s7 (x22/x23)
    typedef enum { VC_NONE = 0, VC_F32 = 1, VC_F64 = 2 } VCacheKind;
    typedef struct { VCacheKind kind; bool dirty; uint8_t vreg; } VCacheSlot;
    VCacheSlot vcache0 = {0};
    VCacheSlot vcache1 = {0};
    uint8_t vcache_mru = 0; // 0 or 1

    // Helper macros for 2-entry cache decisions (compile-time, not runtime)
    #define VC0_LO 20
    #define VC0_HI 21
    #define VC1_LO 22
    #define VC1_HI 23

    #define VCACHE_MATCH_F32(slot, reg) (stable_cache_enabled && (slot).kind == VC_F32 && (slot).vreg == (reg))
    #define VCACHE_MATCH_F64(slot, reg) (stable_cache_enabled && (slot).kind == VC_F64 && (slot).vreg == (reg))

    // Spill a slot to v_regs if dirty
    #define VCACHE_SPILL(slot, reg_lo, reg_hi) do { \
        if (stable_cache_enabled && (slot).kind != VC_NONE && (slot).dirty) { \
            if ((slot).kind == VC_F32) { \
                emit_sw_phys(&ctx, (reg_lo), (slot).vreg * 8, 18); \
            } else if ((slot).kind == VC_F64) { \
                emit_sw_phys(&ctx, (reg_lo), (slot).vreg * 8, 18); \
                emit_sw_phys(&ctx, (reg_hi), (slot).vreg * 8 + 4, 18); \
            } \
        } \
        (slot).kind = VC_NONE; \
        (slot).dirty = false; \
    } while(0)

    // Select slot for writing F64 result (prefer existing rd, then empty, else evict LRU)
    #define VCACHE_SELECT_SLOT_FOR_F64(rd_reg) \
        (VCACHE_MATCH_F64(vcache0, (rd_reg)) ? 0 : \
         VCACHE_MATCH_F64(vcache1, (rd_reg)) ? 1 : \
         (vcache0.kind == VC_NONE) ? 0 : \
         (vcache1.kind == VC_NONE) ? 1 : \
         (vcache_mru == 0 ? 1 : 0))

    // Load F64 vreg into a pair of regs (dst_lo/dst_hi)
    #define VCACHE_LOAD_F64(vreg, dst_lo, dst_hi) do { \
        if (VCACHE_MATCH_F64(vcache0, (vreg))) { \
            emit_addi_phys(&ctx, (dst_lo), VC0_LO, 0); \
            emit_addi_phys(&ctx, (dst_hi), VC0_HI, 0); \
            vcache_mru = 0; \
        } else if (VCACHE_MATCH_F64(vcache1, (vreg))) { \
            emit_addi_phys(&ctx, (dst_lo), VC1_LO, 0); \
            emit_addi_phys(&ctx, (dst_hi), VC1_HI, 0); \
            vcache_mru = 1; \
        } else { \
            emit_lw_phys(&ctx, (dst_lo), (vreg) * 8, 18); \
            emit_lw_phys(&ctx, (dst_hi), (vreg) * 8 + 4, 18); \
        } \
    } while(0)

    // Store F64 result currently in a0/a1 into cache slots.
    #define VCACHE_STORE_F64_RESULT(rd_vreg) do { \
        if (!stable_cache_enabled) { \
            emit_sw_phys(&ctx, 10, (rd_vreg) * 8, 18); \
            emit_sw_phys(&ctx, 11, (rd_vreg) * 8 + 4, 18); \
        } else { \
            uint8_t _slot = (uint8_t)VCACHE_SELECT_SLOT_FOR_F64((rd_vreg)); \
            if (_slot == 0) { \
                if (vcache0.kind != VC_NONE && vcache0.vreg != (rd_vreg) && vcache0.dirty) { \
                    VCACHE_SPILL(vcache0, VC0_LO, VC0_HI); \
                } \
                emit_addi_phys(&ctx, VC0_LO, 10, 0); \
                emit_addi_phys(&ctx, VC0_HI, 11, 0); \
                vcache0.kind = VC_F64; \
                vcache0.dirty = true; \
                vcache0.vreg = (rd_vreg); \
                vcache_mru = 0; \
            } else { \
                if (vcache1.kind != VC_NONE && vcache1.vreg != (rd_vreg) && vcache1.dirty) { \
                    VCACHE_SPILL(vcache1, VC1_LO, VC1_HI); \
                } \
                emit_addi_phys(&ctx, VC1_LO, 10, 0); \
                emit_addi_phys(&ctx, VC1_HI, 11, 0); \
                vcache1.kind = VC_F64; \
                vcache1.dirty = true; \
                vcache1.vreg = (rd_vreg); \
                vcache_mru = 1; \
            } \
        } \
    } while(0)

    // Temporary alias: existing F32 cache code still uses `vcache` (single slot).
    // We keep it mapped to slot0 while migrating F32 to 2-entry.
    #define vcache vcache0

    // Flush both cache slots to v_regs
    #define VCACHE_FLUSH_SLOT(slot, reg_lo, reg_hi) do { \
        if (stable_cache_enabled && (slot).kind != VC_NONE && (slot).dirty) { \
            if ((slot).kind == VC_F32) { \
                emit_sw_phys(&ctx, (reg_lo), (slot).vreg * 8, 18); \
            } else if ((slot).kind == VC_F64) { \
                emit_sw_phys(&ctx, (reg_lo), (slot).vreg * 8, 18); \
                emit_sw_phys(&ctx, (reg_hi), (slot).vreg * 8 + 4, 18); \
            } \
        } \
        (slot).kind = VC_NONE; \
        (slot).dirty = false; \
    } while(0)

    #define VCACHE_FLUSH_ALL() do { \
        VCACHE_FLUSH_SLOT(vcache0, 20, 21); \
        VCACHE_FLUSH_SLOT(vcache1, 22, 23); \
    } while(0)

    // Генерируем код и создаем метки на лету
    while (pc < end) {
        size_t bytecode_offset = pc - bytecode_start;
        // Создаем/обновляем метку для текущей позиции в байт-коде
        jit_context_add_label(&ctx, bytecode_offset);
        
        uint8_t opcode = *pc++;

        // Сохраняем кеш только для группы I32 ALU/IMM8 опов.
        // Любой другой опкод консервативно сбрасывает кеш.
        bool peephole_alu = ((opcode >= 0x20 && opcode <= 0x2D) ||
                             (opcode == 0x40) || (opcode == 0x41) ||
                             (opcode >= 0x44 && opcode <= 0x4B) ||
                             (opcode == 0x30) || (opcode == 0x31));

        // BR/BR_IF обрабатываем отдельно (нужен selective flush)
        bool is_branch = (opcode == 0x02 || opcode == 0x03);

        // Если сейчас в x5/x6 лежит i64 (dirty/valid), то перед любым i32-ALU/IMM8
        // надо сделать flush+reset, иначе i32 опкод перетрёт x5/x6 и мы потеряем i64.
        if (ph.i64_valid) {
            bool is_i32_peephole = ((opcode >= 0x20 && opcode <= 0x2D) ||
                                   (opcode == 0x40) || (opcode == 0x41) ||
                                   (opcode >= 0x44 && opcode <= 0x4B));
            if (is_i32_peephole) {
                peephole_alu = false;
            }
        }
        if (!peephole_alu && !is_branch) {
            // Barrier: любой не-ALU опкод может читать v_regs[] напрямую или быть целью ветвления.
            // Поэтому принудительно синхронизируем dirty значения с памятью.
            ph_flush(&ctx, &ph);
            ph_reset(&ph);

            // Flush stable cache
            VCACHE_FLUSH_ALL();
        }
        
        // if (bytecode_offset > 350) {
        //    printf("[jit-trace] offset: %zu opcode: 0x%02X\n", bytecode_offset, opcode);
        // }
        
        // Отладка опкодов отключена (можно включить для диагностики)
        
        switch (opcode) {
            case 0x00: { // NOP - No operation (padding)
                // Ничего не делаем, просто продолжаем
                break;
            }
            
            case 0x01: { // NOP - Alternative NOP instruction
                // Генерируем RISC-V NOP (ADDI x0, x0, 0)
                // NOP в RISC-V кодируется как: 0x00000013
                emit_instr(&ctx, 0x00000013);
                break;
            }
            
            case 0x05: { // UNREACHABLE - Trap instruction
                // Генерируем RISC-V EBREAK (вызывает trap/breakpoint exception)
                // EBREAK кодируется как: 0x00100073
                emit_instr(&ctx, 0x00100073);
                break;
            }
            
            case 0x04: { // BR_TABLE Ridx(u8), num_targets(u16), [target_offsets(i16)...], default_offset(i16)
                ctx.last_cmp_result_reg = 0xFF;
                
                uint8_t ridx = *pc++;
                uint16_t num_targets;
                memcpy(&num_targets, pc, sizeof(num_targets)); pc += sizeof(num_targets);
                
                // Сохраняем позицию таблицы смещений в байт-коде
                const uint8_t* table_start = pc;
                pc += num_targets * sizeof(int16_t); // Пропускаем таблицу
                
                // Читаем default offset
                int16_t default_offset;
                memcpy(&default_offset, pc, sizeof(default_offset)); pc += sizeof(default_offset);
                
                // ВАЖНО: В интерпретаторе offset применяется от PC ПОСЛЕ инструкции BR_TABLE.
                // pc теперь указывает на следующую инструкцию после BR_TABLE.
                // Все offset'ы в таблице относительны к этой позиции.
                size_t source_bytecode_offset = pc - bytecode_start;
                
                // Flush cache перед переходом
                ph_flush(&ctx, &ph);
                ph_reset(&ph);
                
                // Загружаем индекс из v_regs[ridx]
                emit_lw_phys(&ctx, 5, ridx * 8, 18);  // t0 = v_regs[ridx] (index)
                
                // Проверка: если index >= num_targets, используем default
                // Загружаем num_targets в t1
                if (num_targets < 2048) {
                    emit_addi_phys(&ctx, 6, 0, (int16_t)num_targets);
                } else {
                    uint32_t hi = ((num_targets + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_targets - hi);
                    emit_lui_phys(&ctx, 6, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 6, 6, lo);
                }
                
                // НЕ делаем проверку BGEU - просто пропустим, если индекс вне диапазона
                // Проверка будет неявной: если ни один BEQ не сработает, перейдем на default
                
                // Вычисляем адрес в таблице: table_start + index * 2
                // Используем таблицу переходов в JIT коде - сложный вариант
                // Упрощенный вариант: генерируем цепочку сравнений (для малого num_targets)
                // Или используем computed goto через таблицу адресов
                
                // ОПТИМИЗАЦИЯ: Для небольших таблиц (< 8 элементов) генерируем цепочку if-else
                // Для больших таблиц - используем jump table
                
                if (num_targets <= 8) {
                    // Простая цепочка сравнений для малых таблиц
                    for (uint16_t i = 0; i < num_targets; i++) {
                        int16_t target_offset;
                        memcpy(&target_offset, table_start + i * sizeof(int16_t), sizeof(int16_t));
                        size_t target_bytecode_offset = source_bytecode_offset + target_offset;
                        
                        // Сравниваем index с i
                        if (i < 2047) {
                            emit_addi_phys(&ctx, 6, 0, (int16_t)i);
                        } else {
                            uint32_t hi = ((i + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(i - hi);
                            emit_lui_phys(&ctx, 6, hi);
                            if (lo != 0) emit_addi_phys(&ctx, 6, 6, lo);
                        }
                        
                        // Если index != i, пропускаем (переходим на следующую проверку)
                        size_t skip_patch_location = ctx.offset;
                        emit_bne_phys(&ctx, 5, 6, 8); // BNE t0, t1, +8 (пропустить следующий JAL)
                        
                        // Если index == i, делаем безусловный переход на target
                        size_t patch_location = ctx.offset;
                        emit_jal_phys(&ctx, 0, 0); // JAL x0, <target> (временный offset)
                        jit_context_add_patchpoint(&ctx, patch_location, source_bytecode_offset, 
                                                  target_bytecode_offset, false, 0);
                        
                        // ВАЖНО: Не добавляем code между BEQ - если BEQ не сработал, 
                        // продолжаем со следующей проверкой
                    }
                } else {
                    // Для больших таблиц используем более эффективный подход
                    // Генерируем jump через вычисляемый адрес
                    // Сохраняем таблицу адресов в памяти и делаем indirect jump
                    
                    // Упрощенная реализация: используем цепочку для любого размера
                    for (uint16_t i = 0; i < num_targets; i++) {
                        int16_t target_offset;
                        memcpy(&target_offset, table_start + i * sizeof(int16_t), sizeof(int16_t));
                        size_t target_bytecode_offset = source_bytecode_offset + target_offset;
                        
                        if (i < 2047) {
                            emit_addi_phys(&ctx, 6, 0, (int16_t)i);
                        } else {
                            uint32_t hi = ((i + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(i - hi);
                            emit_lui_phys(&ctx, 6, hi);
                            if (lo != 0) emit_addi_phys(&ctx, 6, 6, lo);
                        }
                        
                        // Если index != i, пропускаем
                        emit_bne_phys(&ctx, 5, 6, 8); // BNE t0, t1, +8
                        
                        // Если index == i, переходим на target
                        size_t patch_location = ctx.offset;
                        emit_jal_phys(&ctx, 0, 0);
                        jit_context_add_patchpoint(&ctx, patch_location, source_bytecode_offset, 
                                                  target_bytecode_offset, false, 0);
                    }
                }
                
                // После всех проверок, если ни один BEQ не сработал, делаем безусловный переход на default
                size_t fallthrough_patch_location = ctx.offset;
                emit_jal_phys(&ctx, 0, 0); // Безусловный переход на default
                
                // Патчим fall-through переход на default
                size_t default_target_bytecode_offset = source_bytecode_offset + default_offset;
                jit_context_add_patchpoint(&ctx, fallthrough_patch_location, source_bytecode_offset,
                                          default_target_bytecode_offset, false, 0);
                
                break;
            }
            
            case 0x02: { // BR offset16 (безусловный переход)
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                ctx.last_cmp_result_reg = 0xFF;

                size_t source_bytecode_offset = (pc - bytecode_start) - 3; // Начало инструкции BR (opcode + offset)
                size_t target_bytecode_offset = source_bytecode_offset + offset;
                size_t fallthrough_bytecode_offset = (size_t)(pc - bytecode_start);

                // Flush stable cache before branch
                VCACHE_FLUSH_ALL();

                // selective flush: пишем только если значение действительно нужно в successor
                ph_flush_selective_for_branch(&ctx, &ph, bytecode_start, end, target_bytecode_offset, fallthrough_bytecode_offset);
                ph_reset(&ph); // Целевой offset в байт-коде
                
                // Генерируем JAL с временным offset (будет исправлен позже)
                size_t patch_location = ctx.offset;
                emit_jal_phys(&ctx, 0, 0); // rd=0 (x0), offset=0 (временно)
                
                // Добавляем patchpoint для отложенной фиксации
                jit_context_add_patchpoint(&ctx, patch_location, source_bytecode_offset, 
                                          target_bytecode_offset, false, 0);
                
                break;
            }
            
            case 0x03: { // BR_IF Rcond, offset16 (условный переход если Rcond != 0)
                // peephole: перед ветвлением делаем selective flush
                // (но cache в любом случае сбрасываем после BR_IF)
                if (no_spill_fastpath) {
                    uint8_t rcond = *pc++;
                    int16_t offset;
                    memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                    size_t source_bytecode_offset = (pc - bytecode_start) - 4;
                    size_t target_bytecode_offset = source_bytecode_offset + offset;
                    size_t fallthrough_bytecode_offset = (size_t)(pc - bytecode_start);

                    ph_flush_selective_for_branch(&ctx, &ph, bytecode_start, end, target_bytecode_offset, fallthrough_bytecode_offset);
                    ph_reset(&ph);

                    uint8_t pcnd = FP_MAP_VREG(rcond);

                    // If register is out of range (returns 0), fall back to slow path
                    if (pcnd != 0) {
                        // bne pcnd, x0, <patch>
                        size_t patch_location = ctx.offset;
                        emit_bne_phys(&ctx, pcnd, 0, 0);
                        jit_context_add_patchpoint(&ctx, patch_location, source_bytecode_offset, target_bytecode_offset, true, pcnd);
                        break;
                    }
                    // Fall through to slow path if register out of range
                    pc -= 4; // Rewind pc to re-read operands
                }
                uint8_t rcond = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                size_t source_bytecode_offset = (pc - bytecode_start) - 4; // Начало инструкции BR_IF (opcode + rcond + offset)
                size_t target_bytecode_offset = source_bytecode_offset + offset;
                size_t fallthrough_bytecode_offset = (size_t)(pc - bytecode_start);

                ph_flush_selective_for_branch(&ctx, &ph, bytecode_start, end, target_bytecode_offset, fallthrough_bytecode_offset);
                ph_reset(&ph);

                // Целевой offset в байт-коде
                
                // CMP+BR_IF ОПТИМИЗАЦИЯ: Если rcond == last_cmp_result_reg и результат в t0
                // то не нужен load!
                // Для корректности: всегда загружаем условие из памяти
                emit_lw_phys(&ctx, 5, rcond * 8, 18);  // t0 = v_regs[rcond]
                ctx.last_cmp_result_reg = 0xFF;  // сброс трекера
                
                // Генерируем BNE с временным offset (будет исправлен позже)
                // BNE t0, x0, target (если t0 != 0, прыгаем)
                size_t patch_location = ctx.offset;
                emit_bne_phys(&ctx, 5, 0, 0); // offset=0 (временно)
                
                // Добавляем patchpoint для отложенной фиксации
                // ВАЖНО: условие реально в регистре t0 (x5), не в виртуальном rcond
                jit_context_add_patchpoint(&ctx, patch_location, source_bytecode_offset, 
                                          target_bytecode_offset, true, 5);
                
                break;
            }
            
            case 0x09: { // CALL_IMPORT (через JIT helper)
                uint16_t import_idx;
                memcpy(&import_idx, pc, sizeof(import_idx));
                pc += sizeof(import_idx);

                // Extended format (0xAA) provides variadic type info.
                // If absent, use signature's param count.
                uint8_t has_variadic_info = 0;
                uint8_t num_args = 0;
                uint8_t arg_types_u8[16] = {0};

                if (pc < end && *pc == 0xAA) {
                    has_variadic_info = 1;
                    pc++;
                    num_args = *pc++;
                    if (num_args > 16) {
                        printf("[JIT ERROR] CALL_IMPORT with num_args=%u > 16 in func_idx=%u\n", num_args, func_idx);
                        free(exec_buffer);
                        *out_code = NULL;
                        *out_size = 0;
                        return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                    }
                    // Read type info (needed for variadic printf)
                    for (uint8_t i = 0; i < num_args; i++) {
                        arg_types_u8[i] = *pc++;
                    }
                } else {
                    // No extended format: use import signature's param count
                    if (import_idx < instance->module->num_imports) {
                        EspbImportDesc *imp_desc = &instance->module->imports[import_idx];
                        if (imp_desc->kind == ESPB_IMPORT_KIND_FUNC) {
                            uint16_t sig_idx = imp_desc->desc.func.type_idx;
                            if (sig_idx < instance->module->num_signatures) {
                                num_args = instance->module->signatures[sig_idx].num_params;
                            }
                        }
                    }
                }

                // OPTIMIZED: Minimal frame + space for arg_types[16] if variadic
                int frame_size_import = has_variadic_info ? 32 : 16;
                emit_addi_phys(&ctx, 2, 2, (int16_t)(-frame_size_import));
                emit_sw_phys(&ctx, 18, 0, 2);   // s2
                emit_sw_phys(&ctx, 1, 4, 2);    // ra

                // If variadic, store arg_types[16] at sp+8
                if (has_variadic_info) {
                    for (int i = 0; i < num_args && i < 16; ++i) {
                        emit_addi_phys(&ctx, 5, 0, (int16_t)(uint16_t)arg_types_u8[i]);
                        emit_sb_phys(&ctx, 5, (int16_t)(8 + i), 2);
                    }
                }

                // a0 = instance (s1)
                emit_addi_phys(&ctx, 10, 9, 0);

                // a1 = import_idx
                if (import_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)import_idx);
                } else {
                    uint32_t hi = ((import_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(import_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }

                // a2 = v_regs (s2)
                emit_addi_phys(&ctx, 12, 18, 0);

                // a3 = num_virtual_regs (u16)
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 13, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 13, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 13, 13, lo);
                }

                // a4 = has_variadic_info
                emit_addi_phys(&ctx, 14, 0, (int16_t)has_variadic_info);
                // a5 = num_total_args
                emit_addi_phys(&ctx, 15, 0, (int16_t)num_args);

                // a6 = &arg_types[0] or NULL
                if (has_variadic_info) {
                    emit_addi_phys(&ctx, 16, 2, 8);  // sp+8
                } else {
                    emit_addi_phys(&ctx, 16, 0, 0);
                }

                // NOTE: Debug call removed - it was clobbering R0-R7 before CALL_IMPORT!
                // Setup args for espb_jit_call_import
                emit_addi_phys(&ctx, 10, 9, 0);     // a0=instance
                if (import_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)import_idx);
                } else {
                    uint32_t hi = ((import_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(import_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }
                emit_addi_phys(&ctx, 12, 18, 0);    // a2=v_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 13, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 13, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 13, 13, lo);
                }
                emit_addi_phys(&ctx, 14, 0, (int16_t)has_variadic_info);
                emit_addi_phys(&ctx, 15, 0, (int16_t)num_args);
                if (has_variadic_info) {
                    emit_addi_phys(&ctx, 16, 2, 8);
                } else {
                    emit_addi_phys(&ctx, 16, 0, 0);
                }

                // Call helper: espb_jit_call_import(instance, import_idx, v_regs, num_virtual_regs,
                //                                 has_variadic_info, num_total_args, arg_types_ptr)
                emit_call_helper(&ctx, (uintptr_t)&espb_jit_call_import);

                // Restore
                emit_lw_phys(&ctx, 18, 0, 2);   // s2
                emit_lw_phys(&ctx, 1, 4, 2);    // ra
                emit_addi_phys(&ctx, 2, 2, (int16_t)frame_size_import);

                break;
            }
            
            case 0x0A: { // CALL func_idx (вызов внутренней функции)
                ctx.last_cmp_result_reg = 0xFF;

                // CALL использует локальный индекс функции (не включая импорты)
                uint16_t local_func_idx;
                memcpy(&local_func_idx, pc, sizeof(local_func_idx)); pc += sizeof(local_func_idx);

                // ОПТИМИЗАЦИЯ: минимальный overhead вызова.
                // jit_call_espb_function() — обычная C функция и по ABI НЕ должна портить s1/s2.
                // Поэтому не сохраняем a0-a7/t0-t6/s2 на каждый CALL.

                // a0 = instance (s1)
                emit_addi_phys(&ctx, 10, 9, 0);

                // a1 = local_func_idx (helper сам преобразует в global)
                if (local_func_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)local_func_idx);
                } else {
                    uint32_t hi = ((local_func_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(local_func_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }

                // a2 = v_regs (s2)
                emit_addi_phys(&ctx, 12, 18, 0);

                // Call helper directly (emit_call_helper uses t0=x5)
                emit_call_helper(&ctx, (uintptr_t)&jit_call_espb_function);

                break;
            }
            
            case 0x0D: { // CALL_INDIRECT_PTR Rfunc_ptr(u8), type_idx(u16)
                ctx.last_cmp_result_reg = 0xFF;

                uint8_t rptr = *pc++;
                uint16_t type_idx;
                memcpy(&type_idx, pc, sizeof(type_idx)); pc += sizeof(type_idx);

                // a0 = instance
                emit_addi_phys(&ctx, 10, 9, 0);

                // a1 = target_ptr (PTR) from v_regs[rptr]
                emit_lw_phys(&ctx, 11, rptr * 8, 18);

                // a2 = type_idx
                if (type_idx < 2048) {
                    emit_addi_phys(&ctx, 12, 0, (int16_t)type_idx);
                } else {
                    uint32_t hi = ((type_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(type_idx - hi);
                    emit_lui_phys(&ctx, 12, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 12, 12, lo);
                }

                // a3 = v_regs
                emit_addi_phys(&ctx, 13, 18, 0);

                // a4 = num_virtual_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 14, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 14, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 14, 14, lo);
                }

                // a5 = func_ptr_reg
                emit_addi_phys(&ctx, 15, 0, (int16_t)rptr);

                emit_call_helper(&ctx, (uintptr_t)&espb_jit_call_indirect_ptr);
                break;
            }

            case 0x0B: { // CALL_INDIRECT Rfunc(u8), type_idx(u16)
                ctx.last_cmp_result_reg = 0xFF;

                // Читаем операнды
                uint8_t r_func_idx = *pc++;
                uint16_t expected_type_idx;
                memcpy(&expected_type_idx, pc, sizeof(expected_type_idx)); pc += sizeof(expected_type_idx);

                // ИСПРАВЛЕНО: Используем espb_jit_call_indirect вместо jit_call_espb_function
                // Это позволяет обрабатывать как прямые local_func_idx, так и указатели в data segment
                // (аналогично логике интерпретатора op_0x0B)

                // a0 = instance (s1/x9)
                emit_addi_phys(&ctx, 10, 9, 0);

                // a1 = func_idx_or_ptr из v_regs[r_func_idx]
                emit_lw_phys(&ctx, 11, r_func_idx * 8, 18);

                // a2 = type_idx
                if (expected_type_idx < 2048) {
                    emit_addi_phys(&ctx, 12, 0, (int16_t)expected_type_idx);
                } else {
                    uint32_t hi = ((expected_type_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(expected_type_idx - hi);
                    emit_lui_phys(&ctx, 12, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 12, 12, lo);
                }

                // a3 = v_regs (s2/x18)
                emit_addi_phys(&ctx, 13, 18, 0);

                // a4 = num_virtual_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 14, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 14, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 14, 14, lo);
                }

                // a5 = func_idx_reg
                emit_addi_phys(&ctx, 15, 0, (int16_t)r_func_idx);

                // Вызываем новый helper с поддержкой func_ptr_map lookup
                emit_call_helper(&ctx, (uintptr_t)&espb_jit_call_indirect);

                break;
            }
            
            case 0x84: { // LOAD.I32 Rd, Ra, offset
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                emit_lw_phys(&ctx, 5, ra * 8, 18);
                if (offset >= -2048 && offset < 2048) {
                    emit_lw_phys(&ctx, 5, offset, 5);
                } else {
                    emit_addi_phys(&ctx, 6, 0, offset);
                    emit_add_phys(&ctx, 5, 5, 6);
                    emit_lw_phys(&ctx, 5, 0, 5);
                }
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }

            case 0x86: { // LOAD.F32 Rd, Ra, offset (load raw f32 bits)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                emit_lw_phys(&ctx, 5, ra * 8, 18);
                if (offset >= -2048 && offset < 2048) {
                    emit_lw_phys(&ctx, 5, offset, 5);
                } else {
                    emit_addi_phys(&ctx, 6, 0, offset);
                    emit_add_phys(&ctx, 5, 5, 6);
                    emit_lw_phys(&ctx, 5, 0, 5);
                }
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0x85: { // LOAD.I64 Rd, Ra, offset
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                emit_lw_phys(&ctx, 5, ra * 8, 18);
                if (offset >= -2048 && offset < 2048) {
                    emit_lw_phys(&ctx, 6, offset, 5);
                } else {
                    // offset doesn't fit in 12-bit immediate: materialize full 32-bit constant
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_lw_phys(&ctx, 6, 0, 7);
                }
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                if (offset + 4 >= -2048 && offset + 4 < 2048) {
                    emit_lw_phys(&ctx, 6, offset + 4, 5);
                } else {
                    int32_t off32 = (int32_t)offset + 4;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_lw_phys(&ctx, 6, 0, 7);
                }
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x87: { // LOAD.F64 Rd, Ra, offset (load raw f64 bits)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                // Same encoding/storage as I64 load
                emit_lw_phys(&ctx, 5, ra * 8, 18);

                // low word
                if (offset >= -2048 && offset < 2048) {
                    emit_lw_phys(&ctx, 6, offset, 5);
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_lw_phys(&ctx, 6, 0, 7);
                }
                emit_sw_phys(&ctx, 6, rd * 8, 18);

                // high word
                int16_t offset_hi = (int16_t)(offset + 4);
                if (offset_hi >= -2048 && offset_hi < 2048) {
                    emit_lw_phys(&ctx, 6, offset_hi, 5);
                } else {
                    int32_t off32 = (int32_t)offset + 4;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_lw_phys(&ctx, 6, 0, 7);
                }
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x88: { // LOAD.PTR Rd, Ra, offset (load 32-bit pointer)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                // NO_SPILL fast-path: get address from physical register if in range
                uint8_t addr_reg = 5;  // default: use t0
                if (no_spill_fastpath && i32_only) {
                    uint8_t pa = FP_MAP_VREG(ra);
                    if (pa != 0) {
                        addr_reg = pa;
                    } else {
                        emit_lw_phys(&ctx, 5, ra * 8, 18);
                    }
                } else {
                    emit_lw_phys(&ctx, 5, ra * 8, 18);
                }

                // load pointer-sized value (RV32 => 4 bytes)
                if (offset >= -2048 && offset < 2048) {
                    emit_lw_phys(&ctx, 6, offset, addr_reg);
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, addr_reg, 7);
                    emit_lw_phys(&ctx, 6, 0, 7);
                }

                // NO_SPILL fast-path: store result to physical register if in range
                if (no_spill_fastpath && i32_only) {
                    uint8_t pd = FP_MAP_VREG(rd);
                    if (pd != 0) {
                        emit_addi_phys(&ctx, pd, 6, 0);  // mv pd, t1
                        break;
                    }
                }

                emit_sw_phys(&ctx, 6, rd * 8, 18);
                // clear high word for ptr
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            
            case 0x74: { // STORE.I32 Rs, Ra, offset
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Загружаем значение из v_regs[rs]
                emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs]
                
                // Загружаем базовый адрес из v_regs[ra]
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra]
                
                // Сохраняем в память
                if (offset >= -2048 && offset < 2048) {
                    emit_sw_phys(&ctx, 6, offset, 5);  // *(t0 + offset) = t1
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 5, 5, 7);
                    emit_sw_phys(&ctx, 6, 0, 5);
                }
                break;
            }

            case 0x78: { // STORE.F32 Rs, Ra, offset (store raw f32 bits)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                // f32 stored in low 32 bits of v_regs[rs]
                emit_lw_phys(&ctx, 6, rs * 8, 18);
                emit_lw_phys(&ctx, 5, ra * 8, 18);

                if (offset >= -2048 && offset < 2048) {
                    emit_sw_phys(&ctx, 6, offset, 5);
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 5, 5, 7);
                    emit_sw_phys(&ctx, 6, 0, 5);
                }
                break;
            }

            case 0x79: { // STORE.F64 Rs, Ra, offset (store raw f64 bits)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);

                // f64 stored as u64 across low/high 32-bit words
                emit_lw_phys(&ctx, 6, rs * 8, 18);      // t1 = lo
                emit_lw_phys(&ctx, 7, rs * 8 + 4, 18);  // t2 = hi
                emit_lw_phys(&ctx, 5, ra * 8, 18);      // t0 = base

                if (offset >= -2048 && offset < 2048) {
                    emit_sw_phys(&ctx, 6, offset, 5);
                    emit_sw_phys(&ctx, 7, offset + 4, 5);
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 28, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 28, 28, lo);
                    emit_add_phys(&ctx, 5, 5, 28);
                    emit_sw_phys(&ctx, 6, 0, 5);
                    emit_sw_phys(&ctx, 7, 4, 5);
                }
                break;
            }
            
            case 0x76: { // STORE.I64 Rs, Ra, offset
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Загружаем базовый адрес
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra]
                
                // Загружаем младшие 32 бита из v_regs[rs]
                emit_lw_phys(&ctx, 6, rs * 8, 18);
                
                // Сохраняем младшие 32 бита
                if (offset >= -2048 && offset < 2048) {
                    emit_sw_phys(&ctx, 6, offset, 5);
                } else {
                    int32_t off32 = (int32_t)offset;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_sw_phys(&ctx, 6, 0, 7);
                    emit_lw_phys(&ctx, 5, ra * 8, 18);  // Перезагрузить базовый адрес
                }
                
                // Загружаем старшие 32 бита из v_regs[rs] + 4
                emit_lw_phys(&ctx, 6, rs * 8 + 4, 18);
                
                // Сохраняем старшие 32 бита
                if (offset + 4 >= -2048 && offset + 4 < 2048) {
                    emit_sw_phys(&ctx, 6, offset + 4, 5);
                } else {
                    int32_t off32 = (int32_t)offset + 4;
                    uint32_t hi = (((uint32_t)off32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(off32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 7, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 7, 7, lo);
                    emit_add_phys(&ctx, 7, 5, 7);
                    emit_sw_phys(&ctx, 6, 0, 7);
                }
                break;
            }
            
            case 0x90: { // TRUNC.I64.I32 Rd, Rs (обрезка 64->32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Просто копируем младшие 32 бита
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0x96: { // ZEXT.I8.I16 Rd, Rs (zero-extend 8->16)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // andi t0, t0, 0xFF (маска младших 8 бит)
                uint32_t andi_instr = (0xFF << 20) | (5 << 15) | (0b111 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, andi_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }

            case 0x97: { // ZEXT.I8.I32 Rd, Rs (zero-extend 8->32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // andi t0, t0, 0xFF (маска младших 8 бит)
                uint32_t andi_instr = (0xFF << 20) | (5 << 15) | (0b111 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, andi_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }

            case 0x98: { // ZEXT.I8.I64 Rd, Rs (zero-extend 8->64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                // andi t0, t0, 0xFF
                uint32_t andi_instr = (0xFF << 20) | (5 << 15) | (0b111 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, andi_instr);

                // store low32
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                // store high32 = 0
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            
            case 0x99: { // ZEXT.I16.I32 Rd, Rs (zero-extend 16->32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // Маскируем младшие 16 бит: lui + and
                // lui t1, 0 + addi t1, t1, 0xFFFF не работает, используем сдвиги
                // slli t0, t0, 16
                uint32_t slli_instr = (16 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);
                
                // srli t0, t0, 16
                uint32_t srli_instr = (16 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srli_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }

            case 0x9C: { // SEXT.I8.I16 Rd, Rs (sign-extend 8->16)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                // slli t0, t0, 24
                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                // srai t0, t0, 24 (sign-extend 8->32)
                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0x92: { // TRUNC.I64.I8 Rd, Rs (truncate to I8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Use low 32 bits of v_regs[rs], truncate to I8 with sign-extension back to I32
                emit_lw_phys(&ctx, 5, rs * 8, 18);

                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            case 0x95: { // TRUNC.I16.I8 Rd, Rs (truncate to I8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                // slli t0, t0, 24
                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                // srai t0, t0, 24
                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            case 0x94: { // TRUNC.I32.I8 alias
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }

            case 0x9E: { // SEXT.I8.I64 Rd, Rs (sign-extend 8->64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                // slli t0, t0, 24 (shift left to get lowest byte in highest position)
                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                // srai t0, t0, 24 (arithmetic shift right to sign-extend low 32)
                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                // store low 32 bits
                emit_sw_phys(&ctx, 5, rd * 8, 18);

                // sign-extend into high 32 bits: srai t0, t0, 31
                uint32_t srai31_instr = (0b0100000 << 25) | (31 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai31_instr);
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                break;
            }
            
            case 0x93: { // TRUNC.I32.I16 Rd, Rs (truncate to I16 with sign-extension back to I32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Truncate I32 to I16 with sign-extension back to I32
                // Strategy: shift left 16 bits, then arithmetic shift right 16 bits
                emit_lw_phys(&ctx, 5, rs * 8, 18);  // t0 = v_regs[rs]
                
                // slli t0, t0, 16 (shift left to get lowest halfword in highest position)
                uint32_t slli_instr = (16 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);
                
                // srai t0, t0, 16 (arithmetic shift right to sign-extend)
                uint32_t srai_instr = (0b0100000 << 25) | (16 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0x9B: { // ZEXT.I32.I64 Rd, Rs (zero-extend 32->64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Копируем младшие 32 бита
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                // Обнуляем старшие 32 бита
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);  // x0 = 0
                break;
            }
            
            case 0x9D: { // SEXT.I8.I32 Rd, Rs (sign-extend 8->32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // Сдвигаем влево на 24, затем вправо арифметически на 24
                // slli t0, t0, 24
                uint32_t slli_instr = (24 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);
                
                // srai t0, t0, 24 (arithmetic shift right)
                uint32_t srai_instr = (0b0100000 << 25) | (24 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0x9F: { // SEXT.I16.I32 Rd, Rs (sign-extend 16->32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // slli t0, t0, 16
                uint32_t slli_instr = (16 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);
                
                // srai t0, t0, 16
                uint32_t srai_instr = (0b0100000 << 25) | (16 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0xA0: { // SEXT.I16.I64 Rd, Rs (sign-extend 16->64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 5, rs * 8, 18);

                // slli t0, t0, 16
                uint32_t slli_instr = (16 << 20) | (5 << 15) | (0b001 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, slli_instr);

                // srai t0, t0, 16 (sign-extend to 32)
                uint32_t srai_instr = (0b0100000 << 25) | (16 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                // store low 32 bits
                emit_sw_phys(&ctx, 5, rd * 8, 18);

                // sign-extend into high 32 bits: srai t0, t0, 31
                uint32_t srai31_instr = (0b0100000 << 25) | (31 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai31_instr);
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                break;
            }
            case 0xA1: { // SEXT.I32.I64 alias
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // Копируем младшие 32 бита
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                emit_sw_phys(&ctx, 5, rd * 8, 18);

                // Расширяем знак в старшие 32 бита: srai t0, t0, 31
                uint32_t srai_instr = (0b0100000 << 25) | (31 << 20) | (5 << 15) | (0b101 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, srai_instr);

                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                break;
            }

            // Float arithmetic F32 (0x60-0x67)
            case 0x60: { // ADD.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                // load r1 -> a0
                if (vcache.kind == VC_F32 && vcache.vreg == r1) {
                    emit_addi_phys(&ctx, 10, 20, 0); // mv a0, s4
                } else {
                    emit_lw_phys(&ctx, 10, r1 * 8, 18);
                }

                // load r2 -> a1
                if (vcache.kind == VC_F32 && vcache.vreg == r2) {
                    emit_addi_phys(&ctx, 11, 20, 0); // mv a1, s4
                } else {
                    emit_lw_phys(&ctx, 11, r2 * 8, 18);
                }

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fadd_f32_bits);

                // evict old cached value if dirty
                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }

                // keep result in stable cache (s4)
                emit_addi_phys(&ctx, 20, 10, 0); // mv s4, a0
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x61: { // SUB.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == r1) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, r1 * 8, 18);

                if (vcache.kind == VC_F32 && vcache.vreg == r2) emit_addi_phys(&ctx, 11, 20, 0);
                else emit_lw_phys(&ctx, 11, r2 * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fsub_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x62: { // MUL.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == r1) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, r1 * 8, 18);

                if (vcache.kind == VC_F32 && vcache.vreg == r2) emit_addi_phys(&ctx, 11, 20, 0);
                else emit_lw_phys(&ctx, 11, r2 * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fmul_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x63: { // DIV.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == r1) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, r1 * 8, 18);

                if (vcache.kind == VC_F32 && vcache.vreg == r2) emit_addi_phys(&ctx, 11, 20, 0);
                else emit_lw_phys(&ctx, 11, r2 * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fdiv_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x64: { // MIN.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == r1) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, r1 * 8, 18);

                if (vcache.kind == VC_F32 && vcache.vreg == r2) emit_addi_phys(&ctx, 11, 20, 0);
                else emit_lw_phys(&ctx, 11, r2 * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fmin_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x65: { // MAX.F32
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == r1) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, r1 * 8, 18);

                if (vcache.kind == VC_F32 && vcache.vreg == r2) emit_addi_phys(&ctx, 11, 20, 0);
                else emit_lw_phys(&ctx, 11, r2 * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fmax_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            case 0x66: { // ABS.F32 (inline: clear sign bit)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // t0 = v_regs[rs]
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                // clear MSB: (t0 << 1) >> 1
                // slli t0, t0, 1
                emit_instr(&ctx, (1u << 20) | (5u << 15) | (0b001u << 12) | (5u << 7) | 0b0010011u);
                // srli t0, t0, 1
                emit_instr(&ctx, (1u << 20) | (5u << 15) | (0b101u << 12) | (5u << 7) | 0b0010011u);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            case 0x67: { // SQRT.F32
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                if (vcache.kind == VC_F32 && vcache.vreg == rs) emit_addi_phys(&ctx, 10, 20, 0);
                else emit_lw_phys(&ctx, 10, rs * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fsqrt_f32_bits);

                if (vcache.kind == VC_F32 && vcache.dirty && vcache.vreg != rd) {
                    emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                }
                emit_addi_phys(&ctx, 20, 10, 0);
                vcache.kind = VC_F32;
                vcache.dirty = true;
                vcache.vreg = rd;
                break;
            }
            
            // Float arithmetic F64 (0x68-0x6D)
            case 0x68: { // ADD.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_add_f64);

                VCACHE_STORE_F64_RESULT(rd);
                break;
            }
            
            case 0x69: { // SUB.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_sub_f64);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }
            
            case 0x6A: { // MUL.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_mul_f64);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }
            
            case 0x6B: { // DIV.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_div_f64);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }

            case 0x6C: { // MIN.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fmin_f64_bits);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }

            case 0x6D: { // MAX.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                VCACHE_LOAD_F64(r1, 10, 11);
                VCACHE_LOAD_F64(r2, 12, 13);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fmax_f64_bits);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }

            case 0x6E: { // ABS.F64 (inline: clear sign bit)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // lo/hi = v_regs[rs]
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                emit_lw_phys(&ctx, 6, rs * 8 + 4, 18);

                // clear MSB of high word: (hi << 1) >> 1
                emit_instr(&ctx, (1u << 20) | (6u << 15) | (0b001u << 12) | (6u << 7) | 0b0010011u); // slli x6,x6,1
                emit_instr(&ctx, (1u << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0010011u); // srli x6,x6,1

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x6F: { // SQRT.F64 (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                VCACHE_LOAD_F64(rs, 10, 11);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fsqrt_f64_bits);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }
            
            case 0xA5: { // FPROMOTE Rd, Rs (F32 -> F64) (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                if (stable_cache_enabled && vcache.kind == VC_F32 && vcache.vreg == rs) {
                    emit_addi_phys(&ctx, 10, VC0_LO, 0);
                } else {
                    emit_lw_phys(&ctx, 10, rs * 8, 18);
                }

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fpromote_f32_to_f64_bits);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }

            case 0xAC: { // CVT.F64.I32 Rd, Rs  (F64 -> I32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // a0/a1 = raw f64 bits
                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f64_i32);

                // return i32 in a0
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                break;
            }

            case 0xAF: { // CVT.U32.F64 Rd, Rs - Convert U32 to F64
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // a0 = u32
                emit_lw_phys(&ctx, 10, rs * 8, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_u32_f64);

                // return double in a0/a1
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }
            
            case 0xB1: { // CVT.U64.F64 - Convert U64 to F64
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // a0/a1 = v_regs[rs] (low/high)
                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_u64_f64);

                // return double in a0/a1
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }

            case 0xB4: { // CVT.I64.F32 Rd, Rs (I64 -> F32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // a0/a1 = i64 bits
                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_i64_f32_bits);

                // result raw f32 bits in a0
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                break;
            }

            case 0xB5: { // CVT.I64.F64 Rd, Rs (I64 -> F64) (2-entry typed-cache)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // a0/a1 = i64 bits
                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);

                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_i64_f64_bits);
                VCACHE_STORE_F64_RESULT(rd);
                break;
            }
            
            case 0xA4: { // FPROUND (F64 -> F32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);
                
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_fpround_f64_to_f32_bits);
                
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xA6: { // CVT.F32.U32 (F32 -> U32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f32_u32);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);

                break;
            }

            case 0xA7: { // CVT.F32.U64 (F32 -> U64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f32_u64);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }

            case 0xA8: { // CVT.F64.U32 (F64 -> U32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f64_u32);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);

                break;
            }

            case 0xA9: { // CVT.F64.U64 (F64 -> U64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f64_u64);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }

            case 0xAA: { // CVT.F32.I32 (F32 -> I32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f32_i32);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_srai_phys(&ctx, 11, 10, 31);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }

            case 0xAB: { // CVT.F32.I64 (F32 -> I64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f32_i64);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }

            case 0xAD: { // CVT.F64.I64 (F64 -> I64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_f64_i64);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }

            case 0xAE: { // CVT.U32.F32 (U32 -> F32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_u32_f32_bits);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);

                break;
            }

            case 0xB0: { // CVT.U64.F32 (U64 -> F32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_u64_f32_bits);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);

                break;
            }

            case 0xB2: { // CVT.I32.F32 (I32 -> F32)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_i32_f32_bits);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);

                break;
            }

            case 0xB3: { // CVT.I32.F64 (I32 -> F64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                emit_lw_phys(&ctx, 10, rs * 8, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_cvt_i32_f64_bits);

                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);

                break;
            }
            
            // Other float opcodes not yet implemented - fall back to interpreter
            case 0xA2: case 0xA3: {  // (remaining unsupported conversions)

                printf("[JIT ERROR] Float/unsupported opcode 0x%02X in func_idx=%u at offset %zu\n", opcode, func_idx, bytecode_offset);
                free(exec_buffer);
                *out_code = NULL;
                *out_size = 0;
                return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
            }
            
            case 0x8F: { // ALLOCA Rd, Rs, align - HEAP-BASED через helper
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                uint8_t align_param = *pc++;
                
                // ОПТИМИЗАЦИЯ #6: Убрана отладка из hot path
                // printf("[JIT-COMPILE] ALLOCA at offset %ld: rd=%u, rs=%u, align=%u\n", 
                //        (long)(pc - bytecode - 3), rd, rs, align_param);
                
                // Генерируем вызов helper: void espb_jit_alloca_ex(EspbInstance*, Value* v_regs, u16 num_regs_allocated, u8 rd, u8 rs, u8 align)
                // Сохраняем caller-saved
                emit_addi_phys(&ctx, 2, 2, -32);
                for (int i = 0; i < 7; ++i) emit_sw_phys(&ctx, 5 + i, i * 4, 2);
                emit_sw_phys(&ctx, 18, 28, 2);
                
                // a0 = instance (s1)
                emit_addi_phys(&ctx, 10, 9, 0);
                // a1 = v_regs (s2)
                emit_addi_phys(&ctx, 11, 18, 0);

                // a2 = num_regs_allocated (max_reg_used + 1)
                uint16_t num_regs_allocated = (uint16_t)max_reg_used + 1;
                if (num_regs_allocated < 2048) {
                    emit_addi_phys(&ctx, 12, 0, (int16_t)num_regs_allocated);
                } else {
                    uint32_t hi = ((num_regs_allocated + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_regs_allocated - hi);
                    emit_lui_phys(&ctx, 12, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 12, 12, lo);
                }

                // a3 = rd
                emit_addi_phys(&ctx, 13, 0, rd);
                // a4 = rs
                emit_addi_phys(&ctx, 14, 0, rs);
                // a5 = align
                emit_addi_phys(&ctx, 15, 0, align_param);
                
                emit_call_helper(&ctx, (uintptr_t)&espb_jit_alloca_ex);
                
                // Restore
                for (int i = 0; i < 7; ++i) emit_lw_phys(&ctx, 5 + i, i * 4, 2);
                emit_lw_phys(&ctx, 18, 28, 2);
                emit_addi_phys(&ctx, 2, 2, 32);
                
                break;
            }
            
            case 0x10: { // MOV.I8 Rd, Rs (8-bit move)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Загружаем 32-bit значение (младшие 8 бит валидны)
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // Сохраняем обратно (копируем весь 32-bit word)
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                break;
            }
            
            case 0x11: { // MOV.I16 Rd, Rs (16-bit move)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Загружаем 32-bit значение (младшие 16 бит валидны)
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                
                // Сохраняем обратно
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                break;
            }
            
            case 0xBC: { // PTRTOINT Rd(u8), Rs(u8) - Convert PTR to I32
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // PTRTOINT просто копирует 32-битное значение указателя в I32
                // Загружаем нижние 32 бита (указатель) из v_regs[rs]
                emit_lw_phys(&ctx, 5, rs * 8, 18);   // t0 = v_regs[rs] (PTR)
                emit_sw_phys(&ctx, 5, rd * 8, 18);   // v_regs[rd] = t0
                
                // Очищаем старшие 32 бита
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xBD: { // INTTOPTR Rd(u8), Rs(u8) - Convert I32 to PTR
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // INTTOPTR просто копирует 32-битное значение из rs в rd
                // В ESPB PTR и I32 занимают одинаковое пространство (32 бита)
                emit_lw_phys(&ctx, 5, rs * 8, 18);   // Загружаем значение из v_regs[rs]
                emit_sw_phys(&ctx, 5, rd * 8, 18);   // Сохраняем в v_regs[rd]
                
                // Очищаем старшие 32 бита (для PTR всегда 0)
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xD7: // ATOMIC.RMW.ADD.I32
            case 0xD8: // ATOMIC.RMW.SUB.I32
            case 0xD9: // ATOMIC.RMW.AND.I32
            case 0xDA: // ATOMIC.RMW.OR.I32
            case 0xDB: // ATOMIC.RMW.XOR.I32
            case 0xDC: { // ATOMIC.RMW.XCHG.I32 (Swap/Exchange)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rv = *pc++;
                
                // Ra содержит адрес (PTR), Rv содержит значение
                // Rd получает старое значение
                
                // ESP32-C3 (RV32IMAC) does NOT support AMO instructions!
                // Use helper functions with GCC __atomic_* built-ins instead.
                
                // Select helper function based on opcode
                uintptr_t helper = 0;
                switch (opcode) {
                    case 0xD7: helper = (uintptr_t)&jit_atomic_fetch_add_4; break;
                    case 0xD8: helper = (uintptr_t)&jit_atomic_fetch_sub_4; break;
                    case 0xD9: helper = (uintptr_t)&jit_atomic_fetch_and_4; break;
                    case 0xDA: helper = (uintptr_t)&jit_atomic_fetch_or_4; break;
                    case 0xDB: helper = (uintptr_t)&jit_atomic_fetch_xor_4; break;
                    case 0xDC: helper = (uintptr_t)&jit_atomic_exchange_4; break;
                }
                
                // Prepare arguments: a0 = address (ptr), a1 = value
                // a0 = v_regs[ra].ptr (address)
                emit_lw_phys(&ctx, 10, ra * 8, 18);  // a0 = v_regs[ra]
                // a1 = v_regs[rv].i32 (value)
                emit_lw_phys(&ctx, 11, rv * 8, 18);  // a1 = v_regs[rv]
                
                // Call helper function
                emit_call_helper(&ctx, helper);
                
                // Result is in a0, store to v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);  // v_regs[rd].lo = a0
                
                // Zero high 32 bits
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xDE: { // ATOMIC.LOAD.I32 Rd(u8), Ra(u8)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                
                // ESP32-C3: Use helper function instead of LR.W
                // a0 = address
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // Call jit_atomic_load_4
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_load_4);
                
                // Result in a0, store to v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                
                // Zero high 32 bits
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xDF: { // ATOMIC.STORE.I32 Rs(u8), Ra(u8)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                
                // ESP32-C3: Use helper function instead of AMOSWAP
                // a0 = address
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                // a1 = value
                emit_lw_phys(&ctx, 11, rs * 8, 18);
                
                // Call jit_atomic_store_4
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_store_4);
                
                break;
            }
            
            case 0xDD: { // ATOMIC.RMW.CMPXCHG.I32 Rd(u8), Ra(u8), Rexp(u8), Rdes(u8)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rexp = *pc++;
                uint8_t rdes = *pc++;

                // ESP32-C3: Use helper function instead of LR/SC loop
                // jit_atomic_compare_exchange_4(ptr, &expected, desired) 
                // After the call, expected contains the actual value read
                
                // a0 = address (v_regs[ra].ptr)
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // a1 = &v_regs[rexp].i32 (pointer to expected value)
                // s2 (x18) contains v_regs base address
                emit_addi_phys(&ctx, 11, 18, rexp * 8);  // a1 = v_regs + rexp*8
                
                // a2 = desired (v_regs[rdes].i32)
                emit_lw_phys(&ctx, 12, rdes * 8, 18);
                
                // Call jit_atomic_compare_exchange_4
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_compare_exchange_4);
                
                // After the call, v_regs[rexp] contains the actual value that was read
                // Copy it to v_regs[rd]
                emit_lw_phys(&ctx, 5, rexp * 8, 18);  // t0 = v_regs[rexp] (actual value)
                emit_sw_phys(&ctx, 5, rd * 8, 18);   // v_regs[rd] = t0
                
                // Zero high 32 bits
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            // NOTE: Old 0xDD code was here, see new implementation above
            #if 0 // OLD CODE DISABLED
                // Ra содержит адрес (PTR)
                // Rexp содержит ожидаемое значение (expected)
                // Rdes содержит желаемое значение (desired)
                // Rd получит фактическое прочитанное значение
                
                // Загружаем адрес из v_regs[ra] в t1 (x6)
                emit_lw_phys(&ctx, 6, ra * 8, 18);  // t1 = v_regs[ra] (адрес)
                
                // Загружаем expected из v_regs[rexp] в t2 (x7)
                emit_lw_phys(&ctx, 7, rexp * 8, 18);  // t2 = v_regs[rexp] (expected)
                
                // Загружаем desired из v_regs[rdes] в a3 (x13)
                emit_lw_phys(&ctx, 13, rdes * 8, 18);  // a3 = v_regs[rdes] (desired)
                
                // Реализация CAS через LR/SC loop
                // retry:
                size_t retry_label = ctx.offset;
                
                // LR.W t0, (t1) - Load Reserved
                emit_lr_w_phys(&ctx, 5, 6);  // t0 = *addr (load reserved)
                
                // Сравниваем t0 с t2 (expected)
                // BNE t0, t2, done - если не равны, выходим
                size_t done_branch = ctx.offset;
                emit_bne_phys(&ctx, 5, 7, 0);  // Временный offset, будет исправлен
                
                // SC.W a4, a3, (t1) - Store Conditional
                emit_sc_w_phys(&ctx, 14, 13, 6);  // a4 = SC result (0=success, 1=fail)
                
                // BNE a4, x0, retry - если SC не удался, повторяем
                int32_t retry_offset = (int32_t)retry_label - (int32_t)ctx.offset;
                emit_bne_phys(&ctx, 14, 0, (int16_t)retry_offset);
                
                // done: (метка для выхода из CAS)
                size_t done_label = ctx.offset;
                
                // Патчим done_branch
                int32_t done_offset = (int32_t)done_label - (int32_t)done_branch;
                uint32_t* branch_instr = (uint32_t*)(ctx.buffer + done_branch);
                *branch_instr = encode_branch_instr(0b001, 5, 7, (int16_t)done_offset);
                
                // Сохраняем прочитанное значение в v_regs[rd]
                emit_sw_phys(&ctx, 5, rd * 8, 18);  // v_regs[rd] = t0
                
                // Очищаем старшие 32 бита
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            #endif // OLD CODE DISABLED
            
            case 0xF0: // ATOMIC.RMW.ADD.I64
            case 0xF1: // ATOMIC.RMW.SUB.I64
            case 0xF2: // ATOMIC.RMW.AND.I64
            case 0xF3: // ATOMIC.RMW.OR.I64
            case 0xF4: // ATOMIC.RMW.XOR.I64
            case 0xF5: { // ATOMIC.RMW.XCHG.I64
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rv = *pc++;
                
                // 64-битные атомарные операции используют __atomic_fetch_* с 64-битными значениями
                // Ra = адрес (PTR), Rv = 64-битное значение, Rd = получит старое 64-битное значение
                
                // a0 = адрес из v_regs[ra]
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // a1 = v_regs[rv] low, a2 = v_regs[rv] high (64-битное значение для операции)
                emit_lw_phys(&ctx, 11, rv * 8, 18);
                emit_lw_phys(&ctx, 12, rv * 8 + 4, 18);
                
                // a3 = __ATOMIC_SEQ_CST (5)
                emit_addi_phys(&ctx, 13, 0, 5);
                
                // Вызываем соответствующую wrapper функцию для атомарной операции
                // Результат возвращается в a0:a1 (64-битное значение)
                switch (opcode) {
                    case 0xF0: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_fetch_add_8); break;
                    case 0xF1: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_fetch_sub_8); break;
                    case 0xF2: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_fetch_and_8); break;
                    case 0xF3: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_fetch_or_8); break;
                    case 0xF4: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_fetch_xor_8); break;
                    case 0xF5: emit_call_helper(&ctx, (uintptr_t)&jit_atomic_exchange_8); break;
                }
                
                // Сохраняем результат (a0:a1) в v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);      // low
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);  // high
                
                break;
            }
            
            case 0xEC: { // ATOMIC.LOAD.I64 Rd(u8), Ra(u8)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                
                // Для 64-битной атомарной загрузки используем wrapper
                // a0 = адрес
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // Вызов __atomic_load_n для 64-бит
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_load_8);
                
                // Результат в a0:a1, сохраняем в v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);      // low
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);  // high
                
                break;
            }
            
            case 0xED: { // ATOMIC.STORE.I64 Rs(u8), Ra(u8)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                
                // a0 = адрес
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // a1 = value_low, a2 = value_high
                emit_lw_phys(&ctx, 11, rs * 8, 18);
                emit_lw_phys(&ctx, 12, rs * 8 + 4, 18);
                
                // Вызов __atomic_store_n для 64-бит
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_store_8);
                
                break;
            }
            
            case 0xEE: { // ATOMIC.FENCE
                // Генерируем FENCE инструкцию RISC-V
                // FENCE iorw, iorw (полный барьер памяти)
                // Формат: fm[31:28]=0000, pred[27:24]=1111, succ[23:20]=1111, rs1[19:15]=0, funct3[14:12]=000, rd[11:7]=0, opcode[6:0]=0001111
                emit_instr(&ctx, 0x0FF0000F);  // FENCE iorw, iorw
                
                break;
            }
            
            case 0xF6: { // ATOMIC.RMW.CMPXCHG.I64
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                uint8_t rexp = *pc++;
                uint8_t rdes = *pc++;
                
                // CAS для 64-битных значений
                // a0 = адрес
                emit_lw_phys(&ctx, 10, ra * 8, 18);
                
                // a1 = &expected (указатель на v_regs[rexp])
                emit_addi_phys(&ctx, 11, 18, rexp * 8);
                
                // a2 = desired_low, a3 = desired_high
                emit_lw_phys(&ctx, 12, rdes * 8, 18);
                emit_lw_phys(&ctx, 13, rdes * 8 + 4, 18);
                
                // a4 = success_memorder (__ATOMIC_SEQ_CST = 5)
                emit_addi_phys(&ctx, 14, 0, 5);
                
                // a5 = failure_memorder (__ATOMIC_SEQ_CST = 5)
                emit_addi_phys(&ctx, 15, 0, 5);
                
                // Вызов wrapper для atomic_compare_exchange
                emit_call_helper(&ctx, (uintptr_t)&jit_atomic_compare_exchange_8);
                
                // expected обновлён по месту в v_regs[rexp], копируем в v_regs[rd]
                emit_lw_phys(&ctx, 5, rexp * 8, 18);
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_lw_phys(&ctx, 5, rexp * 8 + 4, 18);
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x50: { // ADD.I64.IMM8 Rd(u8), Rs(u8), imm8(i8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;
                
                // 64-битное сложение с immediate
                // Загружаем значение (64-bit) из v_regs[rs]
                emit_lw_phys(&ctx, 5, rs * 8, 18);      // t0 = low (old_low)
                emit_lw_phys(&ctx, 6, rs * 8 + 4, 18);  // t1 = high
                
                // Сохраняем old_low в t3 ДО модификации
                emit_addi_phys(&ctx, 28, 5, 0);  // t3 = old_low
                
                // Добавляем imm к младшей части (imm8 всегда в диапазоне ADDI)
                emit_addi_phys(&ctx, 5, 5, imm);  // t0 = new_low = old_low + imm
                
                // Проверяем переполнение (carry) для старшей части
                if (imm >= 0) {
                    // Carry при сложении положительного: new_low < old_low (unsigned)
                    emit_sltu_phys(&ctx, 29, 5, 28);  // t4 = (new_low < old_low) ? 1 : 0
                    emit_add_phys(&ctx, 6, 6, 29);  // high += carry
                } else {
                    // Borrow при сложении отрицательного (вычитании): new_low > old_low (unsigned)
                    emit_sltu_phys(&ctx, 29, 28, 5);  // t4 = (old_low < new_low) ? 1 : 0
                    emit_sub_phys(&ctx, 6, 6, 29);  // high -= borrow
                }
                
                // Сохраняем результат
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x51: { // SUB.I64.IMM8 Rd(u8), Rs(u8), imm8(i8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;
                
                // 64-битное вычитание с immediate: rs - imm
                // Эквивалентно rs + (-imm)
                int16_t neg_imm = (int16_t)(-imm);  // Use int16_t to handle -(-128) = 128 correctly
                
                // Загружаем значение (64-bit) из v_regs[rs]
                emit_lw_phys(&ctx, 5, rs * 8, 18);      // t0 = old_low
                emit_lw_phys(&ctx, 6, rs * 8 + 4, 18);  // t1 = high
                
                // Сохраняем old_low в t3 ДО модификации
                emit_addi_phys(&ctx, 28, 5, 0);  // t3 = old_low
                
                // Вычитаем imm из младшей части (добавляем -imm)
                emit_addi_phys(&ctx, 5, 5, neg_imm);  // t0 = new_low = old_low - imm
                
                // Проверяем заём (borrow) для старшей части
                if (imm > 0) {
                    // Вычитаем положительное: borrow если new_low > old_low (unsigned wrap)
                    emit_sltu_phys(&ctx, 29, 28, 5);  // t4 = (old_low < new_low) ? 1 : 0
                    emit_sub_phys(&ctx, 6, 6, 29);  // high -= borrow
                } else if (imm < 0) {
                    // Вычитаем отрицательное (добавляем положительное): carry если new_low < old_low
                    emit_sltu_phys(&ctx, 29, 5, 28);  // t4 = (new_low < old_low) ? 1 : 0
                    emit_add_phys(&ctx, 6, 6, 29);  // high += carry
                }
                // imm == 0: no carry/borrow
                
                // Сохраняем результат
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x52: { // MUL.I64.IMM8 Rd(u8), Rs(u8), imm8(i8)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;
                
                // 64-битное умножение на immediate
                // Вызываем helper функцию для 64-bit multiplication
                // a0:a1 = multiplicand (from v_regs[rs])
                emit_lw_phys(&ctx, 10, rs * 8, 18);      // a0 = low
                emit_lw_phys(&ctx, 11, rs * 8 + 4, 18);  // a1 = high
                
                // a2:a3 = multiplier (sign-extended imm8)
                emit_addi_phys(&ctx, 12, 0, (int16_t)imm);  // a2 = imm (sign-extended)
                emit_srai_phys(&ctx, 13, 12, 31);  // a3 = sign extension of imm
                
                // Вызываем helper для 64-bit multiplication
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_mul64);
                
                // Результат в a0:a1, сохраняем в v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);      // low
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);  // high
                
                break;
            }
            
            case 0x53: { // DIVS.I64.IMM8 Rd(u8), R1(u8), imm8(i8)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm = (int8_t)*pc++;
                
                // 64-битное деление с immediate делителем
                // Проверяем деление на 0 и overflow во время компиляции
                if (imm == 0) {
                    printf("[JIT ERROR] DIVS.I64.IMM8: Division by zero at compile time\n");
                    *out_code = NULL;
                    *out_size = 0;
                    return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                }
                
                // Оптимизация: для степеней двойки используем арифметический сдвиг
                bool is_power_of_2 = (imm > 0) && ((imm & (imm - 1)) == 0);
                bool is_neg_power_of_2 = (imm < 0) && (((-imm) & ((-imm) - 1)) == 0);
                
                if (is_power_of_2) {
                    // Деление на положительную степень 2: просто арифметический сдвиг вправо
                    int shift = 0;
                    int8_t temp = imm;
                    while (temp > 1) { temp >>= 1; shift++; }
                    
                    // Загружаем значение (64-bit) из v_regs[r1]
                    emit_lw_phys(&ctx, 5, r1 * 8, 18);      // t0 = low
                    emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);  // t1 = high
                    
                    // Для signed division: if (val < 0) val += (divisor - 1)
                    // Проверяем знак (high part)
                    emit_srai_phys(&ctx, 7, 6, 31);  // t2 = sign extension (all 1s if negative)
                    
                    // bias = (1 << shift) - 1
                    int32_t bias = (1 << shift) - 1;
                    if (bias < 2048) {
                        emit_addi_phys(&ctx, 28, 0, bias);  // t3 = bias
                    } else {
                        uint32_t hi = ((bias + 0x800) & 0xFFFFF000);
                        int16_t lo = (int16_t)(bias - hi);
                        emit_lui_phys(&ctx, 28, hi);
                        if (lo != 0) emit_addi_phys(&ctx, 28, 28, lo);
                    }
                    
                    // masked_bias = t2 & t3 (bias if negative, 0 if positive)
                    emit_and_phys(&ctx, 28, 7, 28);  // t3 = masked_bias
                    
                    // Add bias to low part
                    emit_add_phys(&ctx, 5, 5, 28);  // t0 += masked_bias
                    // Carry to high part
                    emit_sltu_phys(&ctx, 29, 5, 28);  // t4 = carry
                    emit_add_phys(&ctx, 6, 6, 29);  // t1 += carry
                    
                    // Арифметический сдвиг вправо (64-bit)
                    if (shift < 32) {
                        // Сдвиг менее 32 бит
                        emit_srli_phys(&ctx, 5, 5, shift);  // low >>= shift
                        emit_slli_phys(&ctx, 29, 6, 32 - shift);  // temp = high << (32-shift)
                        emit_or_phys(&ctx, 5, 5, 29);  // low |= temp
                        emit_srai_phys(&ctx, 6, 6, shift);  // high >>= shift (arithmetic)
                    } else {
                        // Сдвиг >= 32 бит
                        emit_srai_phys(&ctx, 5, 6, shift - 32);  // low = high >> (shift-32)
                        emit_srai_phys(&ctx, 6, 6, 31);  // high = sign extension
                    }
                    
                    // Сохраняем результат
                    emit_sw_phys(&ctx, 5, rd * 8, 18);
                    emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                    
                } else if (is_neg_power_of_2) {
                    // Деление на отрицательную степень 2: сдвиг + инверсия знака
                    int shift = 0;
                    int8_t temp = -imm;
                    while (temp > 1) { temp >>= 1; shift++; }
                    
                    // Аналогично положительной степени, но с инверсией результата
                    // (реализация аналогична выше, но с NEG в конце)
                    // Для краткости используем общий путь через helper
                    goto divs_i64_general_case;
                    
                } else {
divs_i64_general_case:
                    // Общий случай: вызываем helper функцию для 64-bit division
                    // a0:a1 = dividend (from v_regs[r1])
                    emit_lw_phys(&ctx, 10, r1 * 8, 18);
                    emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                    
                    // a2:a3 = divisor (sign-extended imm8)
                    emit_addi_phys(&ctx, 12, 0, (int16_t)imm);
                    if (imm < 0) {
                        emit_addi_phys(&ctx, 13, 0, -1);  // 0xFFFFFFFF
                    } else {
                        emit_addi_phys(&ctx, 13, 0, 0);
                    }
                    
                    // Вызываем helper для 64-bit signed division
                    emit_call_helper(&ctx, (uintptr_t)&jit_divs_i64);
                    
                    // Результат в a0:a1
                    emit_sw_phys(&ctx, 10, rd * 8, 18);
                    emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                }
                
                break;
            }
            
            case 0x54: { // DIVU.I64.IMM8 Rd(u8), R1(u8), imm8(u8) - Unsigned division
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t imm = *pc++;
                
                // Проверка деления на 0
                if (imm == 0) {
                    printf("[JIT ERROR] DIVU.I64.IMM8: Division by zero at compile time\n");
                    *out_code = NULL;
                    *out_size = 0;
                    return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                }
                
                // Оптимизация: для степеней 2 используем логический сдвиг
                bool is_power_of_2 = (imm & (imm - 1)) == 0;
                
                if (is_power_of_2) {
                    // Деление на степень 2: просто логический сдвиг вправо
                    int shift = 0;
                    uint8_t temp = imm;
                    while (temp > 1) { temp >>= 1; shift++; }
                    
                    // Загружаем значение (64-bit) из v_regs[r1]
                    emit_lw_phys(&ctx, 5, r1 * 8, 18);      // t0 = low
                    emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);  // t1 = high
                    
                    // Логический сдвиг вправо (64-bit unsigned)
                    if (shift < 32) {
                        // Сдвиг менее 32 бит
                        emit_srli_phys(&ctx, 5, 5, shift);  // low >>= shift
                        emit_slli_phys(&ctx, 29, 6, 32 - shift);  // temp = high << (32-shift)
                        emit_or_phys(&ctx, 5, 5, 29);  // low |= temp
                        emit_srli_phys(&ctx, 6, 6, shift);  // high >>= shift (logical)
                    } else {
                        // Сдвиг >= 32 бит
                        emit_srli_phys(&ctx, 5, 6, shift - 32);  // low = high >> (shift-32)
                        emit_addi_phys(&ctx, 6, 0, 0);  // high = 0
                    }
                    
                    // Сохраняем результат
                    emit_sw_phys(&ctx, 5, rd * 8, 18);
                    emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                    
                } else {
                    // Общий случай: вызываем helper
                    emit_lw_phys(&ctx, 10, r1 * 8, 18);
                    emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                    
                    // a2:a3 = divisor (zero-extended imm8)
                    emit_addi_phys(&ctx, 12, 0, (uint8_t)imm);
                    emit_addi_phys(&ctx, 13, 0, 0);  // high = 0
                    
                    // Вызываем helper
                    emit_call_helper(&ctx, (uintptr_t)&jit_divu_i64);
                    
                    // Результат в a0:a1
                    emit_sw_phys(&ctx, 10, rd * 8, 18);
                    emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                }
                
                break;
            }
            
            case 0x56: { // REMU.I64.IMM8 Rd(u8), R1(u8), imm8(u8) - Unsigned remainder
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t imm = *pc++;
                
                // Проверка деления на 0
                if (imm == 0) {
                    printf("[JIT ERROR] REMU.I64.IMM8: Division by zero at compile time\n");
                    *out_code = NULL;
                    *out_size = 0;
                    return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                }
                
                // Для остатка используем helper
                // a0:a1 = dividend
                emit_lw_phys(&ctx, 10, r1 * 8, 18);
                emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                
                // a2:a3 = divisor (zero-extended imm8)
                emit_addi_phys(&ctx, 12, 0, (uint8_t)imm);
                emit_addi_phys(&ctx, 13, 0, 0);
                
                // Вызываем helper
                emit_call_helper(&ctx, (uintptr_t)&jit_remu_i64);
                
                // Результат в a0:a1
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                
                break;
            }
            case 0x7A: { // STORE.PTR Rs(u8), Ra(u8), offset(i16)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Сохраняем указатель (PTR) из v_regs[rs] в память по адресу v_regs[ra] + offset
                // Загружаем адрес из v_regs[ra]
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (base address)
                
                // Загружаем значение указателя из v_regs[rs]
                emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs] (pointer value)
                
                // Вычисляем эффективный адрес и сохраняем
                if (offset >= -2048 && offset < 2048) {
                    // SW t1, offset(t0)
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF);
                    uint32_t imm_11_5 = (imm_bits >> 5) << 25;
                    uint32_t imm_4_0 = (imm_bits & 0x1F) << 7;
                    emit_instr(&ctx, imm_11_5 | (6 << 20) | (5 << 15) | (0b010 << 12) | imm_4_0 | 0b0100011);
                } else {
                    // Для больших offset используем временный регистр
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        emit_sub_phys(&ctx, 28, 0, 28);  // NEG t3
                    }
                    emit_add_phys(&ctx, 28, 5, 28);  // t3 = t0 + offset
                    // SW t1, 0(t3)
                    emit_instr(&ctx, (6 << 20) | (28 << 15) | (0b010 << 12) | 0b0100011);
                }
                
                break;
            }
            
            case 0x80: { // LOAD.I8 Rd(u8), Ra(u8), offset(i16)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // NO_SPILL fast-path: get address from physical register if in range
                uint8_t addr_reg = 5;  // default: use t0
                if (no_spill_fastpath && i32_only) {
                    uint8_t pa = FP_MAP_VREG(ra);
                    if (pa != 0) {
                        addr_reg = pa;  // use physical register directly
                    } else {
                        // ra out of range, load from memory
                        emit_lw_phys(&ctx, 5, ra * 8, 18);
                    }
                } else {
                    // Standard path: load address from memory
                    emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (address)
                }
                
                // Загружаем байт со знаковым расширением: LB (Load Byte signed)
                // LB rd, offset(rs1): opcode=0000011, funct3=000
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
                    emit_instr(&ctx, imm_bits | (addr_reg << 15) | (0b000 << 12) | (6 << 7) | 0b0000011);  // lb t1, offset(addr_reg)
                } else {
                    // Для больших offset используем временный регистр
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        // NEG t3, t3
                        emit_sub_phys(&ctx, 28, 0, 28);
                    }
                    emit_add_phys(&ctx, 28, addr_reg, 28);  // t3 = addr_reg + offset
                    // LB t1, 0(t3)
                    emit_instr(&ctx, (0 << 20) | (28 << 15) | (0b000 << 12) | (6 << 7) | 0b0000011);
                }
                
                // NO_SPILL fast-path: store result to physical register if in range
                if (no_spill_fastpath && i32_only) {
                    uint8_t pd = FP_MAP_VREG(rd);
                    if (pd != 0) {
                        emit_addi_phys(&ctx, pd, 6, 0);  // mv pd, t1
                        break;
                    }
                }
                
                // Standard path: store to memory
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                // Sign extend to 64-bit: заполняем старшую часть в зависимости знака
                emit_srai_phys(&ctx, 7, 6, 31);  // t2 = t1 >> 31 (sign extension)
                emit_sw_phys(&ctx, 7, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x81: { // LOAD.I8U Rd(u8), Ra(u8), offset(i16) - Load unsigned byte
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Загружаем адрес из v_regs[ra]
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (address)
                
                // Загружаем байт с нулевым расширением: LBU (Load Byte Unsigned)
                // LBU rd, offset(rs1): opcode=0000011, funct3=100
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
                    emit_instr(&ctx, imm_bits | (5 << 15) | (0b100 << 12) | (6 << 7) | 0b0000011);  // lbu t1, offset(t0)
                } else {
                    // Для больших offset
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        emit_sub_phys(&ctx, 28, 0, 28);
                    }
                    emit_add_phys(&ctx, 28, 5, 28);  // t3 = t0 + offset
                    emit_instr(&ctx, (0 << 20) | (28 << 15) | (0b100 << 12) | (6 << 7) | 0b0000011);  // lbu t1, 0(t3)
                }
                
                // Сохраняем результат (zero-extended byte -> U32)
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                // Zero extend to 64-bit: старшая часть = 0
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);  // x0 = 0
                
                break;
            }
            
            case 0x82: { // LOAD.I16S Rd(u8), Ra(u8), offset(i16) - Load signed halfword
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // NO_SPILL fast-path: get address from physical register if in range
                uint8_t addr_reg = 5;  // default: use t0
                if (no_spill_fastpath && i32_only) {
                    uint8_t pa = FP_MAP_VREG(ra);
                    if (pa != 0) {
                        addr_reg = pa;  // use physical register directly
                    } else {
                        emit_lw_phys(&ctx, 5, ra * 8, 18);
                    }
                } else {
                    emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (address)
                }
                
                // Загружаем halfword со знаковым расширением: LH (Load Halfword signed)
                // LH rd, offset(rs1): opcode=0000011, funct3=001
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
                    emit_instr(&ctx, imm_bits | (addr_reg << 15) | (0b001 << 12) | (6 << 7) | 0b0000011);  // lh t1, offset(addr_reg)
                } else {
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        emit_sub_phys(&ctx, 28, 0, 28);
                    }
                    emit_add_phys(&ctx, 28, addr_reg, 28);  // t3 = addr_reg + offset
                    emit_instr(&ctx, (0 << 20) | (28 << 15) | (0b001 << 12) | (6 << 7) | 0b0000011);  // lh t1, 0(t3)
                }
                
                // NO_SPILL fast-path: store result to physical register if in range
                if (no_spill_fastpath && i32_only) {
                    uint8_t pd = FP_MAP_VREG(rd);
                    if (pd != 0) {
                        emit_addi_phys(&ctx, pd, 6, 0);  // mv pd, t1
                        break;
                    }
                }
                
                // Standard path: store to memory
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                // Sign extend to 64-bit
                emit_srai_phys(&ctx, 7, 6, 31);  // t2 = t1 >> 31 (sign extension)
                emit_sw_phys(&ctx, 7, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x83: { // LOAD.U16 Rd(u8), Ra(u8), offset(i16) - Load unsigned halfword
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Загружаем адрес из v_regs[ra]
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (address)
                
                // Загружаем halfword с нулевым расширением: LHU (Load Halfword Unsigned)
                // LHU rd, offset(rs1): opcode=0000011, funct3=101
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
                    emit_instr(&ctx, imm_bits | (5 << 15) | (0b101 << 12) | (6 << 7) | 0b0000011);  // lhu t1, offset(t0)
                } else {
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        emit_sub_phys(&ctx, 28, 0, 28);
                    }
                    emit_add_phys(&ctx, 28, 5, 28);  // t3 = t0 + offset
                    emit_instr(&ctx, (0 << 20) | (28 << 15) | (0b101 << 12) | (6 << 7) | 0b0000011);  // lhu t1, 0(t3)
                }
                
                // Сохраняем результат (zero-extended halfword -> U32)
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                // Zero extend to 64-bit
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);  // x0 = 0
                
                break;
            }
            
            case 0x89: { // LOAD.BOOL Rd(u8), Ra(u8), offset(i16) - Load boolean (byte with zero-extension)
                uint8_t rd = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // Загружаем адрес из v_regs[ra]
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (address)
                
                // Загружаем байт с нулевым расширением: LBU
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF) << 20;
                    emit_instr(&ctx, imm_bits | (5 << 15) | (0b100 << 12) | (6 << 7) | 0b0000011);  // lbu t1, offset(t0)
                } else {
                    uint32_t abs_off = (offset < 0) ? -offset : offset;
                    emit_lui_phys(&ctx, 28, (abs_off + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, (int16_t)(abs_off & 0xFFF));
                    if (offset < 0) {
                        emit_sub_phys(&ctx, 28, 0, 28);
                    }
                    emit_add_phys(&ctx, 28, 5, 28);
                    emit_instr(&ctx, (0 << 20) | (28 << 15) | (0b100 << 12) | (6 << 7) | 0b0000011);
                }
                
                // Normalize to boolean (0 or 1): SNEZ t1, t1
                emit_instr(&ctx, (0 << 25) | (6 << 20) | (0 << 15) | (0b011 << 12) | (6 << 7) | 0b0110011);
                
                // Сохраняем результат
                emit_sw_phys(&ctx, 6, rd * 8, 18);
                
                // Zero extend to 64-bit
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x70: { // STORE.I8 Rs(u8), Ra(u8), offset(i16)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // NO_SPILL fast-path: get address and value from physical registers if in range
                uint8_t addr_reg = 5;  // default: use t0
                uint8_t val_reg = 6;   // default: use t1
                
                if (no_spill_fastpath && i32_only) {
                    uint8_t pa = FP_MAP_VREG(ra);
                    uint8_t ps = FP_MAP_VREG(rs);
                    if (pa != 0) {
                        addr_reg = pa;
                    } else {
                        emit_lw_phys(&ctx, 5, ra * 8, 18);
                    }
                    if (ps != 0) {
                        val_reg = ps;
                    } else {
                        emit_lw_phys(&ctx, 6, rs * 8, 18);
                    }
                } else {
                    emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (base address)
                    emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs] (value)
                }
                
                // Сохраняем байт (SB - Store Byte)
                if (offset >= -2048 && offset < 2048) {
                    // SB val_reg, offset(addr_reg)
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF);
                    uint32_t imm_11_5 = (imm_bits >> 5) << 25;
                    uint32_t imm_4_0 = (imm_bits & 0x1F) << 7;
                    emit_instr(&ctx, imm_11_5 | (val_reg << 20) | (addr_reg << 15) | (0b000 << 12) | imm_4_0 | 0b0100011);
                } else {
                    // Для больших offset
                    emit_lui_phys(&ctx, 28, (offset + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, offset & 0xFFF);
                    emit_add_phys(&ctx, 28, addr_reg, 28);
                    emit_instr(&ctx, (val_reg << 20) | (28 << 15) | (0b000 << 12) | 0b0100011);
                }
                
                break;
            }
            
            case 0x71: { // STORE.U8 Rs(u8), Ra(u8), offset(i16)
                // U8 and I8 are identical at the byte level - same implementation as 0x70
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (base address)
                emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs] (value)
                
                // SB - Store Byte (unsigned same as signed at byte level)
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF);
                    uint32_t imm_11_5 = (imm_bits >> 5) << 25;
                    uint32_t imm_4_0 = (imm_bits & 0x1F) << 7;
                    emit_instr(&ctx, imm_11_5 | (6 << 20) | (5 << 15) | (0b000 << 12) | imm_4_0 | 0b0100011);
                } else {
                    emit_lui_phys(&ctx, 28, (offset + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, offset & 0xFFF);
                    emit_add_phys(&ctx, 28, 5, 28);
                    emit_instr(&ctx, (6 << 20) | (28 << 15) | (0b000 << 12) | 0b0100011);
                }
                
                break;
            }
            
            case 0x72: // STORE.I16 Rs(u8), Ra(u8), offset(i16)
            case 0x73: { // STORE.U16 Rs(u8), Ra(u8), offset(i16)
                // I16 and U16 are identical at the halfword level
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                // NO_SPILL fast-path: get address and value from physical registers if in range
                uint8_t addr_reg = 5;  // default: use t0
                uint8_t val_reg = 6;   // default: use t1
                
                if (no_spill_fastpath && i32_only) {
                    uint8_t pa = FP_MAP_VREG(ra);
                    uint8_t ps = FP_MAP_VREG(rs);
                    if (pa != 0) {
                        addr_reg = pa;
                    } else {
                        emit_lw_phys(&ctx, 5, ra * 8, 18);
                    }
                    if (ps != 0) {
                        val_reg = ps;
                    } else {
                        emit_lw_phys(&ctx, 6, rs * 8, 18);
                    }
                } else {
                    emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (base address)
                    emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs] (value)
                }
                
                // SH - Store Halfword (16-bit, funct3=0b001)
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF);
                    uint32_t imm_11_5 = (imm_bits >> 5) << 25;
                    uint32_t imm_4_0 = (imm_bits & 0x1F) << 7;
                    emit_instr(&ctx, imm_11_5 | (val_reg << 20) | (addr_reg << 15) | (0b001 << 12) | imm_4_0 | 0b0100011);
                } else {
                    emit_lui_phys(&ctx, 28, (offset + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, offset & 0xFFF);
                    emit_add_phys(&ctx, 28, addr_reg, 28);
                    emit_instr(&ctx, (val_reg << 20) | (28 << 15) | (0b001 << 12) | 0b0100011);
                }
                
                break;
            }
            
            case 0x7B: { // STORE.BOOL Rs(u8), Ra(u8), offset(i16)
                // BOOL stored as byte (0 or 1)
                uint8_t rs = *pc++;
                uint8_t ra = *pc++;
                int16_t offset;
                memcpy(&offset, pc, sizeof(offset)); pc += sizeof(offset);
                
                emit_lw_phys(&ctx, 5, ra * 8, 18);  // t0 = v_regs[ra] (base address)
                emit_lw_phys(&ctx, 6, rs * 8, 18);  // t1 = v_regs[rs] (value)
                
                // Convert to boolean: t1 = (t1 != 0) ? 1 : 0
                // SNEZ rd, rs is pseudo-instruction for SLTU rd, x0, rs
                emit_instr(&ctx, (0 << 25) | (6 << 20) | (0 << 15) | (0b011 << 12) | (6 << 7) | 0b0110011);
                
                // SB - Store Byte
                if (offset >= -2048 && offset < 2048) {
                    uint32_t imm_bits = ((uint32_t)offset & 0xFFF);
                    uint32_t imm_11_5 = (imm_bits >> 5) << 25;
                    uint32_t imm_4_0 = (imm_bits & 0x1F) << 7;
                    emit_instr(&ctx, imm_11_5 | (6 << 20) | (5 << 15) | (0b000 << 12) | imm_4_0 | 0b0100011);
                } else {
                    emit_lui_phys(&ctx, 28, (offset + 0x800) & 0xFFFFF000);
                    emit_addi_phys(&ctx, 28, 28, offset & 0xFFF);
                    emit_add_phys(&ctx, 28, 5, 28);
                    emit_instr(&ctx, (6 << 20) | (28 << 15) | (0b000 << 12) | 0b0100011);
                }
                
                break;
            }
            
            case 0x55: { // REMS.I64.IMM8 Rd(u8), R1(u8), imm8(i8) - Signed remainder
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                int8_t imm = (int8_t)*pc++;
                
                // Проверка деления на 0
                if (imm == 0) {
                    printf("[JIT ERROR] REMS.I64.IMM8: Division by zero at compile time\n");
                    *out_code = NULL;
                    *out_size = 0;
                    return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                }
                
                // Для остатка нет простых оптимизаций со сдвигами, используем helper
                // a0:a1 = dividend
                emit_lw_phys(&ctx, 10, r1 * 8, 18);
                emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                
                // a2:a3 = divisor (sign-extended imm8)
                emit_addi_phys(&ctx, 12, 0, (int16_t)imm);
                if (imm < 0) {
                    emit_addi_phys(&ctx, 13, 0, -1);  // 0xFFFFFFFF
                } else {
                    emit_addi_phys(&ctx, 13, 0, 0);
                }
                
                // Вызываем helper
                emit_call_helper(&ctx, (uintptr_t)&jit_rems_i64);
                
                // Результат в a0:a1
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0xFC: { // PREFIX: Extended Ops
                // Читаем второй байт - реальный опкод
                uint8_t ext_opcode = *pc++;
                
                switch (ext_opcode) {
                    case 0x00: { // MEMORY.INIT data_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
                        uint32_t data_seg_idx;
                        memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);
                        uint8_t rd = *pc++;  // destination address
                        uint8_t rs = *pc++;  // source offset in data segment
                        uint8_t rn = *pc++;  // count bytes
                        
                        // memory.init - copy from data segment to memory
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = data_seg_idx
                        if (data_seg_idx < 2048) {
                            emit_addi_phys(&ctx, 11, 0, data_seg_idx);
                        } else {
                            uint32_t hi = ((data_seg_idx + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(data_seg_idx - hi);
                            emit_lui_phys(&ctx, 11, hi);
                            if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                        }
                        
                        // a2 = dst_addr from v_regs[rd]
                        emit_lw_phys(&ctx, 12, rd * 8, 18);
                        
                        // a3 = src_offset from v_regs[rs]
                        emit_lw_phys(&ctx, 13, rs * 8, 18);
                        
                        // a4 = count from v_regs[rn]
                        emit_lw_phys(&ctx, 14, rn * 8, 18);
                        
                        // Call helper
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_memory_init);
                        
                        // Check result (a0), if non-zero = error, could handle here
                        // For now, assume success
                        
                        break;
                    }
                    
                    case 0x01: { // DATA.DROP data_seg_idx(u32)
                        uint32_t data_seg_idx;
                        memcpy(&data_seg_idx, pc, sizeof(data_seg_idx)); pc += sizeof(data_seg_idx);
                        
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = data_seg_idx
                        if (data_seg_idx < 2048) {
                            emit_addi_phys(&ctx, 11, 0, data_seg_idx);
                        } else {
                            uint32_t hi = ((data_seg_idx + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(data_seg_idx - hi);
                            emit_lui_phys(&ctx, 11, hi);
                            if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                        }
                        
                        // Call helper
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_data_drop);
                        
                        break;
                    }
                    
                    case 0x05: { // ELEM.DROP elem_seg_idx(u32)
                        // В интерпретаторе это логически "удаляет" сегмент элементов.
                        // Для текущей реализации (без строгого контроля жизненного цикла сегментов)
                        // делаем no-op, но корректно считываем операнд.
                        uint32_t elem_seg_idx;
                        memcpy(&elem_seg_idx, pc, sizeof(elem_seg_idx)); pc += sizeof(elem_seg_idx);
                        (void)elem_seg_idx;
                        break;
                    }
                    
                    case 0x04: { // TABLE.INIT table_idx(u8), elem_seg_idx(u32), Rd(u8), Rs(u8), Rn(u8)
                        uint8_t table_idx = *pc++;
                        uint32_t elem_seg_idx;
                        memcpy(&elem_seg_idx, pc, sizeof(elem_seg_idx)); pc += sizeof(elem_seg_idx);
                        uint8_t rd = *pc++;  // destination index
                        uint8_t rs = *pc++;  // source offset
                        uint8_t rn = *pc++;  // count
                        
                        // table.init - копирование элементов из пассивного сегмента в таблицу
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = table_idx
                        emit_addi_phys(&ctx, 11, 0, table_idx);
                        
                        // a2 = elem_seg_idx
                        if (elem_seg_idx < 2048) {
                            emit_addi_phys(&ctx, 12, 0, elem_seg_idx);
                        } else {
                            uint32_t hi = ((elem_seg_idx + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(elem_seg_idx - hi);
                            emit_lui_phys(&ctx, 12, hi);
                            if (lo != 0) emit_addi_phys(&ctx, 12, 12, lo);
                        }
                        
                        // a3 = dst_index из v_regs[rd]
                        emit_lw_phys(&ctx, 13, rd * 8, 18);
                        
                        // a4 = src_offset из v_regs[rs]
                        emit_lw_phys(&ctx, 14, rs * 8, 18);
                        
                        // a5 = count из v_regs[rn]
                        emit_lw_phys(&ctx, 15, rn * 8, 18);
                        
                        // Вызываем helper
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_init);
                        
                        break;
                    }
                    
                    case 0x03: { // MEMORY.FILL Rd(u8), Rval(u8), Rn(u8)
                        uint8_t rd = *pc++;   // destination address
                        uint8_t rval = *pc++; // fill value (byte)
                        uint8_t rn = *pc++;   // size in bytes
                        
                        // memset(dst, val, size)
                        // a0 = dst из v_regs[rd]
                        emit_lw_phys(&ctx, 10, rd * 8, 18);
                        
                        // a1 = val из v_regs[rval] (только младший байт)
                        emit_lw_phys(&ctx, 11, rval * 8, 18);
                        
                        // a2 = size из v_regs[rn]
                        emit_lw_phys(&ctx, 12, rn * 8, 18);
                        
                        // Вызываем memset
                        emit_call_helper(&ctx, (uintptr_t)&memset);
                        
                        break;
                    }
                    
                    case 0x02: { // MEMORY.COPY Rd(u8), Rs(u8), Rn(u8)
                        uint8_t rd = *pc++;  // destination address
                        uint8_t rs = *pc++;  // source address
                        uint8_t rn = *pc++;  // size in bytes
                        
                        // memcpy(dst, src, size)
                        // a0 = dst из v_regs[rd]
                        emit_lw_phys(&ctx, 10, rd * 8, 18);
                        
                        // a1 = src из v_regs[rs]
                        emit_lw_phys(&ctx, 11, rs * 8, 18);
                        
                        // a2 = size из v_regs[rn]
                        emit_lw_phys(&ctx, 12, rn * 8, 18);
                        
                        // Вызываем memcpy
                        emit_call_helper(&ctx, (uintptr_t)&memcpy);
                        
                        break;
                    }
                    
                    case 0x06: { // HEAP_REALLOC Rd(u8), Rptr(u8), Rsize(u8)
                        uint8_t rd = *pc++;
                        uint8_t rptr = *pc++;
                        uint8_t rsize = *pc++;
                        
                        // a0 = instance (s1 / x9)
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = old_ptr from v_regs[rptr]
                        emit_lw_phys(&ctx, 11, rptr * 8, 18);
                        
                        // a2 = new_size from v_regs[rsize]
                        emit_lw_phys(&ctx, 12, rsize * 8, 18);
                        
                        emit_call_helper(&ctx, (uintptr_t)&espb_heap_realloc);
                        
                        // Store result pointer
                        emit_sw_phys(&ctx, 10, rd * 8, 18);
                        emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                        
                        break;
                    }
                    
                    case 0x09: { // HEAP_CALLOC Rd(u8), Rcount(u8), Rsize(u8)
                        uint8_t rd = *pc++;
                        uint8_t rcount = *pc++;
                        uint8_t rsize = *pc++;
                        
                        // Calculate total size: num * size (with overflow check)
                        // Load num and size
                        emit_lw_phys(&ctx, 5, rcount * 8, 18);  // t0 = num
                        emit_lw_phys(&ctx, 6, rsize * 8, 18);   // t1 = size
                        
                        // mul t2, t0, t1 (total = num * size)
                        emit_instr(&ctx, (0x01 << 25) | (6 << 20) | (5 << 15) | (0x0 << 12) | (7 << 7) | 0b0110011);
                        
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = total size
                        emit_addi_phys(&ctx, 11, 7, 0);
                        
                        // Call espb_heap_malloc
                        emit_call_helper(&ctx, (uintptr_t)&espb_heap_malloc);
                        
                        // Now we need to zero the memory: memset(ptr, 0, total)
                        // Save pointer
                        emit_addi_phys(&ctx, 28, 10, 0);  // t3 = ptr
                        
                        // a0 = ptr (already in t3, move to a0)
                        emit_addi_phys(&ctx, 10, 28, 0);
                        
                        // a1 = 0 (value to fill)
                        emit_addi_phys(&ctx, 11, 0, 0);
                        
                        // a2 = total size (still in t2/x7)
                        emit_addi_phys(&ctx, 12, 7, 0);
                        
                        // Call memset
                        emit_call_helper(&ctx, (uintptr_t)&memset);
                        
                        // Result pointer is in t3, store it
                        emit_sw_phys(&ctx, 28, rd * 8, 18);
                        emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                        
                        break;
                    }
                    
                    case 0x16: { // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
                        uint8_t dst_table_idx = *pc++;
                        uint8_t src_table_idx = *pc++;
                        uint8_t rd = *pc++;  // destination offset reg
                        uint8_t rs = *pc++;  // source offset reg
                        uint8_t rn = *pc++;  // count reg
                        
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        // a1 = dst_table_idx
                        emit_addi_phys(&ctx, 11, 0, dst_table_idx);
                        // a2 = src_table_idx
                        emit_addi_phys(&ctx, 12, 0, src_table_idx);
                        // a3 = dst_offset
                        emit_lw_phys(&ctx, 13, rd * 8, 18);
                        // a4 = src_offset
                        emit_lw_phys(&ctx, 14, rs * 8, 18);
                        // a5 = count
                        emit_lw_phys(&ctx, 15, rn * 8, 18);
                        
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_copy);
                        break;
                    }
                    
                    case 0x17: { // TABLE.FILL table_idx(u8), Rd(u8), Rval(u8), Rn(u8)
                        uint8_t table_idx = *pc++;
                        uint8_t rd = *pc++;    // start index
                        uint8_t rval = *pc++;  // fill value
                        uint8_t rn = *pc++;    // count
                        
                        // table.fill - заполнить диапазон таблицы одним значением
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = table_idx
                        emit_addi_phys(&ctx, 11, 0, table_idx);
                        
                        // a2 = start_index из v_regs[rd]
                        emit_lw_phys(&ctx, 12, rd * 8, 18);
                        
                        // a3 = fill_value из v_regs[rval]
                        emit_lw_phys(&ctx, 13, rval * 8, 18);
                        
                        // a4 = count из v_regs[rn]
                        emit_lw_phys(&ctx, 14, rn * 8, 18);
                        
                        // Вызываем helper
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_fill);
                        
                        break;
                    }
                    
                    case 0x08: { // TABLE.SIZE Rd(u8), table_idx(u8)
                        uint8_t rd = *pc++;
                        uint8_t table_idx = *pc++;
                        
                        (void)table_idx; // В текущей реализации используется одна таблица
                        
                        // Rd = instance->table_size
                        // Находим offset поля table_size в EspbInstance (нужно определить через sizeof)
                        // Предположим table_size находится по offset (будет уточнено)
                        // Загружаем напрямую из instance->table_size
                        
                        // Для простоты используем helper
                        emit_addi_phys(&ctx, 10, 9, 0);  // a0 = instance
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_size);
                        
                        // Результат в a0
                        emit_sw_phys(&ctx, 10, rd * 8, 18);
                        emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                        
                        break;
                    }
                    
                    case 0x07: { // HEAP_FREE Rp(u8)
                        uint8_t rp = *pc++;
                        
                        // espb_heap_free(instance, ptr)
                        // a0 = instance (s1/x9)
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = ptr из v_regs[rp]
                        emit_lw_phys(&ctx, 11, rp * 8, 18);
                        
                        // Вызываем espb_heap_free(instance, ptr)
                        emit_call_helper(&ctx, (uintptr_t)&espb_heap_free);
                        
                        break;
                    }
                    
                    case 0x18: { // TABLE.GET Rd(u8), table_idx(u8), Rs(u8)
                        uint8_t rd = *pc++;
                        uint8_t table_idx = *pc++;
                        uint8_t rs = *pc++;
                        
                        // Rd = table[Rs]
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = table_idx
                        emit_addi_phys(&ctx, 11, 0, table_idx);
                        
                        // a2 = index из v_regs[rs]
                        emit_lw_phys(&ctx, 12, rs * 8, 18);
                        
                        // Вызываем helper
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_get);
                        
                        // Результат (void*) в a0, сохраняем как I32 в v_regs[rd]
                        emit_sw_phys(&ctx, 10, rd * 8, 18);
                        emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                        
                        break;
                    }
                    
                    case 0x19: { // TABLE.SET table_idx(u8), Rd(u8), Rval(u8)
                        uint8_t table_idx = *pc++;
                        uint8_t rd = *pc++;   // index в таблицу
                        uint8_t rval = *pc++; // значение для записи
                        
                        // table[Rd] = Rval
                        // a0 = instance
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = table_idx
                        emit_addi_phys(&ctx, 11, 0, table_idx);
                        
                        // a2 = index из v_regs[rd]
                        emit_lw_phys(&ctx, 12, rd * 8, 18);
                        
                        // a3 = value из v_regs[rval]
                        emit_lw_phys(&ctx, 13, rval * 8, 18);
                        
                        // Вызываем helper функцию для установки элемента в таблицу
                        // Нужна helper функция: espb_table_set(instance, table_idx, index, value)
                        emit_call_helper(&ctx, (uintptr_t)&jit_helper_table_set);
                        
                        break;
                    }
                    
                    case 0x0B: { // HEAP_MALLOC Rd(u8), Rs(u8)
                        uint8_t rd = *pc++;
                        uint8_t rs = *pc++;
                        
                        // Rd(PTR) = espb_heap_malloc(instance, Rs(I32))
                        // a0 = instance (s1/x9)
                        emit_addi_phys(&ctx, 10, 9, 0);
                        
                        // a1 = size из v_regs[rs]
                        emit_lw_phys(&ctx, 11, rs * 8, 18);
                        
                        // Вызываем espb_heap_malloc(instance, size)
                        emit_call_helper(&ctx, (uintptr_t)&espb_heap_malloc);
                        
                        // Результат (указатель) в a0, сохраняем в v_regs[rd]
                        emit_sw_phys(&ctx, 10, rd * 8, 18);
                        emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);  // high = 0
                        
                        break;
                    }
                    
                    default:
                        printf("[JIT] Extended opcode 0xFC 0x%02X not yet implemented\n", ext_opcode);
                        *out_code = NULL;
                        *out_size = 0;
                        return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
                }
                
                break;
            }
            
            case 0x12: { // MOV.32 Rd, Rs
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;

                // NO_SPILL fast-path: use physical register move if both in range
                if (no_spill_fastpath && i32_only) {
                    uint8_t pd = FP_MAP_VREG(rd);
                    uint8_t ps = FP_MAP_VREG(rs);
                    
                    if (pd != 0) {
                        // Destination is in physical register range
                        if (ps != 0) {
                            // Both in range - direct register-to-register move
                            emit_addi_phys(&ctx, pd, ps, 0);  // mv pd, ps
                        } else {
                            // Source out of range - load from memory to physical register
                            emit_lw_phys(&ctx, pd, rs * 8, 18);
                        }
                        break;
                    }
                    // rd out of range - fall through to memory path
                }

                // IMPORTANT: interpreter MOV copies the whole Value (8 bytes).
                // LLVM/translator may emit MOV.32 even when register holds I64/U64/F64.
                // So we copy full 8 bytes to avoid losing high word.

                emit_lw_phys(&ctx, 5, rs * 8, 18);
                emit_sw_phys(&ctx, 5, rd * 8, 18);

                emit_lw_phys(&ctx, 5, rs * 8 + 4, 18);
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);

                break;
            }
            
            case 0x13: { // MOV.64 Rd, Rs (64-bit move для I64, U64, F64)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                // Загружаем оба 32-bit слова (Value это 64-bit union)
                // lw t0, rs*8(s2)     -- младшие 32 бита
                emit_lw_phys(&ctx, 5, rs * 8, 18);
                // lw t1, rs*8+4(s2)   -- старшие 32 бита
                emit_lw_phys(&ctx, 6, rs * 8 + 4, 18);
                
                // Сохраняем в v_regs[rd]
                // sw t0, rd*8(s2)
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                // sw t1, rd*8+4(s2)
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x16: { // LDC.I16.IMM Rd, imm16
                uint8_t rd = *pc++;
                int16_t imm;
                memcpy(&imm, pc, sizeof(imm)); pc += sizeof(imm);
                
                // Загружаем константу в t0
                emit_addi_phys(&ctx, 5, 0, imm);
                
                // Сохраняем в v_regs[rd]
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                break;
            }
            
            case 0x18: { // LDC.I32.IMM Rd, imm32
                if (no_spill_fastpath) {
                    uint8_t rd = *pc++;
                    int32_t imm;
                    memcpy(&imm, pc, sizeof(imm)); pc += sizeof(imm);
                    
                    uint8_t phys = FP_MAP_VREG(rd);
                    if (phys != 0) {
                        // rd in NO_SPILL range - load to physical register
                        if (imm >= -2048 && imm < 2048) {
                            emit_addi_phys(&ctx, phys, 0, (int16_t)imm);
                        } else {
                            uint32_t hi = (((uint32_t)imm + 0x800) & 0xFFFFF000);
                            int16_t lo = (int16_t)(imm - (int32_t)hi);
                            emit_lui_phys(&ctx, phys, hi);
                            if (lo != 0) emit_addi_phys(&ctx, phys, phys, lo);
                        }
                        break;
                    }
                    // rd out of range - fall through to slow path (writes to memory)
                    pc -= 5; // rewind to re-read rd and imm
                }
                uint8_t rd = *pc++;
                int32_t imm;
                memcpy(&imm, pc, sizeof(imm)); pc += sizeof(imm);
                
                // ИСПРАВЛЕНИЕ: ВСЕГДА загружаем в временный регистр и ВСЕГДА записываем в память
                // Это нужно потому что некоторые инструкции (например ALLOCA) читают из v_regs[]
                // напрямую, а не используют физические регистры
                
                // Загружаем 32-bit константу в t0
                if (imm >= -2048 && imm < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)imm);
                } else {
                    uint32_t hi = (((uint32_t)imm + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(imm - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                
                // ВСЕГДА сохраняем в v_regs[rd] (даже если есть физический регистр)
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                // Если есть физический регистр - копируем туда тоже
                uint8_t phys_rd = map_vreg_to_phys(rd);
                if (phys_rd != 0) {
                    emit_addi_phys(&ctx, phys_rd, 5, 0);  // mv phys_rd, t0
                }
                
                break;
            }
            
            case 0x19: { // LDC.I64.IMM Rd, imm64
                uint8_t rd = *pc++;
                int64_t imm;
                memcpy(&imm, pc, sizeof(imm)); pc += sizeof(imm);
                
                // Загружаем младшие 32 бита
                int32_t lo32 = (int32_t)(imm & 0xFFFFFFFF);
                if (lo32 >= -2048 && lo32 < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)lo32);
                } else {
                    uint32_t hi = (((uint32_t)lo32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(lo32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                // Загружаем старшие 32 бита
                int32_t hi32 = (int32_t)(imm >> 32);
                if (hi32 >= -2048 && hi32 < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)hi32);
                } else {
                    uint32_t hi = (((uint32_t)hi32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(hi32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x1A: { // LDC.F32.IMM Rd, imm32 (float)
                uint8_t rd = *pc++;
                float fimm;
                memcpy(&fimm, pc, sizeof(fimm)); pc += sizeof(fimm);
                
                // Интерпретируем float как int32 для загрузки
                int32_t imm;
                memcpy(&imm, &fimm, sizeof(imm));
                
                if (imm >= -2048 && imm < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)imm);
                } else {
                    uint32_t hi = (((uint32_t)imm + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(imm - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                
                // Сохраняем в v_regs[rd]
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                break;
            }
            
            case 0x1B: { // LDC.F64.IMM Rd, imm64 (double)
                uint8_t rd = *pc++;
                double fimm64;
                memcpy(&fimm64, pc, sizeof(fimm64)); pc += sizeof(fimm64);
                
                // Интерпретируем double как int64 для загрузки
                int64_t imm64;
                memcpy(&imm64, &fimm64, sizeof(imm64));
                
                // Загружаем младшие 32 бита
                int32_t lo32 = (int32_t)(imm64 & 0xFFFFFFFF);
                if (lo32 >= -2048 && lo32 < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)lo32);
                } else {
                    uint32_t hi = (((uint32_t)lo32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(lo32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                // Загружаем старшие 32 бита
                int32_t hi32 = (int32_t)(imm64 >> 32);
                if (hi32 >= -2048 && hi32 < 2048) {
                    emit_addi_phys(&ctx, 5, 0, (int16_t)hi32);
                } else {
                    uint32_t hi = (((uint32_t)hi32 + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(hi32 - (int32_t)hi);
                    emit_lui_phys(&ctx, 5, hi);
                    if (lo != 0) {
                        emit_addi_phys(&ctx, 5, 5, lo);
                    }
                }
                emit_sw_phys(&ctx, 5, rd * 8 + 4, 18);
                
                break;
            }
            
            case 0x1C: { // LDC.PTR.IMM Rd, ptr32
                uint8_t rd = *pc++;
                uint32_t ptr;
                memcpy(&ptr, pc, sizeof(ptr)); pc += sizeof(ptr);
                
                // Загружаем указатель как 32-bit значение
                uint32_t hi = ((ptr + 0x800) & 0xFFFFF000);
                int16_t lo = (int16_t)(ptr - hi);
                emit_lui_phys(&ctx, 5, hi);
                if (lo != 0) {
                    emit_addi_phys(&ctx, 5, 5, lo);
                }
                
                // Сохраняем в v_regs[rd]
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                
                break;
            }
            
            case 0x1D: { // LD_GLOBAL_ADDR Rd(u8), symbol_idx(u16)
                uint8_t rd = *pc++;
                uint16_t symbol_idx;
                memcpy(&symbol_idx, pc, sizeof(symbol_idx)); pc += sizeof(symbol_idx);

                // a0 = instance
                emit_addi_phys(&ctx, 10, 9, 0);
                // a1 = symbol_idx
                if (symbol_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)symbol_idx);
                } else {
                    uint32_t hi = ((symbol_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(symbol_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }
                // a2 = v_regs
                emit_addi_phys(&ctx, 12, 18, 0);
                // a3 = num_virtual_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 13, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 13, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 13, 13, lo);
                }
                // a4 = rd
                emit_addi_phys(&ctx, 14, 0, (int16_t)rd);

                emit_call_helper(&ctx, (uintptr_t)&espb_jit_ld_global_addr);
                break;
            }

            case 0x1E: { // LD_GLOBAL Rd(u8), global_idx(u16)
                uint8_t rd = *pc++;
                uint16_t global_idx;
                memcpy(&global_idx, pc, sizeof(global_idx)); pc += sizeof(global_idx);

                // a0 = instance
                emit_addi_phys(&ctx, 10, 9, 0);
                // a1 = global_idx
                if (global_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)global_idx);
                } else {
                    uint32_t hi = ((global_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(global_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }
                // a2 = v_regs
                emit_addi_phys(&ctx, 12, 18, 0);
                // a3 = num_virtual_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 13, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 13, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 13, 13, lo);
                }
                // a4 = rd
                emit_addi_phys(&ctx, 14, 0, (int16_t)rd);

                emit_call_helper(&ctx, (uintptr_t)&espb_jit_ld_global);
                break;
            }

            case 0x1F: { // ST_GLOBAL global_idx(u16), Rs(u8)
                uint16_t global_idx;
                memcpy(&global_idx, pc, sizeof(global_idx)); pc += sizeof(global_idx);
                uint8_t rs = *pc++;

                // a0 = instance
                emit_addi_phys(&ctx, 10, 9, 0);
                // a1 = global_idx
                if (global_idx < 2048) {
                    emit_addi_phys(&ctx, 11, 0, (int16_t)global_idx);
                } else {
                    uint32_t hi = ((global_idx + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(global_idx - hi);
                    emit_lui_phys(&ctx, 11, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 11, 11, lo);
                }
                // a2 = v_regs (const)
                emit_addi_phys(&ctx, 12, 18, 0);
                // a3 = num_virtual_regs
                if (num_virtual_regs < 2048) {
                    emit_addi_phys(&ctx, 13, 0, (int16_t)num_virtual_regs);
                } else {
                    uint32_t hi = ((num_virtual_regs + 0x800) & 0xFFFFF000);
                    int16_t lo = (int16_t)(num_virtual_regs - hi);
                    emit_lui_phys(&ctx, 13, hi);
                    if (lo != 0) emit_addi_phys(&ctx, 13, 13, lo);
                }
                // a4 = rs
                emit_addi_phys(&ctx, 14, 0, (int16_t)rs);

                emit_call_helper(&ctx, (uintptr_t)&espb_jit_st_global);
                break;
            }
            
            case 0x20: { // ADD.I32 Rd, Rs1, Rs2
                if (no_spill_fastpath) {
                    uint8_t rd = *pc++;
                    uint8_t rs1 = *pc++;
                    uint8_t rs2 = *pc++;
                    uint8_t pd = FP_MAP_VREG(rd);
                    uint8_t p1 = FP_MAP_VREG(rs1);
                    uint8_t p2 = FP_MAP_VREG(rs2);
                    
                    // All in range - pure register operation
                    if (pd != 0 && p1 != 0 && p2 != 0) {
                        emit_add_phys(&ctx, pd, p1, p2);
                        break;
                    }
                    
                    // Handle mixed cases: some in physical regs, some in memory
                    if (pd != 0) {
                        // Result goes to physical register
                        uint8_t src1_reg = p1;
                        uint8_t src2_reg = p2;
                        
                        // Load operands that are out of range from memory
                        if (p1 == 0) {
                            emit_lw_phys(&ctx, 5, rs1 * 8, 18);  // t0 = v_regs[rs1]
                            src1_reg = 5;
                        }
                        if (p2 == 0) {
                            emit_lw_phys(&ctx, 6, rs2 * 8, 18);  // t1 = v_regs[rs2]
                            src2_reg = 6;
                        }
                        
                        emit_add_phys(&ctx, pd, src1_reg, src2_reg);
                        break;
                    }
                    // rd out of range - fall through to slow path
                    pc -= 3; // Rewind pc to re-read operands
                }
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                // Сброс CMP трекера (любая не-CMP инструкция)
                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                emit_add_phys(&ctx, p1, p1, p2);
                // peephole store-elision: откладываем запись в v_regs до барьера
                ph_set(&ph, p1, rd, true);

                break;
            }
            
            case 0x21: { // SUB.I32 Rd, Rs1, Rs2
                if (no_spill_fastpath) {
                    uint8_t rd = *pc++;
                    uint8_t rs1 = *pc++;
                    uint8_t rs2 = *pc++;
                    uint8_t pd = FP_MAP_VREG(rd);
                    uint8_t p1 = FP_MAP_VREG(rs1);
                    uint8_t p2 = FP_MAP_VREG(rs2);
                    // If any register is out of range (returns 0), fall back to slow path
                    if (pd != 0 && p1 != 0 && p2 != 0) {
                        emit_sub_phys(&ctx, pd, p1, p2);
                        break;
                    }
                    // Fall through to slow path if registers out of range
                    pc -= 3; // Rewind pc to re-read operands
                }
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                emit_sub_phys(&ctx, p1, p1, p2);
                // peephole store-elision: откладываем запись в v_regs до барьера
                ph_set(&ph, p1, rd, true);

                break;
            }
            
            case 0x22: { // MUL.I32 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                // mul p1, p1, p2
                uint32_t mul_instr = (0b0000001 << 25) | (p2 << 20) | (p1 << 15) | (0b000 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, mul_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x23: { // DIVS.I32 Rd, Rs1, Rs2 (signed division)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                // div p1, p1, p2 (signed)
                uint32_t div_instr = (0b0000001 << 25) | (p2 << 20) | (p1 << 15) | (0b100 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, div_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x24: { // REMS.I32 Rd, Rs1, Rs2 (signed remainder)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                // rem p1, p1, p2 (signed)
                uint32_t rem_instr = (0b0000001 << 25) | (p2 << 20) | (p1 << 15) | (0b110 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, rem_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x26: { // DIVU.I32 Rd, Rs1, Rs2 (unsigned division)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                // divu p1, p1, p2 (unsigned)
                uint32_t divu_instr = (0b0000001 << 25) | (p2 << 20) | (p1 << 15) | (0b101 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, divu_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x27: { // REMU.I32 Rd, Rs1, Rs2 (unsigned remainder)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                // remu p1, p1, p2 (unsigned)
                uint32_t remu_instr = (0b0000001 << 25) | (p2 << 20) | (p1 << 15) | (0b111 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, remu_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }

            // I64 bitwise ops (v1.7 opcodes 0x38..0x3A) — inline, no helper
            case 0x38: { // AND.I64 Rd, R1, R2
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                // t0/t1 = r1(lo/hi), t2/t3 = r2(lo/hi)
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                emit_lw_phys(&ctx, 28, r2 * 8 + 4, 18);

                // and
                emit_instr(&ctx, (0b0000000 << 25) | (7u << 20) | (5u << 15) | (0b111u << 12) | (5u << 7) | 0b0110011u);   // and x5,x5,x7
                emit_instr(&ctx, (0b0000000 << 25) | (28u << 20) | (6u << 15) | (0b111u << 12) | (6u << 7) | 0b0110011u); // and x6,x6,x28

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }
            case 0x39: { // OR.I64
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                emit_lw_phys(&ctx, 28, r2 * 8 + 4, 18);

                emit_instr(&ctx, (0b0000000 << 25) | (7u << 20) | (5u << 15) | (0b110u << 12) | (5u << 7) | 0b0110011u);   // or x5,x5,x7
                emit_instr(&ctx, (0b0000000 << 25) | (28u << 20) | (6u << 15) | (0b110u << 12) | (6u << 7) | 0b0110011u); // or x6,x6,x28

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }
            case 0x3A: { // XOR.I64
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                emit_lw_phys(&ctx, 28, r2 * 8 + 4, 18);

                emit_instr(&ctx, (0b0000000 << 25) | (7u << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0110011u);   // xor x5,x5,x7
                emit_instr(&ctx, (0b0000000 << 25) | (28u << 20) | (6u << 15) | (0b100u << 12) | (6u << 7) | 0b0110011u); // xor x6,x6,x28

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }
            case 0x3B: { // SHL.I64 Rd, R1, R2 (shamt = low32(R2)) — inline
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                // lo=x5 hi=x6 sh=x7
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                // sh &= 63
                emit_instr(&ctx, (63u << 20) | (7u << 15) | (0b111u << 12) | (7u << 7) | 0b0010011u); // andi x7,x7,63

                // if sh < 32 -> lt32, if sh==32 -> eq32, else gt32
                emit_addi_phys(&ctx, 28, 0, 32);
                size_t br_lt = ctx.offset; emit_bltu_phys(&ctx, 7, 28, 0);
                size_t br_eq = ctx.offset; emit_beq_phys(&ctx, 7, 28, 0);

                // gt32: s=sh-32; new_hi = lo << s; new_lo = 0
                emit_addi_phys(&ctx, 28, 7, -32);
                // sll hi, lo, s
                emit_instr(&ctx, (0b0000000u << 25) | (28u << 20) | (5u << 15) | (0b001u << 12) | (6u << 7) | 0b0110011u);
                emit_addi_phys(&ctx, 5, 0, 0);
                size_t jal_end0 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // lt32:
                size_t off_lt = ctx.offset;
                {
                    // new_hi = (hi<<sh) | (lo>>(32-sh))
                    // new_lo = lo<<sh
                    // tmp = lo>>(32-sh)
                    emit_addi_phys(&ctx, 28, 0, 32);
                    emit_sub_phys(&ctx, 28, 28, 7); // x28 = 32 - sh
                    // tmp (x31) = lo >> x28
                    emit_instr(&ctx, (0b0000000u << 25) | (28u << 20) | (5u << 15) | (0b101u << 12) | (31u << 7) | 0b0110011u);
                    // hi = hi << sh
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (6u << 15) | (0b001u << 12) | (6u << 7) | 0b0110011u);
                    // hi |= tmp
                    emit_instr(&ctx, (0b0000000u << 25) | (31u << 20) | (6u << 15) | (0b110u << 12) | (6u << 7) | 0b0110011u);
                    // lo = lo << sh
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (5u << 15) | (0b001u << 12) | (5u << 7) | 0b0110011u);
                }
                size_t jal_end1 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // eq32:
                size_t off_eq = ctx.offset;
                emit_addi_phys(&ctx, 6, 5, 0); // hi=lo
                emit_addi_phys(&ctx, 5, 0, 0); // lo=0

                size_t off_end = ctx.offset;

                // Patch local branches/jals
                {
                    int32_t d_lt = (int32_t)(off_lt - br_lt);
                    uint32_t ins = encode_branch_instr(0b110, 7, 28, (int16_t)d_lt);
                    memcpy(ctx.buffer + br_lt, &ins, 4);

                    int32_t d_eq = (int32_t)(off_eq - br_eq);
                    ins = encode_branch_instr(0b000, 7, 28, (int16_t)d_eq);
                    memcpy(ctx.buffer + br_eq, &ins, 4);

                    int32_t d_end0 = (int32_t)(off_end - jal_end0);
                    ins = encode_jal_instr(0, d_end0);
                    memcpy(ctx.buffer + jal_end0, &ins, 4);

                    int32_t d_end1 = (int32_t)(off_end - jal_end1);
                    ins = encode_jal_instr(0, d_end1);
                    memcpy(ctx.buffer + jal_end1, &ins, 4);
                }

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x3C: { // SHR.I64 arithmetic — inline
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                emit_instr(&ctx, (63u << 20) | (7u << 15) | (0b111u << 12) | (7u << 7) | 0b0010011u); // andi sh

                emit_addi_phys(&ctx, 28, 0, 32);
                size_t br_lt = ctx.offset; emit_bltu_phys(&ctx, 7, 28, 0);
                size_t br_eq = ctx.offset; emit_beq_phys(&ctx, 7, 28, 0);

                // gt32: s=sh-32; lo = (int32)hi >> s; hi = sign(hi)
                emit_addi_phys(&ctx, 28, 7, -32);
                // lo = sra hi, s
                emit_instr(&ctx, (0b0100000u << 25) | (28u << 20) | (6u << 15) | (0b101u << 12) | (5u << 7) | 0b0110011u);
                // hi = srai hi,31
                emit_instr(&ctx, (0b0100000u << 25) | (31u << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0010011u);
                size_t jal_end0 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // lt32:
                size_t off_lt = ctx.offset;
                {
                    // new_lo = (lo>>sh) | (hi<<(32-sh)); new_hi = (int32)hi >> sh
                    emit_addi_phys(&ctx, 28, 0, 32);
                    emit_sub_phys(&ctx, 28, 28, 7); // 32-sh
                    // tmp = hi << (32-sh) in x31
                    emit_instr(&ctx, (0b0000000u << 25) | (28u << 20) | (6u << 15) | (0b001u << 12) | (31u << 7) | 0b0110011u);
                    // lo = lo >> sh (logical)
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (5u << 15) | (0b101u << 12) | (5u << 7) | 0b0110011u);
                    // lo |= tmp
                    emit_instr(&ctx, (0b0000000u << 25) | (31u << 20) | (5u << 15) | (0b110u << 12) | (5u << 7) | 0b0110011u);
                    // hi = sra hi, sh
                    emit_instr(&ctx, (0b0100000u << 25) | (7u << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0110011u);
                }
                size_t jal_end1 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // eq32:
                size_t off_eq = ctx.offset;
                // lo = hi
                emit_addi_phys(&ctx, 5, 6, 0);
                // hi = sign(hi)
                emit_instr(&ctx, (0b0100000u << 25) | (31u << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0010011u);

                size_t off_end = ctx.offset;

                // Patch branches/jals
                {
                    uint32_t ins;
                    int32_t d_lt = (int32_t)(off_lt - br_lt);
                    ins = encode_branch_instr(0b110, 7, 28, (int16_t)d_lt);
                    memcpy(ctx.buffer + br_lt, &ins, 4);
                    int32_t d_eq = (int32_t)(off_eq - br_eq);
                    ins = encode_branch_instr(0b000, 7, 28, (int16_t)d_eq);
                    memcpy(ctx.buffer + br_eq, &ins, 4);
                    int32_t d_end0 = (int32_t)(off_end - jal_end0);
                    ins = encode_jal_instr(0, d_end0);
                    memcpy(ctx.buffer + jal_end0, &ins, 4);
                    int32_t d_end1 = (int32_t)(off_end - jal_end1);
                    ins = encode_jal_instr(0, d_end1);
                    memcpy(ctx.buffer + jal_end1, &ins, 4);
                }

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x3D: { // USHR.I64 logical — inline
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, r2 * 8, 18);
                emit_instr(&ctx, (63u << 20) | (7u << 15) | (0b111u << 12) | (7u << 7) | 0b0010011u);

                emit_addi_phys(&ctx, 28, 0, 32);
                size_t br_lt = ctx.offset; emit_bltu_phys(&ctx, 7, 28, 0);
                size_t br_eq = ctx.offset; emit_beq_phys(&ctx, 7, 28, 0);

                // gt32: s=sh-32; lo = hi >> s (logical); hi=0
                emit_addi_phys(&ctx, 28, 7, -32);
                emit_instr(&ctx, (0b0000000u << 25) | (28u << 20) | (6u << 15) | (0b101u << 12) | (5u << 7) | 0b0110011u); // srl lo,hi,s
                emit_addi_phys(&ctx, 6, 0, 0);
                size_t jal_end0 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // lt32:
                size_t off_lt = ctx.offset;
                {
                    // new_lo = (lo>>sh) | (hi<<(32-sh)); new_hi = hi >> sh
                    emit_addi_phys(&ctx, 28, 0, 32);
                    emit_sub_phys(&ctx, 28, 28, 7);
                    // tmp = hi << (32-sh) in x31
                    emit_instr(&ctx, (0b0000000u << 25) | (28u << 20) | (6u << 15) | (0b001u << 12) | (31u << 7) | 0b0110011u);
                    // lo = lo >> sh
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (5u << 15) | (0b101u << 12) | (5u << 7) | 0b0110011u);
                    // lo |= tmp
                    emit_instr(&ctx, (0b0000000u << 25) | (31u << 20) | (5u << 15) | (0b110u << 12) | (5u << 7) | 0b0110011u);
                    // hi = hi >> sh
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0110011u);
                }
                size_t jal_end1 = ctx.offset; emit_jal_phys(&ctx, 0, 0);

                // eq32:
                size_t off_eq = ctx.offset;
                emit_addi_phys(&ctx, 5, 6, 0);
                emit_addi_phys(&ctx, 6, 0, 0);

                size_t off_end = ctx.offset;

                // Patch
                {
                    uint32_t ins;
                    int32_t d_lt = (int32_t)(off_lt - br_lt);
                    ins = encode_branch_instr(0b110, 7, 28, (int16_t)d_lt);
                    memcpy(ctx.buffer + br_lt, &ins, 4);
                    int32_t d_eq = (int32_t)(off_eq - br_eq);
                    ins = encode_branch_instr(0b000, 7, 28, (int16_t)d_eq);
                    memcpy(ctx.buffer + br_eq, &ins, 4);
                    int32_t d_end0 = (int32_t)(off_end - jal_end0);
                    ins = encode_jal_instr(0, d_end0);
                    memcpy(ctx.buffer + jal_end0, &ins, 4);
                    int32_t d_end1 = (int32_t)(off_end - jal_end1);
                    ins = encode_jal_instr(0, d_end1);
                    memcpy(ctx.buffer + jal_end1, &ins, 4);
                }

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }
            case 0x3E: { // NOT.I64 Rd, R1 (inline)
                uint8_t rd = *pc++; uint8_t r1 = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);

                // xori lo, lo, -1 ; xori hi, hi, -1
                emit_instr(&ctx, ((uint32_t)0xFFFu << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0010011u);
                emit_instr(&ctx, ((uint32_t)0xFFFu << 20) | (6u << 15) | (0b100u << 12) | (6u << 7) | 0b0010011u);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }

            case 0x58: { // SHRU.I64.IMM8 Rd, R1, imm8 (Logical Shift Right) — inline
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t imm = *pc++;
                ctx.last_cmp_result_reg = 0xFF;
                ph_flush(&ctx, &ph);

                uint8_t sh = (uint8_t)(imm & 63u);

                // Load source (lo/hi)
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r1 * 8 + 4, 18);

                if (sh == 0) {
                    // no-op
                } else if (sh < 32) {
                    // new_lo = (lo >> sh) | (hi << (32-sh)); new_hi = hi >> sh
                    uint8_t left = (uint8_t)(32u - sh);

                    // srli lo, lo, sh
                    emit_instr(&ctx, ((uint32_t)(sh & 0x1F) << 20) | (5u << 15) | (0b101u << 12) | (5u << 7) | 0b0010011u);
                    // slli t2, hi, left
                    emit_instr(&ctx, ((uint32_t)(left & 0x1F) << 20) | (6u << 15) | (0b001u << 12) | (7u << 7) | 0b0010011u);
                    // or lo, lo, t2
                    emit_instr(&ctx, (0b0000000u << 25) | (7u << 20) | (5u << 15) | (0b110u << 12) | (5u << 7) | 0b0110011u);
                    // srli hi, hi, sh
                    emit_instr(&ctx, ((uint32_t)(sh & 0x1F) << 20) | (6u << 15) | (0b101u << 12) | (6u << 7) | 0b0010011u);
                } else if (sh == 32) {
                    // new_lo = hi; new_hi = 0
                    emit_addi_phys(&ctx, 5, 6, 0);
                    emit_addi_phys(&ctx, 6, 0, 0);
                } else {
                    // sh in 33..63: new_lo = hi >> (sh-32); new_hi = 0
                    uint8_t s = (uint8_t)(sh - 32u);
                    // srli lo, hi, s
                    emit_instr(&ctx, ((uint32_t)(s & 0x1F) << 20) | (6u << 15) | (0b101u << 12) | (5u << 7) | 0b0010011u);
                    emit_addi_phys(&ctx, 6, 0, 0);
                }

                // Store result
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 6, rd * 8 + 4, 18);
                break;
            }
            
            case 0x30: { // ADD.I64 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Fast Path для I64: работаем с парами регистров, используем FP_I64_LO/HI макросы
                if (no_spill_fastpath && rd <= 9 && rs1 <= 9 && rs2 <= 9) {
                    uint8_t pd_lo = FP_I64_LO(rd);
                    uint8_t pd_hi = FP_I64_HI(rd);
                    uint8_t p1_lo = FP_I64_LO(rs1);
                    uint8_t p1_hi = FP_I64_HI(rs1);
                    uint8_t p2_lo = FP_I64_LO(rs2);
                    uint8_t p2_hi = FP_I64_HI(rs2);
                    
                    // Используем x29 как временный для carry
                    uint8_t tmp_carry = 29;
                    
                    // ADD.I64 с переносом
                    emit_add_phys(&ctx, pd_lo, p1_lo, p2_lo);           // lo = p1_lo + p2_lo
                    emit_sltu_phys(&ctx, tmp_carry, pd_lo, p1_lo);      // carry = (lo < p1_lo) ? 1 : 0
                    emit_add_phys(&ctx, pd_hi, p1_hi, p2_hi);           // hi = p1_hi + p2_hi
                    emit_add_phys(&ctx, pd_hi, pd_hi, tmp_carry);       // hi = hi + carry
                    
                    break;
                }
                
                // Стандартный путь (fallback)

                ctx.last_cmp_result_reg = 0xFF;

                // i64 peephole: кэшируем rs1 в x5(lo)/x6(hi)
                if (!ph_has_i64(&ph, rs1)) {
                    // если был другой i64 dirty — он сбросится на барьере, но тут мы в i64 op; сбросим если нужно
                    // (без сброса нельзя, иначе потеряем значения при замене кэша)
                    ph_flush(&ctx, &ph);
                    emit_lw_phys(&ctx, 5, (int16_t)(rs1 * 8), 18);
                    emit_lw_phys(&ctx, 6, (int16_t)(rs1 * 8 + 4), 18);
                    ph_set_i64(&ph, rs1, false);
                }

                // rs2 -> x7(lo)/x28(hi)
                emit_lw_phys(&ctx, 7, (int16_t)(rs2 * 8), 18);
                emit_lw_phys(&ctx, 28, (int16_t)(rs2 * 8 + 4), 18);

                // low: x5 = x5 + x7
                emit_add_phys(&ctx, 5, 5, 7);
                // carry: x29 = (x5 < x7)
                emit_instr(&ctx, (0b0000000 << 25) | (7 << 20) | (5 << 15) | (0b011 << 12) | (29 << 7) | 0b0110011);

                // high: x6 = x6 + x28 + carry
                emit_add_phys(&ctx, 6, 6, 28);
                emit_add_phys(&ctx, 6, 6, 29);

                // результат теперь в x5/x6 и кэшируется как rd
                ph_set_i64(&ph, rd, true);
                break;
            }

            case 0x31: { // SUB.I64 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                // i64 peephole: кэшируем rs1 в x5(lo)/x6(hi)
                if (!ph_has_i64(&ph, rs1)) {
                    ph_flush(&ctx, &ph);
                    emit_lw_phys(&ctx, 5, (int16_t)(rs1 * 8), 18);
                    emit_lw_phys(&ctx, 6, (int16_t)(rs1 * 8 + 4), 18);
                    ph_set_i64(&ph, rs1, false);
                }

                // rs2 -> x7(lo)/x28(hi)
                emit_lw_phys(&ctx, 7, (int16_t)(rs2 * 8), 18);
                emit_lw_phys(&ctx, 28, (int16_t)(rs2 * 8 + 4), 18);

                // low: x5 = x5 - x7
                // borrow: x29 = (x5 < x7) computed BEFORE sub? we can compute borrow from original low.
                // Use SLTU x29, x5, x7 before sub.
                emit_instr(&ctx, (0b0000000 << 25) | (7 << 20) | (5 << 15) | (0b011 << 12) | (29 << 7) | 0b0110011);
                emit_sub_phys(&ctx, 5, 5, 7);

                // high: x6 = x6 - x28 - borrow
                emit_sub_phys(&ctx, 6, 6, 28);
                emit_sub_phys(&ctx, 6, 6, 29);

                ph_set_i64(&ph, rd, true);
                break;
            }

            case 0x32: { // MUL.I64 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                emit_lw_phys(&ctx, 10, rs1 * 8, 18);
                emit_lw_phys(&ctx, 11, rs1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, rs2 * 8, 18);
                emit_lw_phys(&ctx, 13, rs2 * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_mul64);
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }

            case 0x33: { // DIVS.I64 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                emit_lw_phys(&ctx, 10, rs1 * 8, 18);
                emit_lw_phys(&ctx, 11, rs1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, rs2 * 8, 18);
                emit_lw_phys(&ctx, 13, rs2 * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_divs64);
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }

            case 0x34: { // REMS.I64 Rd, Rs1, Rs2 (signed remainder)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                emit_lw_phys(&ctx, 10, rs1 * 8, 18);
                emit_lw_phys(&ctx, 11, rs1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, rs2 * 8, 18);
                emit_lw_phys(&ctx, 13, rs2 * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_rems64);
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }

            case 0x36: { // DIVU.I64 Rd, Rs1, Rs2 (unsigned 64-bit division)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                
                // Load dividend (64-bit) into a0/a1
                emit_lw_phys(&ctx, 10, rs1 * 8, 18);     // a0 = low word
                emit_lw_phys(&ctx, 11, rs1 * 8 + 4, 18); // a1 = high word
                
                // Load divisor (64-bit) into a2/a3
                emit_lw_phys(&ctx, 12, rs2 * 8, 18);     // a2 = low word
                emit_lw_phys(&ctx, 13, rs2 * 8 + 4, 18); // a3 = high word
                
                // Call jit_helper_divu64 wrapper
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_divu64);
                
                // Result in a0/a1 -> store to v_regs[rd]
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }
            
            case 0x37: { // REMU.I64 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;
                emit_lw_phys(&ctx, 10, rs1 * 8, 18);
                emit_lw_phys(&ctx, 11, rs1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, rs2 * 8, 18);
                emit_lw_phys(&ctx, 13, rs2 * 8 + 4, 18);
                emit_call_helper(&ctx, (uintptr_t)&jit_helper_remu64);
                emit_sw_phys(&ctx, 10, rd * 8, 18);
                emit_sw_phys(&ctx, 11, rd * 8 + 4, 18);
                break;
            }
            
            case 0x28: { // AND.I32 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t and_instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b111 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, and_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x29: { // OR.I32 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t or_instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b110 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, or_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x2A: { // XOR.I32 Rd, Rs1, Rs2
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t xor_instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b100 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, xor_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x2B: { // SHL.I32 Rd, Rs1, Rs2 (shift left)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t sll_instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b001 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, sll_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x2C: { // SHRS.I32 Rd, Rs1, Rs2 (shift right arithmetic)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t sra_instr = (0b0100000 << 25) | (p2 << 20) | (p1 << 15) | (0b101 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, sra_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x2D: { // SHRU.I32 Rd, Rs1, Rs2 (shift right logical)
                uint8_t rd = *pc++;
                uint8_t rs1 = *pc++;
                uint8_t rs2 = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                uint8_t p1 = 5;
                uint8_t p2 = 6;
                int f1 = ph_find(&ph, rs1);
                int f2 = ph_find(&ph, rs2);

                if (rs1 == rs2) {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = p1;
                } else if (f1 != -1 && f2 != -1) {
                    p1 = (uint8_t)f1;
                    p2 = (uint8_t)f2;
                } else if (f1 != -1 && f2 == -1) {
                    p1 = (uint8_t)f1;
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, (p1 == 5 ? 6 : 5));
                } else if (f1 == -1 && f2 != -1) {
                    p2 = (uint8_t)f2;
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, (p2 == 5 ? 6 : 5));
                } else {
                    p1 = ph_ensure_loaded(&ctx, &ph, rs1, 5);
                    p2 = ph_ensure_loaded(&ctx, &ph, rs2, 6);
                }

                uint32_t srl_instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b101 << 12) | (p1 << 7) | 0b0110011;
                emit_instr(&ctx, srl_instr);

                // peephole store-elision
                ph_set(&ph, p1, rd, true);
                break;
            }
            
            case 0x2E: { // NOT.I32 Rd, Rs (bitwise NOT)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                
                ctx.last_cmp_result_reg = 0xFF;
                
                int fr = ph_find(&ph, rs);
                uint8_t ps = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);
                
                // RISC-V: NOT is implemented as XORI rd, rs, -1
                // XORI: imm[11:0] | rs1 | 100 | rd | 0010011
                uint32_t imm = 0xFFF; // -1 in 12-bit immediate
                emit_instr(&ctx, (imm << 20) | (ps << 15) | (0b100 << 12) | (ps << 7) | 0b0010011);
                
                // peephole store-elision
                ph_set(&ph, ps, rd, true);
                break;
            }

            

            
            case 0x40: { // ADD.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                emit_addi_phys(&ctx, p, p, imm);
                // peephole store-elision
                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x41: { // SUB.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                emit_addi_phys(&ctx, p, p, -imm);  // addi с отрицанием
                // peephole store-elision
                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x42: { // MUL.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // Load imm into a temp phys reg
                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, imm);

                // mul p, p, tmp (RV32M extension)
                emit_instr(&ctx, (0x01 << 25) | (tmp << 20) | (p << 15) | (0x0 << 12) | (p << 7) | 0b0110011);

                // peephole store-elision
                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x43: { // DIVS.I32.IMM8 Rd, Rs, imm8 - Signed division
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                // Check for division by zero (should not happen with valid bytecode)
                if (imm == 0) {
                    // Generate trap or error - for now just load 0
                    uint8_t p = ph_ensure_loaded(&ctx, &ph, rs, 5);
                    emit_addi_phys(&ctx, p, 0, 0);
                    ph_set(&ph, p, rd, true);
                    break;
                }

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // Load imm into a temp phys reg
                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, imm);

                // div p, p, tmp (RV32M extension - signed division)
                emit_instr(&ctx, (0x01 << 25) | (tmp << 20) | (p << 15) | (0x4 << 12) | (p << 7) | 0b0110011);

                // peephole store-elision
                ph_set(&ph, p, rd, true);
                break;
            }

            case 0x49: { // AND.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // andi p, p, imm (sign-extended imm12)
                uint32_t andi_instr = ((uint32_t)(imm & 0xFFF) << 20) | (p << 15) | (0b111 << 12) | (p << 7) | 0b0010011;
                emit_instr(&ctx, andi_instr);

                ph_set(&ph, p, rd, true);
                break;
            }

            case 0x4A: { // OR.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, imm);

                // or p, p, tmp
                emit_instr(&ctx, (0b0000000 << 25) | (tmp << 20) | (p << 15) | (0b110 << 12) | (p << 7) | 0b0110011);

                ph_set(&ph, p, rd, true);
                break;
            }

            case 0x4B: { // XOR.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // xori p, p, imm (sign-extended imm12)
                uint32_t xori_instr = ((uint32_t)(imm & 0xFFF) << 20) | (p << 15) | (0b100 << 12) | (p << 7) | 0b0010011;
                emit_instr(&ctx, xori_instr);

                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x44: { // DIVU.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                uint8_t imm_u = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                if (imm_u == 0) {
                    uint8_t p = ph_ensure_loaded(&ctx, &ph, rs, 5);
                    emit_addi_phys(&ctx, p, 0, 0);
                    ph_set(&ph, p, rd, true);
                    break;
                }

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, (int16_t)imm_u);

                // divu p, p, tmp (RV32M extension - unsigned division)
                emit_instr(&ctx, (0x01 << 25) | (tmp << 20) | (p << 15) | (0x5 << 12) | (p << 7) | 0b0110011);

                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x45: { // REMS.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                int8_t imm = (int8_t)*pc++;

                ctx.last_cmp_result_reg = 0xFF;

                if (imm == 0) {
                    uint8_t p = ph_ensure_loaded(&ctx, &ph, rs, 5);
                    emit_addi_phys(&ctx, p, 0, 0);
                    ph_set(&ph, p, rd, true);
                    break;
                }

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, imm);

                // rem p, p, tmp (RV32M extension - signed remainder)
                emit_instr(&ctx, (0x01 << 25) | (tmp << 20) | (p << 15) | (0x6 << 12) | (p << 7) | 0b0110011);

                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x46: { // REMU.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                uint8_t imm_u = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                if (imm_u == 0) {
                    uint8_t p = ph_ensure_loaded(&ctx, &ph, rs, 5);
                    emit_addi_phys(&ctx, p, 0, 0);
                    ph_set(&ph, p, rd, true);
                    break;
                }

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                uint8_t tmp = (p == 6) ? 7 : 6;
                emit_addi_phys(&ctx, tmp, 0, (int16_t)imm_u);

                // remu p, p, tmp (RV32M extension - unsigned remainder)
                emit_instr(&ctx, (0x01 << 25) | (tmp << 20) | (p << 15) | (0x7 << 12) | (p << 7) | 0b0110011);

                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x47: { // SHRS.I32.IMM8 Rd, Rs, imm8
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                uint8_t imm = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // srai p, p, imm
                uint32_t srai_instr = ((0b0100000 << 25) | ((imm & 0x1F) << 20) | (p << 15) | (0b101 << 12) | (p << 7) | 0b0010011);
                emit_instr(&ctx, srai_instr);
                ph_set(&ph, p, rd, true);
                break;
            }
            
            case 0x48: { // SHRU.I32.IMM8 Rd, Rs, imm8 (logical shift right)
                uint8_t rd = *pc++;
                uint8_t rs = *pc++;
                uint8_t imm = *pc++;

                ctx.last_cmp_result_reg = 0xFF;

                int fr = ph_find(&ph, rs);
                uint8_t p = (fr != -1) ? (uint8_t)fr : ph_ensure_loaded(&ctx, &ph, rs, 5);

                // srli p, p, imm
                uint32_t srli_instr = ((imm & 0x1F) << 20) | (p << 15) | (0b101 << 12) | (p << 7) | 0b0010011;
                emit_instr(&ctx, srli_instr);
                // peephole store-elision
                ph_set(&ph, p, rd, true);
                break;
            }

            // 0x4A/0x4B now handled as I32 IMM8 (OR/XOR) earlier in this switch.
            
            case 0xC0: { // CMP.EQ.I32 Rd, R1, R2 (R1 == R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                
                // Загружаем операнды
                emit_lw_phys(&ctx, 5, r1 * 8, 18);  // t0 = v_regs[r1]
                emit_lw_phys(&ctx, 6, r2 * 8, 18);  // t1 = v_regs[r2]
                
                // sub t2, t0, t1
                emit_sub_phys(&ctx, 7, 5, 6);
                
                // sltiu t0, t2, 1  (t0 = (t2 == 0) ? 1 : 0)
                // Используем sltiu (set less than immediate unsigned)
                uint32_t instr = (1 << 20) | (7 << 15) | (0b011 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, instr);
                
                // Сохраняем результат
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18); // BOOL: clear high
                
                break;
            }
            
            case 0xC1: { // CMP.NE.I32 Rd, R1, R2 (R1 != R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);
                
                // sub t2, t0, t1
                emit_sub_phys(&ctx, 7, 5, 6);
                
                // sltu t0, x0, t2  (t0 = (t2 != 0) ? 1 : 0)
                uint32_t instr = (7 << 20) | (0 << 15) | (0b011 << 12) | (5 << 7) | 0b0110011;
                emit_instr(&ctx, instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18); // BOOL: clear high
                
                // CMP+BR_IF оптимизация
                ctx.last_cmp_result_reg = rd;
                ctx.last_cmp_in_t0 = true;
                
                break;
            }
            
            case 0xC2: { // CMP.LTS.I32 Rd, R1, R2 (signed R1 < R2)
                if (no_spill_fastpath) {
                    uint8_t rd = *pc++;
                    uint8_t r1 = *pc++;
                    uint8_t r2 = *pc++;
                    uint8_t pd = FP_MAP_VREG(rd);
                    uint8_t p1 = FP_MAP_VREG(r1);
                    uint8_t p2 = FP_MAP_VREG(r2);
                    // slt pd, p1, p2
                    uint32_t instr = (0b0000000 << 25) | (p2 << 20) | (p1 << 15) | (0b010 << 12) | (pd << 7) | 0b0110011;
                    emit_instr(&ctx, instr);
                    // BOOL: clear high word in memory (v_regs[rd]+4)
                    emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                    // CMP+BR_IF tracking: result in pd, not t0; just clear
                    ctx.last_cmp_result_reg = 0xFF;
                    ctx.last_cmp_in_t0 = false;
                    break;
                }
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;
                
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);
                
                // slt t0, t0, t1  (signed less than)
                uint32_t instr = (0b0000000 << 25) | (6 << 20) | (5 << 15) | (0b010 << 12) | (5 << 7) | 0b0110011;
                emit_instr(&ctx, instr);
                
                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18); // BOOL: clear high
                
                // CMP+BR_IF оптимизация
                ctx.last_cmp_result_reg = rd;
                ctx.last_cmp_in_t0 = true;
                
                break;
            }
            
            case 0xC3: { // CMP.GT.I32S Rd, R1, R2  (i32 R1 > i32 R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                // R1 > R2  <==>  R2 < R1
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);

                // slt t0, t1, t0  (R2 < R1)
                uint32_t instr = (0b0000000u << 25) | (5u << 20) | (6u << 15) | (0b010u << 12) | (5u << 7) | 0b0110011u;
                emit_instr(&ctx, instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }

            case 0xC4: { // CMP.LE.I32S Rd, R1, R2  (i32 R1 <= i32 R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);

                // R1 <= R2  <==>  !(R2 < R1)
                // slt t0, t1, t0 (R2 < R1)
                uint32_t slt_instr = (0b0000000u << 25) | (5u << 20) | (6u << 15) | (0b010u << 12) | (5u << 7) | 0b0110011u;
                emit_instr(&ctx, slt_instr);
                // xori t0, t0, 1
                uint32_t xori_instr = (1u << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0010011u;
                emit_instr(&ctx, xori_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }

            case 0xC5: { // CMP.GE.I32S Rd, R1, R2  (i32 R1 >= i32 R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);

                // R1 >= R2  <==>  !(R1 < R2)
                // slt t0, t0, t1 (R1 < R2)
                uint32_t slt_instr = (0b0000000u << 25) | (6u << 20) | (5u << 15) | (0b010u << 12) | (5u << 7) | 0b0110011u;
                emit_instr(&ctx, slt_instr);
                // xori t0, t0, 1
                uint32_t xori_instr = (1u << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0010011u;
                emit_instr(&ctx, xori_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }

            case 0xC6: { // CMP.LT.I32U Rd, R1, R2  (u32 R1 < u32 R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);

                // sltu t0, t0, t1
                uint32_t instr = (0b0000000u << 25) | (6u << 20) | (5u << 15) | (0b011u << 12) | (5u << 7) | 0b0110011u;
                emit_instr(&ctx, instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }

            case 0xC7: { // CMP.GT.I32U Rd, R1, R2  (u32 R1 > u32 R2)
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);

                // R1 > R2  <==>  R2 < R1 (unsigned)
                // sltu t0, t1, t0
                uint32_t instr = (0b0000000u << 25) | (5u << 20) | (6u << 15) | (0b011u << 12) | (5u << 7) | 0b0110011u;
                emit_instr(&ctx, instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }

           case 0xC8: { // CMP.LE.I32U Rd, R1, R2  (u32 R1 <= u32 R2)
               uint8_t rd = *pc++;
               uint8_t r1 = *pc++;
               uint8_t r2 = *pc++;

               emit_lw_phys(&ctx, 5, r1 * 8, 18);
               emit_lw_phys(&ctx, 6, r2 * 8, 18);

               // R1 <= R2  <==>  !(R2 < R1)
               // sltu t0, t1, t0  (R2 < R1)
               uint32_t sltu_instr = (0b0000000u << 25) | (5u << 20) | (6u << 15) | (0b011u << 12) | (5u << 7) | 0b0110011u;
               emit_instr(&ctx, sltu_instr);
               // xori t0, t0, 1
               uint32_t xori_instr = (1u << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0010011u;
               emit_instr(&ctx, xori_instr);

               emit_sw_phys(&ctx, 5, rd * 8, 18);
               emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
               break;
           }

           case 0xC9: { // CMP.GE.I32U Rd, R1, R2  (u32 R1 >= u32 R2)
               uint8_t rd = *pc++;
               uint8_t r1 = *pc++;
               uint8_t r2 = *pc++;

               emit_lw_phys(&ctx, 5, r1 * 8, 18);
               emit_lw_phys(&ctx, 6, r2 * 8, 18);

               // R1 >= R2  <==>  !(R1 < R2)
               // sltu t0, t0, t1  (R1 < R2)
               uint32_t sltu_instr = (0b0000000u << 25) | (6u << 20) | (5u << 15) | (0b011u << 12) | (5u << 7) | 0b0110011u;
               emit_instr(&ctx, sltu_instr);
               // xori t0, t0, 1
               uint32_t xori_instr = (1u << 20) | (5u << 15) | (0b100u << 12) | (5u << 7) | 0b0010011u;
               emit_instr(&ctx, xori_instr);

               emit_sw_phys(&ctx, 5, rd * 8, 18);
               emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
               break;
           }
            
            case 0xCA: { // CMP.EQ.I64 Rd, R1, R2 (R1 == R2) для 64-bit
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                // IMPORTANT: don't clobber s0 (x8). Use temporaries only.

                // lo diff -> t2
                emit_lw_phys(&ctx, 5, r1 * 8, 18);      // t0 = R1_lo
                emit_lw_phys(&ctx, 6, r2 * 8, 18);      // t1 = R2_lo
                emit_sub_phys(&ctx, 7, 5, 6);           // t2 = lo_diff

                // hi diff -> t3 (x28)
                emit_lw_phys(&ctx, 5, r1 * 8 + 4, 18);  // t0 = R1_hi
                emit_lw_phys(&ctx, 6, r2 * 8 + 4, 18);  // t1 = R2_hi
                emit_sub_phys(&ctx, 28, 5, 6);          // t3 = hi_diff

                // or t2, t2, t3
                uint32_t or_instr = (0b0000000 << 25) | (28 << 20) | (7 << 15) | (0b110 << 12) | (7 << 7) | 0b0110011;
                emit_instr(&ctx, or_instr);

                // sltiu t0, t2, 1  (t0 = (t2 == 0) ? 1 : 0)
                uint32_t sltiu_instr = (1 << 20) | (7 << 15) | (0b011 << 12) | (5 << 7) | 0b0010011;
                emit_instr(&ctx, sltiu_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0xCB: { // CMP.NE.I64 Rd, R1, R2 (R1 != R2) для 64-bit
                uint8_t rd = *pc++;
                uint8_t r1 = *pc++;
                uint8_t r2 = *pc++;

                // IMPORTANT: don't clobber s0 (x8). Use temporaries only.

                // lo diff -> t2
                emit_lw_phys(&ctx, 5, r1 * 8, 18);
                emit_lw_phys(&ctx, 6, r2 * 8, 18);
                emit_sub_phys(&ctx, 7, 5, 6);

                // hi diff -> t3 (x28)
                emit_lw_phys(&ctx, 5, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 6, r2 * 8 + 4, 18);
                emit_sub_phys(&ctx, 28, 5, 6);

                // or t2, t2, t3
                uint32_t or_instr = (0b0000000 << 25) | (28 << 20) | (7 << 15) | (0b110 << 12) | (7 << 7) | 0b0110011;
                emit_instr(&ctx, or_instr);

                // sltu t0, x0, t2  (t0 = (t2 != 0) ? 1 : 0)
                uint32_t sltu_instr = (7 << 20) | (0 << 15) | (0b011 << 12) | (5 << 7) | 0b0110011;
                emit_instr(&ctx, sltu_instr);

                emit_sw_phys(&ctx, 5, rd * 8, 18);
                break;
            }
            
            case 0xD4: // SELECT.F32 - fallthrough
            case 0xD5: // SELECT.F64 - fallthrough  
            case 0xD6: { // SELECT.PTR/I32/I64 Rd, Rcond, Rtrue, Rfalse
                uint8_t rd = *pc++;
                uint8_t rcond = *pc++;
                uint8_t rtrue = *pc++;
                uint8_t rfalse = *pc++;
                
                // Загружаем условие
                emit_lw_phys(&ctx, 5, rcond * 8, 18);  // t0 = condition
                
                // Загружаем true значение
                emit_lw_phys(&ctx, 6, rtrue * 8, 18);  // t1 = true_val
                
                // Загружаем false значение
                emit_lw_phys(&ctx, 7, rfalse * 8, 18); // t2 = false_val
                
                // Если condition != 0, выбираем true, иначе false
                // beq t0, x0, skip_true (если condition == 0)
                emit_beq_phys(&ctx, 5, 0, 8); // skip next instruction
                
                // Выбираем true: mv t2, t1
                emit_addi_phys(&ctx, 7, 6, 0);
                
                // skip_true: сохраняем результат
                emit_sw_phys(&ctx, 7, rd * 8, 18);
                
                break;
            }
            
            case 0x0F: { // END - генерируем эпилог и возврат
                // peephole: убедимся что dirty значения записаны в v_regs
                ph_flush(&ctx, &ph);
                // Flush stable vcache
                if (vcache.kind != VC_NONE && vcache.dirty) {
                    if (vcache.kind == VC_F32) {
                        emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                    } else if (vcache.kind == VC_F64) {
                        emit_sw_phys(&ctx, 20, vcache.vreg * 8, 18);
                        emit_sw_phys(&ctx, 21, vcache.vreg * 8 + 4, 18);
                    }
                }
                vcache.kind = VC_NONE;
                vcache.dirty = false;
                if (no_spill_fastpath) {
                    // return value assumed in v0 -> a0
                    // epilogue will restore regs/sp and ret
                    // Ensure a0 contains v0 already
                }
                // --- EPILOGUE ---
                
                // КРИТИЧНО: Копируем возвращаемое значение из v_regs[0] в a0/a1
                
                // v_regs находится в s2 (x18), результат должен быть в a0 (x10), a1 (x11)
                // Байткод уже записал результат в v_regs[0], теперь копируем в ABI регистры
                
                // NO_SPILL fast-path: сохраняем v0..max_reg_used обратно в v_regs[] перед возвратом
                // КРИТИЧНО: Сохраняем только используемые регистры (0..max_reg_used)
                if (no_spill_fastpath) {
                    if (i32_only) {
                        // I32: один физический регистр на виртуальный
                        for (uint8_t v = 0; v <= max_reg_used; v++) {
                            uint8_t phys = FP_MAP_VREG(v);
                            if (phys != 0) {
                                emit_sw_phys(&ctx, phys, v * 8, 18);
                            }
                        }
                    } else {
                        // I64: пара физических регистров на виртуальный
                        for (uint8_t v = 0; v <= max_reg_used && v <= 7; v++) {
                            uint8_t phys_lo, phys_hi;
                            
                            if (v < 4) {
                                phys_lo = 10 + v * 2;
                                phys_hi = 10 + v * 2 + 1;
                            } else {
                                phys_lo = 19 + (v - 4) * 2;
                                phys_hi = 19 + (v - 4) * 2 + 1;
                            }
                            
                            // Сохраняем пару регистров (lo в offset+0, hi в offset+4)
                            emit_sw_phys(&ctx, phys_lo, v * 8 + 0, 18);  // Младшие 32 бита
                            emit_sw_phys(&ctx, phys_hi, v * 8 + 4, 18);  // Старшие 32 бита
                        }
                    }
                }

                // Загружаем возвращаемое значение (64-bit) из v_regs[0]
                emit_lw_phys(&ctx, 10, 0, 18);      // a0 = v_regs[0] (младшие 32 бита)
                emit_lw_phys(&ctx, 11, 4, 18);      // a1 = v_regs[0]+4 (старшие 32 бита)
                
                // Restore saved regs using the SAME layout/order as prologue (offset walks down)
                int restore_offset = total_frame_size;

                // NO_SPILL fast-path: restore s3..s10 (callee-saved)
                if (no_spill_fastpath) {
                    for (uint8_t r = 19; r <= 26; r++) {
                        restore_offset -= 4;
                        emit_lw_phys(&ctx, r, restore_offset, 2);
                    }
                }

                // ra only if non-leaf
                if (!is_leaf) {
                    restore_offset -= 4;
                    emit_lw_phys(&ctx, 1, restore_offset, 2);
                }

                // s0, s1, s2
                restore_offset -= 4;
                emit_lw_phys(&ctx, 8, restore_offset, 2);
                restore_offset -= 4;
                emit_lw_phys(&ctx, 9, restore_offset, 2);
                restore_offset -= 4;
                emit_lw_phys(&ctx, 18, restore_offset, 2);

                if (stable_cache_enabled) {
                    // s4/s5 (stable cache regs)
                    restore_offset -= 4;
                    emit_lw_phys(&ctx, 20, restore_offset, 2);
                    restore_offset -= 4;
                    emit_lw_phys(&ctx, 21, restore_offset, 2);
                    restore_offset -= 4;
                    emit_lw_phys(&ctx, 22, restore_offset, 2);
                    restore_offset -= 4;
                    emit_lw_phys(&ctx, 23, restore_offset, 2);
                }
                
                // Освобождаем стек фрейм
                emit_addi_phys(&ctx, 2, 2, (int16_t)total_frame_size); // sp += frame_size
                
                // Возврат
                emit_jalr_phys(&ctx, 0, 1, 0); // ret (jalr x0, ra, 0)
                
                // Фиксируем все переходы перед выходом - MOVED TO END
                // jit_context_patch_branches(&ctx, bytecode, body->code_size);
                
                // END: stop parsing bytecode for this function
                // do not terminate scanning: other blocks may follow
                encountered_end = true;
                break;
            }
            
            // ===== I64 Signed/Unsigned Comparisons & FP Comparisons & SELECT =====
            case 0xCC: case 0xCD: case 0xCE: case 0xCF: { // CMP.I64 signed
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                emit_lw_phys(&ctx, 10, r1 * 8, 18); emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, r2 * 8, 18); emit_lw_phys(&ctx, 13, r2 * 8 + 4, 18);
                uintptr_t helper = (opcode == 0xCC) ? (uintptr_t)&jit_helper_cmp_lts_i64 :
                                   (opcode == 0xCD) ? (uintptr_t)&jit_helper_cmp_gt_i64 :
                                   (opcode == 0xCE) ? (uintptr_t)&jit_helper_cmp_le_i64 :
                                                      (uintptr_t)&jit_helper_cmp_ge_i64;
                emit_call_helper(&ctx, helper);
                emit_sw_phys(&ctx, 10, rd * 8, 18); emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            case 0xD0: case 0xD1: case 0xD2: case 0xD3: { // CMP.I64 unsigned
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                emit_lw_phys(&ctx, 10, r1 * 8, 18); emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, r2 * 8, 18); emit_lw_phys(&ctx, 13, r2 * 8 + 4, 18);
                uintptr_t helper = (opcode == 0xD0) ? (uintptr_t)&jit_helper_cmp_ltu_i64 :
                                   (opcode == 0xD1) ? (uintptr_t)&jit_helper_cmp_gtu_i64 :
                                   (opcode == 0xD2) ? (uintptr_t)&jit_helper_cmp_leu_i64 :
                                                      (uintptr_t)&jit_helper_cmp_geu_i64;
                emit_call_helper(&ctx, helper);
                emit_sw_phys(&ctx, 10, rd * 8, 18); emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: { // CMP.F32
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                emit_lw_phys(&ctx, 10, r1 * 8, 18); emit_lw_phys(&ctx, 11, r2 * 8, 18);
                uintptr_t helper = (opcode == 0xE0) ? (uintptr_t)&jit_helper_cmp_eq_f32 :
                                   (opcode == 0xE1) ? (uintptr_t)&jit_helper_cmp_ne_f32 :
                                   (opcode == 0xE2) ? (uintptr_t)&jit_helper_cmp_lt_f32 :
                                   (opcode == 0xE3) ? (uintptr_t)&jit_helper_cmp_gt_f32 :
                                   (opcode == 0xE4) ? (uintptr_t)&jit_helper_cmp_le_f32 :
                                                      (uintptr_t)&jit_helper_cmp_ge_f32;
                emit_call_helper(&ctx, helper);
                emit_sw_phys(&ctx, 10, rd * 8, 18); emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            case 0xE6: case 0xE7: case 0xE8: case 0xE9: case 0xEA: case 0xEB: { // CMP.F64
                uint8_t rd = *pc++; uint8_t r1 = *pc++; uint8_t r2 = *pc++;
                emit_lw_phys(&ctx, 10, r1 * 8, 18); emit_lw_phys(&ctx, 11, r1 * 8 + 4, 18);
                emit_lw_phys(&ctx, 12, r2 * 8, 18); emit_lw_phys(&ctx, 13, r2 * 8 + 4, 18);
                uintptr_t helper = (opcode == 0xE6) ? (uintptr_t)&jit_helper_cmp_eq_f64 :
                                   (opcode == 0xE7) ? (uintptr_t)&jit_helper_cmp_ne_f64 :
                                   (opcode == 0xE8) ? (uintptr_t)&jit_helper_cmp_lt_f64 :
                                   (opcode == 0xE9) ? (uintptr_t)&jit_helper_cmp_gt_f64 :
                                   (opcode == 0xEA) ? (uintptr_t)&jit_helper_cmp_le_f64 :
                                                      (uintptr_t)&jit_helper_cmp_ge_f64;
                emit_call_helper(&ctx, helper);
                emit_sw_phys(&ctx, 10, rd * 8, 18); emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            case 0xBE: { // SELECT.I32
                uint8_t rd = *pc++; uint8_t rcond = *pc++; uint8_t rtrue = *pc++; uint8_t rfalse = *pc++;
                emit_lw_phys(&ctx, 5, rcond * 8, 18); emit_lw_phys(&ctx, 6, rtrue * 8, 18); emit_lw_phys(&ctx, 7, rfalse * 8, 18);
                emit_instr(&ctx, (0 << 20) | (5 << 15) | (0b000 << 12) | (8 << 7) | 0b1100011);
                emit_addi_phys(&ctx, 7, 6, 0);
                emit_sw_phys(&ctx, 7, rd * 8, 18); emit_sw_phys(&ctx, 0, rd * 8 + 4, 18);
                break;
            }
            case 0xBF: { // SELECT.I64
                uint8_t rd = *pc++; uint8_t rcond = *pc++; uint8_t rtrue = *pc++; uint8_t rfalse = *pc++;
                emit_lw_phys(&ctx, 5, rcond * 8, 18);
                emit_lw_phys(&ctx, 6, rtrue * 8, 18); emit_lw_phys(&ctx, 28, rtrue * 8 + 4, 18);
                emit_lw_phys(&ctx, 7, rfalse * 8, 18); emit_lw_phys(&ctx, 29, rfalse * 8 + 4, 18);
                emit_instr(&ctx, (0 << 20) | (5 << 15) | (0b000 << 12) | (16 << 7) | 0b1100011);
                emit_addi_phys(&ctx, 7, 6, 0); emit_addi_phys(&ctx, 29, 28, 0);
                emit_sw_phys(&ctx, 7, rd * 8, 18); emit_sw_phys(&ctx, 29, rd * 8 + 4, 18);
                break;
            }
            
            // ... (другие case, которые мы уже реализовали) ...

            default:
                printf("[JIT ERROR] Unsupported opcode 0x%02X at bytecode offset %zu in func_idx=%u\n", opcode, bytecode_offset, func_idx);
                free(exec_buffer);
                *out_code = NULL;
                *out_size = 0;
                return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
        }
    }
    
    // Если мы дошли до конца без явного END, добавим эпилог автоматически
    // peephole: на всякий случай сбросить dirty значения
    ph_flush(&ctx, &ph);
    if (!encountered_end) {
        
        // --- EPILOGUE (fallback) ---
        int restore_offset = total_frame_size;

        // NO_SPILL fast-path: restore s3..s10 (callee-saved)
        if (no_spill_fastpath) {
            for (uint8_t r = 19; r <= 26; r++) {
                restore_offset -= 4;
                emit_lw_phys(&ctx, r, restore_offset, 2);
            }
        }

        if (!is_leaf) {
            restore_offset -= 4;
            emit_lw_phys(&ctx, 1, restore_offset, 2);
        }

        restore_offset -= 4;
        emit_lw_phys(&ctx, 8, restore_offset, 2);
        restore_offset -= 4;
        emit_lw_phys(&ctx, 9, restore_offset, 2);
        restore_offset -= 4;
        emit_lw_phys(&ctx, 18, restore_offset, 2);

        if (stable_cache_enabled) {
            restore_offset -= 4;
            emit_lw_phys(&ctx, 20, restore_offset, 2);
            restore_offset -= 4;
            emit_lw_phys(&ctx, 21, restore_offset, 2);
            restore_offset -= 4;
            emit_lw_phys(&ctx, 22, restore_offset, 2);
            restore_offset -= 4;
            emit_lw_phys(&ctx, 23, restore_offset, 2);
        }
        
        emit_addi_phys(&ctx, 2, 2, (int16_t)total_frame_size);
        emit_jalr_phys(&ctx, 0, 1, 0);
    } 
    
    // Фиксируем все переходы (теперь вызывается всегда в конце)
    jit_context_patch_branches(&ctx, bytecode, body->code_size);
    
    // Освобождаем ресурсы
    jit_context_free(&ctx);
    
    // Финальная валидация сгенерированного кода (debug output disabled)
    
    // // Детальный hex dump для отладки
    // // Для больших функций выводим: первые 1024 + последние 256
    // size_t dump_size = ctx.offset < 1024 ? ctx.offset : 1024;
    //     for (size_t i = 0; i < dump_size; i += 16) {
    //     printf("  %04zx: ", i);
    //     for (size_t j = 0; j < 16 && (i + j) < dump_size; j++) {
    //     }
    //     printf("\\n");
    // }
    // 
    // // Если код больше 1024 байт, показываем последние 256 байт
    // if (ctx.offset > 1024) {
    //             printf("JIT: Last 256 bytes:\\n");
    //     size_t start = ctx.offset > 256 ? ctx.offset - 256 : 0;
    //     for (size_t i = start; i < ctx.offset; i += 16) {
    //         printf("  %04zx: ", i);
    //         for (size_t j = 0; j < 16 && (i + j) < ctx.offset; j++) {
    //         }
    //         printf("\\n");
    //     }
    // }
    // 
    // // Проверяем пролог функции
    //     if (ctx.offset >= 16) {
    //     uint32_t instr[4];
    //     memcpy(instr, exec_buffer, 16);
    //     printf("JIT: First 4 instructions: 0x%08x 0x%08x 0x%08x 0x%08x\\n", 
    //            instr[0], instr[1], instr[2], instr[3]);
    // }
    
    // Проверяем что код не начинается с нулей или 0xFF
    if (ctx.offset >= 4) {
        uint32_t first_instr;
        memcpy(&first_instr, exec_buffer, 4);
        if (first_instr == 0 || first_instr == 0xFFFFFFFF) {
            printf("JIT ERROR: Invalid first instruction 0x%08x!\n", first_instr);
            free(exec_buffer);
            *out_code = NULL;
            *out_size = 0;
            return ESPB_ERR_JIT_UNSUPPORTED_OPCODE;
        }
    }
    
    // ОПТИМИЗАЦИЯ #3: Выполняем fence.i ОДИН РАЗ после компиляции
    // Это синхронизирует instruction cache с data cache
    // Теперь НЕ нужно делать fence.i при каждом вызове функции!
    #ifdef ESP_PLATFORM
    __asm__ volatile("fence.i" ::: "memory");
    #endif
    
#ifndef JIT_TRIM_EXEC_BUFFER
#define JIT_TRIM_EXEC_BUFFER 0
#endif

#if JIT_TRIM_EXEC_BUFFER
    // Trim unused executable heap.
    // WARNING: With PC-relative helper calls (auipc+jalr), moving the code buffer breaks call targets.
    // Therefore, trimming is disabled by default.
    uint8_t* trimmed = (uint8_t*)espb_exec_realloc(exec_buffer, ctx.offset);
    if (trimmed) {
        exec_buffer = trimmed;
        jit_icache_sync(exec_buffer, ctx.offset);
    }
#endif

    *out_code = exec_buffer;
    *out_size = ctx.offset;
    
    // JIT compilation successful (silent mode)

#ifdef JIT_STATS
    printf("[JIT_STATS] func=%u helper_calls=%u abs_fallback=%u\n",
           (unsigned)func_idx,
           (unsigned)ctx.helper_call_count,
           (unsigned)ctx.helper_call_fallback_abs_count);
#endif

    return ESPB_OK;
}
