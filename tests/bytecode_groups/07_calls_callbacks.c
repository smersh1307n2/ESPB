#include <stdint.h>
#include <stdio.h>

#define JIT_HOT __attribute__((annotate("espb_jit_hot"), noinline))
#define JIT_PROBE __attribute__((annotate("espb_jit_hot"), noinline, optnone))

typedef int32_t (*binary_callback_t)(int32_t, int32_t, void *);

/* Pseudo-imports recognized by the ESPB translator as TABLE opcodes. */
extern int32_t OP_table_size(int32_t table_idx);
extern void OP_table_init(int32_t table_idx, int32_t elem_seg_idx,
                          int32_t dest, int32_t offset, int32_t size);
extern void OP_table_copy(int32_t table_dest_idx, int32_t table_src_idx,
                          int32_t dest, int32_t src, int32_t size);
extern void OP_table_fill(int32_t table_idx, int32_t dest,
                          int32_t value, int32_t size);
extern int32_t OP_table_get(int32_t table_idx, int32_t index);
extern void OP_table_set(int32_t table_idx, int32_t index, int32_t value);

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

/*
 * TABLE coverage ported from the original all_tests.c. These named
 * pseudo-imports are translated into TABLE.SIZE/INIT/COPY/FILL/GET/SET
 * operations; preserve the calls for opcode auditing.
 */
JIT_PROBE static int32_t table_opcode_probe(void)
{
    int32_t initial_size = OP_table_size(0);
    OP_table_init(0, 0, 5, 0, 2);
    OP_table_fill(0, 0, 1, 3);
    OP_table_copy(0, 0, 7, 0, 2);

    int32_t first_size = OP_table_size(0);
    OP_table_fill(0, 15, 2, 3);
    int32_t expanded_size = OP_table_size(0);

    OP_table_set(0, 0, 0);
    OP_table_set(0, 1, 1);
    OP_table_set(0, 2, 2);
    int32_t index0 = OP_table_get(0, 0);
    int32_t index1 = OP_table_get(0, 1);
    int32_t index2 = OP_table_get(0, 2);

    /*
     * The module table persists across repeated app_main calls. Its initial
     * size is 10 on first entry and may already be expanded on later entries.
     * Test lower bounds and the actual mutations, not a one-shot initial size.
     */
    return initial_size >= 10 && first_size >= 9 && expanded_size >= 18 &&
           index0 == 0 && index1 == 1 && index2 == 2;
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
    int32_t table_ok = table_opcode_probe();

    native_callback_result = 0;
    native_callback2_result = 0;
    host_invoke_cb(native_callback_one, (void *)(uintptr_t)0x2468ace0u);
    host_invoke_cb2(native_callback_two, 12, (void *)(uintptr_t)0x100u);

    int ok = direct == 20 && recursive == 15 &&
             indirect_add == 34 && indirect_mul == 25 &&
             indirect_sub == 18 && table_ok &&
             native_callback_result == (0x2468ace0u ^ 0x13579bdfu) &&
             native_callback2_result == 0x10cu;

    printf("[ESPB_GROUP] calls_callbacks=%s arg=%ld\n", ok ? "PASS" : "FAIL", (long)arg);
    (void)text;
    (void)user_data;
}
