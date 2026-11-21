#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))

typedef struct memory_record {
    int32_t id;
    int16_t value;
    uint8_t flags;
    uint8_t pad;
} memory_record_t;

JIT_HOT static int32_t array_struct_ops(int32_t seed)
{
    memory_record_t records[3];
    int32_t values[4] = { seed, seed + 1, seed + 2, seed + 3 };
    records[0].id = values[0];
    records[0].value = (int16_t)values[1];
    records[0].flags = 3;
    records[1] = records[0];
    records[1].id += values[2];
    return records[1].id + records[1].value + records[1].flags;
}

JIT_HOT static int32_t heap_copy_fill(void)
{
    uint8_t *source = (uint8_t *)malloc(16);
    uint8_t *target = (uint8_t *)malloc(16);
    if (!source || !target) {
        free(source);
        free(target);
        return -1;
    }
    memset(source, 0x2a, 16);
    memcpy(target, source, 16);
    int32_t result = target[0] + target[7] + target[15];
    free(source);
    free(target);
    return result;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int32_t seed = 10;
    int32_t aggregate = array_struct_ops(seed);
    int32_t copied = heap_copy_fill();
    int ok = aggregate == 36 && copied == 126;
    printf("[ESPB_GROUP] memory=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
}
