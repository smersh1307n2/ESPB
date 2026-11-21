#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

typedef int32_t (*binary_callback_t)(int32_t, int32_t, void *);

extern void host_invoke_cb(void (*callback)(void *), void *user_data);
extern void host_invoke_cb2(void (*callback)(int32_t, void *),
                            int32_t value, void *user_data);

static volatile uint32_t native_callback_result;
static volatile uint32_t native_callback2_result;

JIT_HOT static int32_t add_inner(int32_t a, int32_t b)
{
    return a + b;
}

JIT_HOT static int32_t multiply_inner(int32_t a, int32_t b)
{
    return a * b;
}

JIT_HOT static int32_t subtract_inner(int32_t a, int32_t b)
{
    return a - b;
}

JIT_HOT static int32_t call_chain(int32_t value)
{
    return multiply_inner(add_inner(value, 3), 2);
}

JIT_PROBE static int32_t recursive_sum(int32_t value)
{
    return value <= 0 ? 0 : value + recursive_sum(value - 1);
}

JIT_HOT static int32_t callback_add(int32_t a, int32_t b, void *context)
{
    return a + b + (int32_t)(intptr_t)context;
}

JIT_HOT static int32_t callback_multiply(int32_t a, int32_t b, void *context)
{
    return a * b + (int32_t)(intptr_t)context;
}

JIT_HOT static int32_t callback_subtract(int32_t a, int32_t b, void *context)
{
    return a - b + (int32_t)(intptr_t)context;
}

static binary_callback_t callback_table[3] = {
    callback_add, callback_multiply, callback_subtract
};

/* A runtime-selected function-table entry must remain an indirect call. */
JIT_PROBE static int32_t call_callback_by_index(
    int32_t index, int32_t a, int32_t b)
{
    uint32_t slot = (uint32_t)index % 3u;
    binary_callback_t callback = callback_table[slot];
    return callback(a, b, (void *)(intptr_t)4);
}

JIT_HOT static void native_callback_one(void *user_data)
{
    native_callback_result = (uint32_t)(uintptr_t)user_data ^ 0x13579bdfu;
}

JIT_HOT static void native_callback_two(int32_t value, void *user_data)
{
    native_callback2_result =
        (uint32_t)value + (uint32_t)(uintptr_t)user_data;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    int32_t direct = call_chain(7);
    int32_t recursive = recursive_sum(5);
    int32_t indirect_add = call_callback_by_index(0, 10, 20);
    int32_t indirect_mul = call_callback_by_index(1, 7, 3);
    int32_t indirect_sub = call_callback_by_index(2, 19, 5);

    native_callback_result = 0;
    native_callback2_result = 0;
    host_invoke_cb(native_callback_one, (void *)(uintptr_t)0x2468ace0u);
    host_invoke_cb2(native_callback_two, 12, (void *)(uintptr_t)0x100u);

    int ok = direct == 20 && recursive == 15 &&
             indirect_add == 34 && indirect_mul == 25 &&
             indirect_sub == 18 &&
             native_callback_result == (0x2468ace0u ^ 0x13579bdfu) &&
             native_callback2_result == 0x10cu;

    printf("[ESPB_GROUP] calls_callbacks=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
