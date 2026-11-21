#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#if defined(__clang__) && defined(__riscv)
#define JIT_HOT_ATOMIC __attribute__((annotate("espb_jit_hot"), noinline, optnone, target("arch=+a")))
#else
#define JIT_HOT_ATOMIC JIT_HOT
#endif

/*
 * The CI translation unit is built for rv32imc (no A extension). Enable A
 * only for this probe function so Clang retains native LLVM atomicrmw /
 * cmpxchg IR. This is a per-function test attribute, not a global -march
 * change to ESP-IDF or to the rest of the corpus.
 */
JIT_HOT_ATOMIC static int32_t atomic_i32(void)
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

    return old_add + old_sub + old_and + old_or + old_xor + old_exchange +
           success + failure + loaded;
}

/*
 * On RV32, 64-bit atomics are not guaranteed lock-free even with the A
 * extension. Keep this case as a semantic/helper-lowering control: its LLVM
 * IR is inspected separately and it is not counted as raw ESPB atomic-opcode
 * coverage unless the translated Code section actually contains such opcodes.
 */
JIT_HOT __attribute__((optnone)) static int64_t atomic_i64(void)
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
    int ok = i32_result == 241 && i64_result == 1977;
    printf("[ESPB_GROUP] atomics=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
