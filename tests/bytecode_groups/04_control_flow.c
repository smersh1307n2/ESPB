#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

JIT_HOT static int32_t branch_switch(int32_t value)
{
    if (value < 0) return -1;
    if (value == 0) return 7;
    switch (value) {
        case 1: return 11;
        case 2: return 13;
        case 3: return 17;
        default: return 19;
    }
}

JIT_HOT static int32_t loop_mix(int32_t limit)
{
    int32_t sum = 0;
    for (int32_t i = 0; i < limit; ++i) {
        if ((i & 1) == 0) continue;
        sum += i;
        if (sum > 20) break;
    }
    int32_t n = 0;
    while (n < 2) ++n;
    do { --n; } while (n > 0);
    return sum + n;
}

JIT_HOT static int32_t nested_loop(void)
{
    int32_t count = 0;
    for (int32_t i = 0; i < 3; ++i)
        for (int32_t j = 0; j < 2; ++j)
            count += (i + j);
    return count;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    int ok = branch_switch(2) == 13 && branch_switch(-1) == -1 &&
             branch_switch(9) == 19 && loop_mix(10) == 26 &&
             nested_loop() == 9 && (text == text) && (user_data == user_data);
    printf("[ESPB_GROUP] control_flow=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
