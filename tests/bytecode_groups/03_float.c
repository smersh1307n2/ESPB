#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

JIT_HOT static float float_math(float a, float b)
{
    return (a * b) + (a / b) - (b - a);
}

JIT_HOT static double double_math(double a, double b)
{
    return (a + b) * (a - b) / b;
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
    int ok = (f == 6.5f) && (d == 10.5) && (converted == 13) &&
             (a > b) && (x != y) && (text == text) && (user_data == user_data);
    printf("[ESPB_GROUP] float=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
