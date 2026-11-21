#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

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
    int ok = (signed_result == 88) && (unsigned_result != 0) &&
             (narrowed == 42) && ((unsigned_value >> 4) == 0x100000002ull);

    printf("[ESPB_GROUP] integer64=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
