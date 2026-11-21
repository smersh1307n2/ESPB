# Opcode coverage audit

Coverage is measured from decoded ESPB Code sections, not inferred from C syntax. C source, optimized LLVM IR, translated ESPB and dynamic execution are separate evidence layers. Numbers below are pinned to one historical paired artifact and must not be presented as a live count for later corpora.

## Artifact basis

- Branch: `ESPB_WORK`
- Paired translation run: `44861542751` (success), producer commit `25e79ed04f086efc3b8150c1302deee2f636da42`
- Artifact: `espb-opcode-groups-paired`, 20 modules (10 JIT + 10 Interpreter)
- All 20 modules parse cleanly: 782 instructions, 77 distinct raw opcode values, plus 3 distinct `0xFC` extended subopcodes.
- Runtime interpreter dispatch implements 202 raw opcode values. This artifact covers 77/202 (38.1%); 125 implemented raw opcode values remain unobserved in this artifact.
- Extended coverage: `0xFC 0x03` MEMORY.FILL, `0xFC 0x07` HEAP_FREE, `0xFC 0x0B` HEAP_MALLOC. Only 3 of 15 implemented extended subopcodes are covered.
- Every observed raw opcode is present in the interpreter and in both Xtensa and RISC-V JIT dispatch implementations.
- Each JIT/Interpreter pair has identical function bytecode. The only difference is the HOT flag: all 38 functions are HOT in JIT variants and non-HOT in Interpreter variants.

## Pre-translation bitcode spot-check

A successful bitcode-only build is available without spending translator quota:

- Bitcode workflow run: `44869019337` (success), branch `ESPB_WORK`, source commit `24049ced8f2ab7a43f0c903abee6d24130a6d387`.
- Artifact: `espb-opcode-groups-bc`, artifact ID `14693600365`; ten non-empty .bc files.
- LLVM module target for the inspected groups: `riscv32-esp-unknown-elf`.
- Build environment: ESP-IDF v5.4.1 and Espressif Clang `esp-18.1.2_20240912`.
- Effective compile target for these groups: `--target=riscv32-esp-elf -march=rv32imc_zicsr_zifencei -mabi=ilp32 -Og`. This is `-Og`, not an unoptimized `-O0` build. The captured compile command contains no explicit `-ffast-math` or `-ffp-contract` switch; do not infer a non-default FP contraction policy from that absence.
- Debug compile-unit metadata marks the module `isOptimized=true`.
- `03_float.bc` SHA-256: `e7dd468a25e83aeddb1e109f367ff4ac9bb307c8327e0a19ee8fe29e39a98fdb`.
- `08_atomics.bc` SHA-256: `947b5cf0682bc035607f04297169365117f1161a97af4779dc8e0428021fd539`.

The inspected `float_math` LLVM function contains `fdiv`, a call to `llvm.fmuladd.f32`, two `fsub` operations and `ret`. It does not contain standalone `fmul` or `fadd`. The same module still contains separate F64 `fadd`, `fsub`, `fmul`, `fdiv` operations in `double_math`, and F32 `fmul` plus `fptosi` in `float_convert`. This confirms that the frontend/bitcode stage has already contracted the F32 multiply-plus-add expression into an LLVM fused-multiply-add intrinsic. It does not yet tell us how the translator handles that intrinsic because this bitcode-only artifact has no paired ESPB result.

The inspected `08_atomics.bc` functions call `__atomic_fetch_add_4`, `__atomic_compare_exchange_4`, `__atomic_load_4`, `__atomic_exchange_8`, `__atomic_store_8` and `__atomic_load_8`. There are no LLVM `atomicrmw`, `cmpxchg` or `fence` instructions in either helper function. The RISC-V target feature string contains `-a` (the A/atomic extension is disabled), consistent with the frontend leaving these builtins as helper calls instead of emitting RISC-V atomic instructions. The source-level atomic calls therefore cannot be used as evidence of raw ESPB atomic-opcode coverage.

This spot-check is pre-translation evidence only. Do not merge its LLVM operation counts into the historical ESPB emitted-opcode counts above. The next translator-quota use for this question should be a single targeted translation of this exact `03_float.bc`, not a new ten-group corpus run. That will determine whether `llvm.fmuladd.f32` is correctly lowered, expanded or lost by the translator.

## Confirmed gaps

