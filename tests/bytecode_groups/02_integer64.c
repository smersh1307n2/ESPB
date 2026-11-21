#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

/* Register-form I64 arithmetic and logical operations. */
JIT_PROBE static uint64_t integer64_register_probe(
    int64_t a, int64_t b, uint64_t u, uint64_t v)
{
    int64_t add = a + b;
    int64_t sub = a - b;
    int64_t mul = a * b;
    int64_t divs = a / b;
    int64_t rems = a % b;
    uint64_t divu = u / v;
    uint64_t remu = u % v;
    uint64_t bit_and = u & v;
    uint64_t bit_or = u | v;
    uint64_t bit_xor = u ^ v;
    uint64_t bit_not = ~u;
    uint64_t shift_left = u << 3;
    int64_t shift_right_signed = a >> 3;
    uint64_t shift_right_unsigned = u >> 3;

    return (uint64_t)add ^ (uint64_t)sub ^ (uint64_t)mul ^
           (uint64_t)divs ^ (uint64_t)rems ^ divu ^ remu ^
           bit_and ^ bit_or ^ bit_xor ^ bit_not ^
           shift_left ^ (uint64_t)shift_right_signed ^ shift_right_unsigned;
}

/* Immediate-form I64 operations copied from the older project test. */
JIT_PROBE static uint64_t integer64_immediate_probe(int64_t signed_value,
                                                     uint64_t unsigned_value)
{
    int64_t add_imm = signed_value + 17;
    int64_t sub_imm = signed_value - 9;
    int64_t mul_imm = signed_value * 3;
    int64_t divs_imm = signed_value / -10;
    int64_t rems_imm = signed_value % 13;
    uint64_t divu_imm = unsigned_value / 20u;
    uint64_t remu_imm = unsigned_value % 17u;
    uint64_t shru_imm = unsigned_value >> 3;
    return (uint64_t)add_imm ^ (uint64_t)sub_imm ^ (uint64_t)mul_imm ^
           (uint64_t)divs_imm ^ (uint64_t)rems_imm ^
           divu_imm ^ remu_imm ^ shru_imm;
}

JIT_HOT static int64_t signed_divrem(int64_t value)
{
    return (value / -10) + (value % 13);
}

JIT_HOT static uint64_t unsigned_ops(uint64_t value)
{
    return ((value >> 4) ^ (value << 3)) + (value % 17u);
}

JIT_HOT static int32_t narrow_i64(int64_t value)
{
    return (int32_t)value;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int64_t signed_value = -1000;
    volatile uint64_t unsigned_value = 0x100000000ull + 34ull;
    int64_t signed_result = signed_divrem(signed_value);
    uint64_t unsigned_result = unsigned_ops(unsigned_value);
    int32_t narrowed = narrow_i64(0x10000002aull);

    uint64_t register_probe = integer64_register_probe(
        -1234567890123ll, 37ll, 0x123456789abcdef0ull, 29ull);
    uint64_t immediate_probe =
        integer64_immediate_probe(-10000000000ll, 10000000000ull);

    int ok = (signed_result == 88) && (unsigned_result != 0) &&
             (narrowed == 42) &&
             (register_probe == 9119519501662434067ull) &&
             (immediate_probe == 28201811865ull) &&
             ((unsigned_value >> 4) == 0x10000002ull);

    printf("[ESPB_GROUP] integer64=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
