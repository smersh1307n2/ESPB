# ESPB opcode-family test groups

These are compact translator inputs, not checked-in runtime modules. Each source is compiled separately to a small LLVM bitcode module, then translated to ESPB. Do not concatenate groups into one large module.

## Paired execution modes

Every function defined in every source file is marked JIT_HOT (including helpers, callbacks, task/timer callbacks and app_main). The JIT corpus variant keeps HOT on every function. The interpreter variant is derived from the exact same translated module by clearing HOT flags from all function bodies. No source logic, input or expected result changes between variants.

## Initial groups

| Group | Coverage |
|---|---|
| 01 integer32 | I32 arithmetic, signed division/remainder, comparisons, bitwise operations and shifts |
| 02 integer64 | I64 arithmetic, signed/unsigned division and remainder, shifts and truncation |
| 03 float | F32/F64 arithmetic, comparisons, fabs/sqrt/min/max and integer conversions; F32 contraction is disabled locally in the probe |
| 04 control_flow | conditional branches, switch/table dispatch, for/while/do-while, break/continue, nested loops |
| 05 select_convert | SELECT for scalar types, sign/zero extension, truncation and pointer/integer conversions |
| 06 memory | scalar loads/stores, arrays, structs, pointers, globals, malloc/free, copy/fill |
| 07 calls_callbacks | nested direct calls, recursion, indirect calls through function pointers and callback context |
| 08 atomics | I32 atomic RMW/CMPXCHG/load/store under a group-only RV32 A target; I64 remains a helper-lowering control |
| 09 freertos_task | task creation, argument passing, task callback and completion |
| 10 freertos_timer | timer creation/start/expiry callback/stop/delete and timer ID |
| 11 fibonacci | Existing iterative Fibonacci(85) algorithm and exact 64-bit expected result; checked in both JIT and interpreter modes |

## Rules

- Keep each group self-contained and deliberately small; split it if its generated native code approaches the measured JIT RAM budget.
- Every test has a deterministic success condition. RTOS groups are integration tests and should be run separately from tight execution microbenchmarks. Invoke each group execution function only three times per test pass; do not add long repetition loops.
- Do not add long delays, unbounded loops, verbose per-operation printing, or arbitrary-address calls to the regular suite.
- The build/translation workflow must generate paired interpreter and HOT/JIT modules from the same source bitcode. Fibonacci uses the existing iterative algorithm from `ESP32_PRJ_TO_LLVM/main_2/fibonacci.c`; the ESP-IDF timing wrappers are excluded from the standalone QEMU module, while the input N and expected value are unchanged. The checked-in ESPB corpus is not regenerated as part of ordinary runtime tests.
- Coverage is measured across four distinct stages: C source, optimized LLVM IR/bitcode, translated ESPB opcode stream and dynamic execution. An operation written in C may be folded, reassociated, lowered to another form or removed before translation; the ESPB translator also performs its own optimization passes. A group is complete only when the intended opcode is present in the translated stream and its path actually executes in both modes.
- Do not assume noinline preserves individual operations or volatile forces a particular arithmetic opcode. Use runtime-dependent inputs and observable results, then inspect the LLVM IR and emitted ESPB before classifying a gap.
- The bitcode builder changes `-march` only for `08_atomics.c` to `rv32imac_zicsr_zifencei`, solely to make 32-bit atomic builtins lower to LLVM atomic operations. Other groups retain the standard ESP-IDF target flags; this is a test-only per-source override, not a global ESP-IDF or firmware setting.
- Keep normal optimized coverage separate from narrowly targeted opcode probes; document any local optimization controls used by a probe.
- Do not modify ESP-IDF configuration, clock, compiler/linker settings, or code/data placement for these tests.

The first pass is a broad coverage baseline. A follow-up opcode audit must map every opcode in the runtime specification to at least one group and identify opcodes that cannot be emitted naturally by the current translator.


The paired `11_fibonacci` group is the Fibonacci coverage for the QEMU dual-mode suite: it runs once with HOT/JIT and once with HOT cleared in the interpreter variant. The legacy JIT-only Fibonacci in `main/test.espb` is not a substitute for this paired group.
