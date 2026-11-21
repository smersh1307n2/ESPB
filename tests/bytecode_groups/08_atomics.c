#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

JIT_HOT static int32_t atomic_i32(void)
{
    volatile int32_t value = 10;
    int32_t old = __atomic_fetch_add(&value, 5, __ATOMIC_SEQ_CST);
    int32_t expected = 15;
    int exchanged = __atomic_compare_exchange_n(&value, &expected, 42, 0,
                                                 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    int32_t loaded = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
    return old + loaded + exchanged;
}

JIT_HOT static int64_t atomic_i64(void)
{
    volatile int64_t value = 1000;
    int64_t old = __atomic_exchange_n(&value, 5555, __ATOMIC_SEQ_CST);
    __atomic_store_n(&value, 77, __ATOMIC_SEQ_CST);
    return old + __atomic_load_n(&value, __ATOMIC_SEQ_CST);
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    int32_t a = atomic_i32();
    int64_t b = atomic_i64();
    int ok = a == 53 && b == 1077 && (text == text) && (user_data == user_data);
    printf("[ESPB_GROUP] atomics=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
