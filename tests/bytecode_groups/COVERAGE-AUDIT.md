# Opcode coverage audit

Coverage is measured from decoded ESPB Code sections, not inferred from C syntax. Numbers below are pinned to one historical paired artifact and must not be presented as a live count for later corpora.

## Artifact basis

- Branch: `ESPB_WORK`
- Paired translation run: `44861542751` (success), producer commit `25e79ed04f086efc3b8150c1302deee2f636da42`
- Artifact: `espb-opcode-groups-paired`, 20 modules (10 JIT + 10 Interpreter)
- All 20 modules parse cleanly: 782 instructions, 77 distinct raw opcode values, plus 3 distinct `0xFC` extended subopcodes.
- Runtime interpreter dispatch implements 202 raw opcode values. This artifact covers 77/202 (38.1%); 125 implemented raw opcode values remain unobserved in this artifact.
- Extended coverage: `0xFC 0x03` MEMORY.FILL, `0xFC 0x07` HEAP_FREE, `0xFC 0x0B` HEAP_MALLOC. Only 3 of 15 implemented extended subopcodes are covered.
- Every observed raw opcode is present in the interpreter and in both Xtensa and RISC-V JIT dispatch implementations.
- Each JIT/Interpreter pair has identical function bytecode. The only difference is the HOT flag: all 38 functions are HOT in JIT variants and non-HOT in Interpreter variants.

## Confirmed gaps

- Integer: register DIV/REM signed and unsigned, OR/NOT, several shifts, I64 SUB/MUL/OR/NOT and many immediate variants.
- Floating point: MIN/MAX/ABS/SQRT for F32/F64 and most conversion variants; F32 ADD is not present in the emitted corpus.
- Comparisons: most signed/unsigned I32/I64 comparisons and most F32/F64 comparisons.
- Select/conversion: SELECT.F32 and many integer sign/zero extension and truncation variants.
- Memory: STORE.I8/U8/I16/U16/BOOL, LOAD.I8U/I16S/U16/PTR/BOOL, ADDR_OF.
- Atomics: raw RMW opcodes are not covered. The I32 fetch-add/CMPXCHG/load and I64 exchange in the C test are imported `__atomic_*` calls. The only raw atomic opcodes observed are I64 LOAD (`0xEC`) and STORE (`0xED`).
- Extended operations: MEMORY.INIT/COPY, DATA.DROP, ELEM.DROP, TABLE.INIT/COPY/FILL/SIZE/GET/SET, HEAP_REALLOC and HEAP_CALLOC are not covered.
- Native-to-ESPB callback bridge: the test group currently covers internal bytecode function pointers and recursion. It does not yet prove that native `host_invoke_cb` / `host_invoke_cb2` successfully invoke ESPB callbacks through the registered callback metadata path.

## Translator semantic warning

The emitted `03_float` function corresponding to the F32 arithmetic helper contains DIV.F32 followed by two SUB.F32 operations, but no MUL.F32 or ADD.F32 for the source expression `(a * b) + (a / b) - (b - a)`. Treat this as a translator semantic discrepancy to reproduce and investigate; do not trust the group's PASS marker until emitted code and runtime result are both independently checked.

## Runtime execution status

- QEMU run `44864781309` (head `513e582a7567b32a7ec54d6aa65353a494a78066`) completed with failure on both targets at the first Interpreter module `interp__01_integer32.espb`, before any opcode-group PASS or benchmark record. Xtensa reported LoadProhibited; ESP32-C3 reported Load access fault at `espb_call_function`, `runtime_oc.c:4340`.
- That run used the old pinned artifact from run `44861542751`, which predates the integer32 source correction `aba89fff64631a12a85da5ab63d54e058c5f640f` and the result-log newline fix `ae3e2a6925aa937aaf213a57fd7a5bb3933c96c6`. Therefore it does not verify the corrected source corpus.
- The QEMU workflow has since been changed to discover a successful paired-translator run and require a provenance manifest whose source, translator, bitcode and paired-module hashes match. Legacy artifacts without that manifest are intentionally rejected.
- A later QEMU run `44868439987` was started from workflow revision `114092dcf118209c5b10f70bc0a81c8247bdca85`, before the new provenance-gated QEMU workflow commit was in the branch. It downloaded the legacy pinned artifact and must not be counted as a run of the new provenance gate. Its final result should be recorded separately when it completes.
- As of the provenance-gate implementation, no new paired translator artifact with `manifest.json` has yet been confirmed. The manual translation workflow remains quota-aware; QEMU correctness is therefore not yet certified by a fresh artifact.

## Audit rules

1. Report counts with the exact producer run ID and manifest source commit.
2. Distinguish implemented opcode count, statically emitted opcode count, dynamically executed opcode paths and semantic assertion coverage.
3. Do not count source syntax as opcode coverage; verify the actual Code section. For example, compiler `__atomic_*` builtins may resolve to imported calls rather than raw atomic opcodes.
4. Keep static opcode audit outside the timed regions. If runtime instrumentation is added, use a distinct coverage profile.
5. Refresh this audit whenever the corpus, translator revision, or opcode implementation changes.

## Acceptance

Before adding a large number of source cases, establish a fresh provenance-bound paired artifact and get a minimal Interpreter/JIT smoke pair through both Xtensa and RISC-V QEMU. Then verify semantics and expand the corpus against confirmed opcode gaps. Do not change runtime/JIT implementation based on static speculation alone. Preserve the current three-call execution limit for FreeRTOS groups.
