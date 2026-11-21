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
    const int32_t switch_two = branch_switch(2);
    const int32_t switch_negative = branch_switch(-1);
    const int32_t switch_default = branch_switch(9);
    const int32_t mixed_loop = loop_mix(10);
    const int32_t nested = nested_loop();
    int ok = switch_two == 13 && switch_negative == -1 &&
             switch_default == 19 && mixed_loop == 25 && nested == 9;
    printf("[ESPB_DETAIL] control switch2=%ld switchneg=%ld switchdefault=%ld loop=%ld nested=%ld\n",
           (long)switch_two, (long)switch_negative, (long)switch_default,
           (long)mixed_loop, (long)nested);
    printf("[ESPB_GROUP] control_flow=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
