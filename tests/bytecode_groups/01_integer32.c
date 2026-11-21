#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

/*
 * This probe is deliberately non-optimizable. Inputs arrive through function
 * parameters, so the LLVM IR should retain register, immediate, signed,
 * unsigned, bitwise, shift and comparison operations for opcode inspection.
 */
JIT_PROBE static uint32_t integer32_opcode_probe(
    int32_t a, int32_t b, uint32_t u, uint32_t v)
{
    int32_t add = a + b;
    int32_t sub = a - b;
    int32_t mul = a * b;
    int32_t divs = a / b;
    int32_t rems = a % b;
    uint32_t divu = u / v;
    uint32_t remu = u % v;
    int32_t bit_and = a & b;
    int32_t bit_or = a | b;
    int32_t bit_xor = a ^ b;
    int32_t bit_not = ~a;
    uint32_t shift_left = u << 3;
    int32_t shift_right_signed = a >> 3;
    uint32_t shift_right_unsigned = u >> 3;

    int32_t add_imm = a + 7;
    int32_t sub_imm = a - 9;
    int32_t mul_imm = a * 3;
    int32_t divs_imm = a / 5;
    int32_t rems_imm = a % 5;
    uint32_t divu_imm = u / 13u;
    uint32_t remu_imm = u % 11u;
    uint32_t shl_imm = u << 2;
    uint32_t shru_imm = u >> 2;

    uint32_t comparisons =
        (uint32_t)(a == b) |
        ((uint32_t)(a != b) << 1) |
        ((uint32_t)(a < b) << 2) |
        ((uint32_t)(a > b) << 3) |
        ((uint32_t)(a <= b) << 4) |
        ((uint32_t)(a >= b) << 5) |
        ((uint32_t)(u < v) << 6) |
        ((uint32_t)(u >= v) << 7);

    uint32_t mix = (uint32_t)add;
    mix ^= (uint32_t)sub;
    mix ^= (uint32_t)mul;
    mix ^= (uint32_t)divs;
    mix ^= (uint32_t)rems;
    mix ^= divu ^ remu;
    mix ^= (uint32_t)bit_and ^ (uint32_t)bit_or;
    mix ^= (uint32_t)bit_xor ^ (uint32_t)bit_not;
    mix ^= shift_left ^ (uint32_t)shift_right_signed ^ shift_right_unsigned;
    mix ^= (uint32_t)add_imm ^ (uint32_t)sub_imm ^ (uint32_t)mul_imm;
    mix ^= (uint32_t)divs_imm ^ (uint32_t)rems_imm;
    mix ^= divu_imm ^ remu_imm ^ shl_imm ^ shru_imm;
    return mix ^ comparisons;
}

JIT_HOT static int32_t integer_mix(int32_t a, int32_t b)
{
    return ((a + b) * 3) ^ ((a - b) << 2) ^ ((uint32_t)a >> 2);
}

JIT_HOT static int32_t integer_signed(int32_t value, int32_t divisor)
{
    return (value / divisor) * 10 + (value % divisor);
}

JIT_HOT static uint32_t integer_bits(uint32_t value)
{
    return ((value << 5) | (value >> 27)) ^ (value & 0x55aa55aau);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    const int32_t a = arg;
    const int32_t b = arg / 8;
    const int32_t neg = -17;
    const uint32_t bits = (uint32_t)arg ^ 0x12345678u;

    const int32_t mix = integer_mix(a, b);
    const int32_t expected_mix =
        ((a + b) * 3) ^ ((a - b) << 2) ^ ((uint32_t)a >> 2);
    const int32_t signed_result = integer_signed(neg, 5);
    const uint32_t bit_result = integer_bits(bits);
    const uint32_t expected_bits =
        ((bits << 5) | (bits >> 27)) ^ (bits & 0x55aa55aau);

    const uint32_t opcode_probe =
        integer32_opcode_probe(1234567, 37, 0xF1234567u, 29u);
    const int ok = (mix == expected_mix) &&
                   (signed_result == -32) &&
                   (bit_result == expected_bits) &&
                   (opcode_probe == 2286556547u) &&
                   (a < b + 2048) && (a != b);
    printf("[ESPB_GROUP] integer32=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
