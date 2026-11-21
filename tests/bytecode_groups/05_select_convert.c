#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

JIT_HOT static int32_t select_i32(int condition, int32_t a, int32_t b)
{
    return condition ? a : b;
}

JIT_HOT static int64_t select_i64(int condition, int64_t a, int64_t b)
{
    return condition ? a : b;
}

JIT_HOT static double select_f64(int condition, double a, double b)
{
    return condition ? a : b;
}

JIT_HOT static uintptr_t pointer_roundtrip(void *pointer)
{
    return (uintptr_t)pointer;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int condition = 1;
    volatile int32_t i32 = select_i32(condition, 123, -456);
    volatile int64_t i64 = select_i64(!condition, 0x123456789ll, -987654321ll);
    volatile double f64 = select_f64(condition, 3.5, 9.25);
    volatile int64_t signed_value = -7;
    int32_t truncated = (int32_t)0x10000002aull;
    int32_t extended = (int32_t)(int8_t)-5;
    uintptr_t pointer_value = pointer_roundtrip((void *)text);
    int ok = i32 == 123 && i64 == -987654321ll && f64 == 3.5 &&
             truncated == 42 && extended == -5 &&
             pointer_value == (uintptr_t)text && signed_value < 0 &&
;
    printf("[ESPB_GROUP] select_convert=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