- Integer: register DIV/REM signed and unsigned, OR/NOT, several shifts, I64 SUB/MUL/OR/NOT and many immediate variants.
- Floating point: MIN/MAX/ABS/SQRT for F32/F64 and most conversion variants; F32 ADD is not present in the emitted corpus.
- Comparisons: most signed/unsigned I32/I64 comparisons and most F32/F64 comparisons.
- Select/conversion: SELECT.F32 and many integer sign/zero extension and truncation variants.
- Memory: STORE.I8/U8/I16/U16/BOOL, LOAD.I8U/I16S/U16/PTR/BOOL, ADDR_OF.
- Atomics: raw RMW opcodes are not covered. The I32 fetch-add/CMPXCHG/load and I64 exchange in the C test are imported `__atomic_*` calls. The only raw atomic opcodes observed are I64 LOAD (`0xEC`) and STORE (`0xED`).
- Extended operations: MEMORY.INIT/COPY, DATA.DROP, ELEM.DROP, TABLE.INIT/COPY/FILL/SIZE/GET/SET, HEAP_REALLOC and HEAP_CALLOC are not covered.
- Native-to-ESPB callback bridge: `07_calls_callbacks.c` tests internal bytecode function pointers and recursion. It does not yet prove native `host_invoke_cb` / `host_invoke_cb2` calls invoke ESPB callbacks through CBMETA. Keep that as an explicit gap until the emitted metadata and both engine paths pass.

## Source / LLVM / translator lowering warning

The emitted `03_float` function corresponding to the F32 arithmetic helper contains DIV.F32 followed by two SUB.F32 operations, but no MUL.F32 or ADD.F32 for source expression `(a * b) + (a / b) - (b - a)`. This is an unresolved source-to-ESPB lowering discrepancy, not yet proof of a translator defect.

The actual bitcode build inherits the ESP-IDF CMake compile command, so frontend optimization and floating-point flags may already have transformed the source before translation. The ESPB translation request then enables additional LLVM optimizations, call-result optimization, alloca optimization, register patching, peephole optimization and compaction. To attribute this observation correctly, inspect the same function in disassembled LLVM IR from the exact .bc used for translation, then compare it with the decoded ESPB Code section:
- If fmul/fadd are already absent from LLVM IR, investigate Clang lowering, effective optimization flags, FP options and test construction.
- If fmul/fadd remain in LLVM IR but disappear from ESPB, investigate translator optimization/lowering.
- If the ESPB instructions exist but are not executed, fix dynamic path coverage rather than changing the translator.
- If they execute but the result is wrong, investigate runtime opcode semantics and expected-value calculation.

Do not infer the exact arithmetic opcode sequence from the C expression, and do not spend translator quota until the pre-translation IR and relevant compile options have been checked. The existing manifest binds the .bc SHA-256, but the current report does not yet retain the exact per-source compile command/options record.

## Runtime execution status

- QEMU run `44864781309` (workflow revision at head `513e582a7567b32a7ec54d6aa65353a494a78066`) failed on both Xtensa and RISC-V at the first Interpreter module `interp__01_integer32.espb`, before any group PASS or benchmark. Xtensa reported LoadProhibited; ESP32-C3 reported Load access fault at `espb_call_function`, `runtime_oc.c:4340`.
- QEMU run `44868439987` (workflow revision/head `114092dcf118209c5b10f70bc0a81c8247bdca85`) also failed on both targets at the same first Interpreter module and before semantic assertions or timing. This revision still used the fixed legacy artifact run `44861542751`.
- Artifact run `44861542751` predates integer32 source fixes `aba89fff64631a12a85da5ab63d54e058c5f640f` and `ae3e2a6925aa937aaf213a57fd7a5bb3933c96c6`. These legacy QEMU failures do not verify the corrected source corpus.
- Paired translator run `44864453025` built all ten LLVM bitcode groups successfully, but the translation step failed before producing paired ESPB modules: translator reported all configured API key slots were quota-exhausted. No artifact with the new provenance manifest was uploaded.
- The current QEMU workflow selects a completed successful paired-translator run and verifies the producer run ID, exact `head_sha`, current source hashes, translator submodule SHA, bitcode hashes and both bytecode hashes before runtime execution. It intentionally rejects legacy paired artifacts without `manifest.json`.
- Latest verified host-tool CI: run `44868924641` completed successfully with 9/9 tests. This verifies host-side helpers and QEMU workflow-selection contracts, not target execution.
- The separate bitcode-only workflow succeeded on run `44868439990` and built ten non-empty bitcode groups. It does not use translator API quota.
- No current QEMU runtime PASS has yet been established for a fresh manifest-bearing artifact.

## Audit rules

1. Report every coverage count with exact producer run ID and manifest source commit.
2. Distinguish implemented opcode count, statically emitted opcode count, dynamically executed opcode paths and semantic assertion coverage.
3. Do not count source syntax as opcode coverage; inspect pre-translation LLVM IR, the emitted ESPB Code section and actual runtime paths. Compiler `__atomic_*` builtins may become LLVM atomics, target-specific sequences or imported calls rather than raw atomic opcodes.
4. Keep static opcode audit outside timed regions. If runtime instrumentation is added, use a distinct coverage profile.
5. Refresh this audit whenever the corpus, translator revision, or opcode implementation changes.

## Acceptance

Before adding many source cases, generate a fresh provenance-bound paired artifact and get a minimal Interpreter/JIT smoke pair through both Xtensa and RISC-V QEMU. Then verify semantic results and expand the corpus against confirmed opcode gaps. Do not change runtime/JIT implementation based on static speculation alone. Preserve the current three-call execution limit for FreeRTOS groups.
