#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

JIT_HOT static int32_t select_i32(int condition, int32_t a, int32_t b)
{
    return condition ? a : b;
}

JIT_HOT static int64_t select_i64(int condition, int64_t a, int64_t b)
{
    return condition ? a : b;
}

JIT_HOT static float select_f32(int condition, float a, float b)
{
    return condition ? a : b;
}

JIT_HOT static double select_f64(int condition, double a, double b)
{
    return condition ? a : b;
}

JIT_HOT static void *select_ptr(int condition, void *a, void *b)
{
    return condition ? a : b;
}

/* Keep pointer/integer and narrow/widen conversions in the LLVM IR. */
JIT_PROBE static int32_t pointer_roundtrip(int32_t *pointer)
{
    uintptr_t address = (uintptr_t)pointer;
    int32_t *restored = (int32_t *)address;
    return *restored;
}

JIT_PROBE static uint64_t integer_conversion_probe(int64_t wide, int32_t narrow)
{
    uint8_t u8 = (uint8_t)narrow;
    int8_t s8 = (int8_t)narrow;
    uint16_t u16 = (uint16_t)narrow;
    int16_t s16 = (int16_t)narrow;
    int32_t truncated = (int32_t)wide;
    uint32_t truncated_unsigned = (uint32_t)wide;
    int64_t sign_extended = (int64_t)narrow;
    uint64_t zero_extended = (uint64_t)(uint32_t)narrow;

    uint64_t mix = (uint64_t)u8;
    mix ^= (uint64_t)(int64_t)s8 << 8;
    mix ^= (uint64_t)u16 << 16;
    mix ^= (uint64_t)(int64_t)s16 << 32;
    mix ^= (uint32_t)truncated;
    mix ^= (uint64_t)truncated_unsigned << 1;
    mix ^= (uint64_t)sign_extended;
    mix ^= zero_extended << 2;
    return mix;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int condition = 1;
    volatile int32_t i32 = select_i32(condition, 123, -456);
    volatile int64_t i64 = select_i64(!condition, 0x123456789ll, -987654321ll);
    volatile float f32 = select_f32(!condition, 1.25f, 4.5f);
    volatile double f64 = select_f64(condition, 3.5, 9.25);
    void *selected_ptr = select_ptr(condition, (void *)text, user_data);

    int32_t local = 1234;
    int32_t pointer_value = pointer_roundtrip(&local);
    uint64_t converted = integer_conversion_probe(0x123456789ll,
                                                   (int32_t)0x89abcdefu);

    int32_t truncated = (int32_t)0x10000002aull;
    int32_t extended = (int32_t)(int8_t)-5;
    volatile int64_t signed_value = -7;

    int ok = i32 == 123 && i64 == -987654321ll &&
             f32 == 4.5f && f64 == 3.5 &&
             selected_ptr == (void *)text &&
             pointer_value == 1234 &&
             converted == 0xffffcdedf8dbbd27ull &&
             truncated == 42 && extended == -5 && signed_value < 0;

    printf("[ESPB_DETAIL] select i32=%ld i64=%lld f32x100=%ld f64x100=%lld ptr_eq=%d ptrval=%ld converted=0x%016llx truncated=%ld extended=%ld signedneg=%d\n",
           (long)i32, (long long)i64, (long)(f32 * 100.0f),
           (long long)(f64 * 100.0), selected_ptr == (void *)text,
           (long)pointer_value, (unsigned long long)converted,
           (long)truncated, (long)extended, signed_value < 0);
    printf("[ESPB_GROUP] select_convert=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
