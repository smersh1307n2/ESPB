#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

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

    const int ok = (mix == expected_mix) &&
                   (signed_result == -32) &&
                   (bit_result == expected_bits) &&
                   (a < b + 2048) && (a != b);
    printf("[ESPB_GROUP] integer32=%s\\n", ok ? "PASS" : "FAIL");
}
