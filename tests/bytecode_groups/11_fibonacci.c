#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

/* Existing iterative Fibonacci benchmark algorithm from
 * ESP32_PRJ_TO_LLVM/main_2/fibonacci.c. Keep the same N and expected value;
 * omit only ESP-IDF timing/console helpers so this module is self-contained
 * in the ESPB dual-mode QEMU runner.
 */
JIT_HOT static uint64_t fibonacci_iterative(int n)
{
    if (n <= 1) {
        return (uint64_t)n;
    }
    uint64_t a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        uint64_t temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

#define FIBONACCI_N 85
#define FIBONACCI_N85_EXPECTED 259695496911122585ULL

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    (void)arg;
    (void)text;
    (void)user_data;

    const uint64_t result = fibonacci_iterative(FIBONACCI_N);
    const int ok = result == FIBONACCI_N85_EXPECTED;
    /* Keep only the runner's semantic marker; avoid the extra 64-bit printf. */
    printf("[ESPB_GROUP] fibonacci=%s\n", ok ? "PASS" : "FAIL");
}
