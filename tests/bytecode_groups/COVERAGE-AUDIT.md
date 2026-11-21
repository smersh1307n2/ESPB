# Opcode coverage audit

Coverage is measured from the translated ESPB Code sections, not inferred from C syntax.

## Verified paired artifact

- Branch: `ESPB_WORK`
- Paired translation run: `44861542751` (success), commit `25e79ed04f086efc3b8150c1302deee2f636da42`
- Artifact: `espb-opcode-groups-paired`, 20 modules (10 JIT + 10 interpreter)
- All 20 modules parse cleanly: 782 instructions, 77 distinct raw opcode values, plus 3 distinct `0xFC` extended subopcodes.
- Runtime interpreter dispatch implements 202 raw opcode values. The corpus currently covers 77/202 (38.1%); 125 implemented raw opcode values remain unobserved.
- Extended coverage: `0xFC 0x03` MEMORY.FILL, `0xFC 0x07` HEAP_FREE, `0xFC 0x0B` HEAP_MALLOC. Only 3 of the 15 implemented extended subopcodes are covered.
- Every observed raw opcode is present in the interpreter and in both the Xtensa and RISC-V JIT dispatch implementations.
- Each JIT/interpreter pair has identical function bytecode. The only difference is the HOT flag: all 38 functions are HOT in JIT variants and non-HOT in interpreter variants.

## Confirmed gaps

- Integer: register DIV/REM signed and unsigned, OR/NOT, several shifts, I64 SUB/MUL/OR/NOT and many immediate variants.
- Floating point: MIN/MAX/ABS/SQRT for F32/F64 and most conversion variants; F32 ADD is not present in the emitted corpus.
- Comparisons: most signed/unsigned I32/I64 comparisons and most F32/F64 comparisons.
- Select/conversion: SELECT.F32 and many integer sign/zero extension and truncation variants.
- Memory: STORE.I8/U8/I16/U16/BOOL, LOAD.I8U/I16S/U16/PTR/BOOL, ADDR_OF.
- Atomics: raw RMW opcodes are not covered. The I32 fetch-add/CMPXCHG/load and I64 exchange in the C test are imported `__atomic_*` calls. The only raw atomic opcodes observed are I64 LOAD (`0xEC`) and STORE (`0xED`).
- Extended operations: MEMORY.INIT/COPY, DATA.DROP, ELEM.DROP, TABLE.INIT/COPY/FILL/SIZE/GET/SET, HEAP_REALLOC and HEAP_CALLOC are not covered.

## Important validation issue

The emitted `03_float` function corresponding to the F32 arithmetic helper contains DIV.F32 followed by two SUB.F32 operations, but no MUL.F32 or ADD.F32 for the source expression `(a * b) + (a / b) - (b - a)`. This is a likely translator semantic mismatch and must be confirmed by executing the module before trusting this group's PASS condition.

## Execution status

The paired artifact is generated successfully, but this workflow only translates and uploads modules. The existing QEMU workflow does not consume `tests/bytecode_groups` artifacts and expects exactly three HOT functions per module; these ten groups contain 2–7 functions each. Therefore the 20 new modules have not yet been runtime-tested by the current QEMU workflow.

## Acceptance

Before adding a large number of source cases, connect these paired modules to the dual-mode execution harness and verify semantic results. Then add compact cases for the confirmed opcode gaps. Do not change runtime/JIT implementation based on static speculation alone; preserve the three-call execution limit for the FreeRTOS groups.
