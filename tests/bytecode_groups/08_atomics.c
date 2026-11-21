#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

/*
 * This is a dedicated RV32 A-extension bitcode probe. The bitcode builder
 * applies -march=rv32imac_zicsr_zifencei to this source file only. The normal
 * ESP-IDF compile target and all other corpus groups remain rv32imc.
 *
 * Keeping optnone here prevents the operation sequence from being folded.
 */
JIT_PROBE static int32_t atomic_i32(void)
{
    volatile int32_t value = 10;

    int32_t old_add = __atomic_fetch_add(&value, 5, __ATOMIC_SEQ_CST);
    int32_t old_sub = __atomic_fetch_sub(&value, 3, __ATOMIC_SEQ_CST);
    int32_t old_and = __atomic_fetch_and(&value, 0x0f, __ATOMIC_SEQ_CST);
    int32_t old_or = __atomic_fetch_or(&value, 0x20, __ATOMIC_SEQ_CST);
    int32_t old_xor = __atomic_fetch_xor(&value, 0x03, __ATOMIC_SEQ_CST);
    int32_t old_exchange = __atomic_exchange_n(&value, 42, __ATOMIC_SEQ_CST);

    int32_t expected = 42;
    int success = __atomic_compare_exchange_n(&value, &expected, 100, 0,
                                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    expected = 99;
    int failure = __atomic_compare_exchange_n(&value, &expected, 200, 0,
                                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    int32_t loaded = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
    __atomic_store_n(&value, 77, __ATOMIC_SEQ_CST);
    int32_t stored = __atomic_load_n(&value, __ATOMIC_SEQ_CST);

    return old_add + old_sub + old_and + old_or + old_xor +
           old_exchange + success + failure + loaded + stored;
}

/* RV32 I64 atomics are intentionally retained as a helper-lowering control. */
JIT_PROBE static int64_t atomic_i64(void)
{
    volatile int64_t value = 1000;
    int64_t old_sub = __atomic_fetch_sub(&value, 100, __ATOMIC_SEQ_CST);
    int64_t old_exchange = __atomic_exchange_n(&value, 5555, __ATOMIC_SEQ_CST);
    __atomic_store_n(&value, 77, __ATOMIC_SEQ_CST);
    int64_t loaded = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
    return old_sub + old_exchange + loaded;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    int32_t i32_result = atomic_i32();
    int64_t i64_result = atomic_i64();
    int ok = i32_result == 318 && i64_result == 1977;
    printf("[ESPB_GROUP] atomics=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
