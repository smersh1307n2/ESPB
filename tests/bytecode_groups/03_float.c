#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

JIT_PROBE static float float_math(float a, float b)
{
#pragma clang fp contract(off)
    return (a * b) + (a / b) - (b - a);
}

JIT_HOT static double double_math(double a, double b)
{
    return (a + b) * (a - b) / b;
}

/* Use Clang builtins so fabs/sqrt/min/max lowering is visible in the BC. */
JIT_PROBE static void float_intrinsics(float a, float b, float *out)
{
    out[0] = __builtin_fabsf(a);
    out[1] = __builtin_sqrtf(b * b);
    out[2] = __builtin_fminf(a, b);
    out[3] = __builtin_fmaxf(a, b);
}

JIT_PROBE static void double_intrinsics(double a, double b, double *out)
{
    out[0] = __builtin_fabs(a);
    out[1] = __builtin_sqrt(b * b);
    out[2] = __builtin_fmin(a, b);
    out[3] = __builtin_fmax(a, b);
}

JIT_PROBE static uint32_t float_compare_probe(float a, float b)
{
    return (uint32_t)(a == b) |
           ((uint32_t)(a != b) << 1) |
           ((uint32_t)(a < b) << 2) |
           ((uint32_t)(a > b) << 3) |
           ((uint32_t)(a <= b) << 4) |
           ((uint32_t)(a >= b) << 5);
}

JIT_PROBE static uint32_t double_compare_probe(double a, double b)
{
    return (uint32_t)(a == b) |
           ((uint32_t)(a != b) << 1) |
           ((uint32_t)(a < b) << 2) |
           ((uint32_t)(a > b) << 3) |
           ((uint32_t)(a <= b) << 4) |
           ((uint32_t)(a >= b) << 5);
}

JIT_PROBE static int32_t float_conversion_probe(
    float f, double d, int32_t signed_value, uint32_t unsigned_value)
{
    int32_t from_f64 = (int32_t)d;
    uint32_t from_f32_unsigned = (uint32_t)f;
    float from_i32 = (float)signed_value;
    float from_u32 = (float)unsigned_value;
    double promoted = (double)f;
    float narrowed = (float)d;
    return from_f64 + (int32_t)from_f32_unsigned +
           (int32_t)from_i32 + (int32_t)from_u32 +
           (int32_t)promoted + (int32_t)narrowed;
}

JIT_HOT static int32_t float_convert(float value)
{
    return (int32_t)(value * 4.0f);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile float a = 3.0f;
    volatile float b = 2.0f;
    volatile double x = 5.0;
    volatile double y = 2.0;
    float f = float_math(a, b);
    double d = double_math(x, y);
    int32_t converted = float_convert(3.25f);

    float fops[4] = {0};
    double dops[4] = {0};
    float_intrinsics(-3.0f, 4.0f, fops);
    double_intrinsics(-5.0, 2.0, dops);

    uint32_t fcmp = float_compare_probe(-3.0f, 4.0f);
    uint32_t dcmp = double_compare_probe(5.0, 2.0);
    int32_t conversions = float_conversion_probe(6.75f, -12.5, -17, 19u);

    int ok = (f == 8.5f) && (d == 10.5) && (converted == 13) &&
             fops[0] == 3.0f && fops[1] == 4.0f &&
             fops[2] == -3.0f && fops[3] == 4.0f &&
             dops[0] == 5.0 && dops[1] == 2.0 &&
             dops[2] == -5.0 && dops[3] == 2.0 &&
             fcmp == 22u && dcmp == 42u && conversions == -10 &&
             (a > b) && (x != y);

    printf("[ESPB_GROUP] float=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
