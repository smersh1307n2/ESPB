#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

typedef struct memory_record {
    int32_t id;
    int16_t value;
    uint8_t flags;
    uint8_t pad;
} memory_record_t;

static const uint8_t memory_seed_data[8] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
};

JIT_PROBE static int32_t array_struct_ops(int32_t seed)
{
    memory_record_t records[3];
    int32_t values[4] = { seed, seed + 1, seed + 2, seed + 3 };
    records[0].id = values[0];
    records[0].value = (int16_t)values[1];
    records[0].flags = 3;
    records[1] = records[0];
    records[1].id += values[2];
    records[2] = records[1];
    return records[2].id + records[2].value + records[2].flags;
}

/* Volatile typed accesses keep narrow, BOOL and pointer loads/stores alive. */
JIT_PROBE static int32_t scalar_memory_probe(int32_t seed)
{
    volatile int8_t i8 = -7;
    volatile uint8_t u8 = 250;
    volatile int16_t i16 = -1234;
    volatile uint16_t u16 = 65000;
    volatile bool flag = (seed != 0);
    int32_t local = seed + 17;
    int32_t * volatile pointer = &local;

    int8_t r_i8 = i8;
    uint8_t r_u8 = u8;
    int16_t r_i16 = i16;
    uint16_t r_u16 = u16;
    bool r_flag = flag;
    int32_t *loaded_pointer = pointer;
    int32_t r_local = *loaded_pointer;

    return (int32_t)r_i8 + (int32_t)r_u8 +
           (int32_t)r_i16 + (int32_t)r_u16 +
           (int32_t)r_flag + r_local +
           (loaded_pointer == &local);
}

JIT_PROBE static int32_t heap_copy_fill(void)
{
    uint8_t *source = (uint8_t *)malloc(16);
    uint8_t *target = (uint8_t *)calloc(16, 1);
    if (!source || !target) {
        free(source);
        free(target);
        return -1;
    }

    memset(source, 0x2a, 16);
    memcpy(target, source, 16);
    memcpy(target + 8, memory_seed_data, sizeof(memory_seed_data));

    uint8_t *grown = (uint8_t *)realloc(target, 24);
    if (!grown) {
        free(source);
        free(target);
        return -2;
    }

    memset(grown + 16, 0x11, 8);
    int32_t result = grown[0] + grown[7] + grown[15] +
                     grown[16] + grown[23] + grown[8];
    free(source);
    free(grown);
    return result;
}

JIT_HOT void app_main(int32_t arg, const char *text, void *user_data)
{
    volatile int32_t seed = 10;
    int32_t aggregate = array_struct_ops(seed);
    int32_t scalar = scalar_memory_probe(seed);
    int32_t copied = heap_copy_fill();

    int ok = aggregate == 36 && scalar == 64038 &&
             copied == (42 + 42 + 0x80 + 0x11 + 0x11 + 0x10);
    printf("[ESPB_GROUP] memory=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
