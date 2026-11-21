#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

JIT_HOT static int32_t integer_mix(int32_t a, int32_t b)
{
    return ((a + b) * 3) ^ ((a - b) << 2) ^ ((uint32_t)a >> 2);
}

JIT_HOT static int32_t integer_signed(int32_t value)
{
    return (value / 5) * 10 + (value % 5);
}

JIT_HOT static uint32_t integer_bits(uint32_t value)
{
    return ((value << 5) | (value >> 27)) ^ (value & 0x55aa55aau);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int32_t a = 40;
    volatile int32_t b = 9;
    volatile int32_t neg = -17;
    volatile uint32_t bits = 0x12345678u;
    int32_t mix = integer_mix(a, b);
    int32_t signed_result = integer_signed(neg);
    uint32_t bit_result = integer_bits(bits);
    int ok = (mix == 229) && (signed_result == -32) &&
             (bit_result == ((((0x12345678u << 5) | (0x12345678u >> 27)) ^ (0x12345678u & 0x55aa55aau)))) &&
             (a < b + 32) && (a != b);
    printf("[ESPB_GROUP] integer32=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
