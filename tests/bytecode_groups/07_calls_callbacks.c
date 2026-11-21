#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

typedef int32_t (*binary_callback_t)(int32_t, int32_t, void *);

JIT_HOT static int32_t add_inner(int32_t a, int32_t b)
{
    return a + b;
}

JIT_HOT static int32_t multiply_inner(int32_t a, int32_t b)
{
    return a * b;
}

JIT_HOT static int32_t call_chain(int32_t value)
{
    return multiply_inner(add_inner(value, 3), 2);
}

JIT_HOT static int32_t recursive_sum(int32_t value)
{
    return value <= 0 ? 0 : value + recursive_sum(value - 1);
}

JIT_HOT static int32_t callback_add(int32_t a, int32_t b, void *context)
{
    return a + b + (int32_t)(intptr_t)context;
}

JIT_HOT static int32_t invoke_callback(binary_callback_t callback, void *context)
{
    return callback(10, 20, context);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    binary_callback_t table[2] = { callback_add, callback_add };
    int32_t direct = call_chain(7);
    int32_t recursive = recursive_sum(5);
    int32_t indirect = invoke_callback(table[1], (void *)(intptr_t)4);
    int ok = direct == 20 && recursive == 15 && indirect == 34;

    printf("[ESPB_GROUP] calls_callbacks=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
