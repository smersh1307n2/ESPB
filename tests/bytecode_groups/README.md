# ESPB opcode-family test groups

These are compact translator inputs, not checked-in runtime modules. Each source is compiled separately to a small LLVM bitcode module, then translated to ESPB. Do not concatenate groups into one large module.

## Paired execution modes

Every function defined in every source file is marked JIT_HOT (including helpers, callbacks, task/timer callbacks and app_main). The JIT corpus variant keeps HOT on every function. The interpreter variant is derived from the exact same translated module by clearing HOT flags from all function bodies. No source logic, input or expected result changes between variants.

## Initial groups

| Group | Coverage |
|---|---|
| 01 integer32 | I32 arithmetic, signed division/remainder, comparisons, bitwise operations and shifts |
| 02 integer64 | I64 arithmetic, signed/unsigned division and remainder, shifts and truncation |
| 03 float | F32/F64 arithmetic, comparisons and integer conversions |
| 04 control_flow | conditional branches, switch/table dispatch, for/while/do-while, break/continue, nested loops |
| 05 select_convert | SELECT for scalar types, sign/zero extension, truncation and pointer/integer conversions |
| 06 memory | scalar loads/stores, arrays, structs, pointers, globals, malloc/free, copy/fill |
| 07 calls_callbacks | nested direct calls, recursion, indirect calls through function pointers and callback context |
| 08 atomics | I32/I64 atomic load/store/add/exchange/compare-exchange |
| 09 freertos_task | task creation, argument passing, task callback and completion |
| 10 freertos_timer | timer creation/start/expiry callback/stop/delete and timer ID |

## Rules

- Keep each group self-contained and deliberately small; split it if its generated native code approaches the measured JIT RAM budget.
- Every test has a deterministic success condition. RTOS groups are integration tests and should be run separately from tight execution microbenchmarks.
- Do not add long delays, unbounded loops, verbose per-operation printing, or arbitrary-address calls to the regular suite.
- The build/translation workflow must generate paired interpreter and HOT/JIT modules from the same source bitcode. The checked-in ESPB corpus is not regenerated as part of ordinary runtime tests.
- Coverage is measured from the translated ESPB opcode stream, not inferred from C syntax. A group is complete only when the intended opcodes are present and actually execute in both modes.
- Do not modify ESP-IDF configuration, clock, compiler/linker settings, or code/data placement for these tests.

The first pass is a broad coverage baseline. A follow-up opcode audit must map every opcode in the runtime specification to at least one group and identify opcodes that cannot be emitted naturally by the current translator.
