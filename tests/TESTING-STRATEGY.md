# ESPB testing architecture

This document defines how ESPB tests are layered and what each result proves. A green host test does not prove target execution; a successful firmware build does not prove bytecode semantics; a QEMU pass does not replace hardware benchmarking.

## 1. Test profiles

| Profile | Executes | Proves | Does not prove |
|---|---|---|---|
| Host tools | Python tools, format helpers, manifest and pair logic | Tool correctness, boundary checks, reproducibility gates | Runtime opcode semantics |
| LLVM corpus | Each isolated C/C++ group through ESP-IDF Clang | The selected sources build into bitcode | Translator output or runtime behavior |
| Translator | LLVM bitcode through ESPB translator | Bytecode artifact generation and structural validity | Correct execution |
| Paired runtime | Same ESPB functions in Interpreter (HOT clear) and JIT (HOT set) | Semantic behavior and engine parity on a target | Physical-board performance |
| ABI / FFI | Native imports, function pointers, native-to-ESPB callbacks and data pointers | Cross-boundary calling convention and marshalling behavior | RTOS scheduling behavior |
| RTOS | Bounded FreeRTOS task/timer cases | Task/timer/callback integration | Microbenchmark timings |
| Performance | Interpreter core, raw JIT entry, public API separately | Relative execution cost for a controlled workload | Translator time or compilation speed |
| Resources / lifecycle | Load, warm-up, execution, unload, heap/stack and code-size snapshots | Memory behavior and lifecycle cleanup | Long-run fragmentation without a dedicated soak test |

## 2. Data flow and provenance

The canonical corpus flow is:

`tests/bytecode_groups/*.c -> LLVM .bc -> translated JIT .espb -> interpreter twin`

The Interpreter twin is produced by clearing only the function-header HOT bit (`0x40`) in the translated module. The pair validator must prove:
- every JIT function is HOT;
- every corresponding Interpreter function is non-HOT;
- all non-HOT bits, function bodies, section contents and module metadata remain byte-identical;
- each pair has one-to-one group identity and no missing/extra artifact.

Every paired artifact includes a manifest. The manifest binds:
- repository, branch, producer workflow run ID;
- source commit and translator submodule commit;
- each C source SHA-256;
- each LLVM bitcode SHA-256;
- each JIT bytecode SHA-256;
- each Interpreter bytecode SHA-256.

QEMU must discover a successful producer workflow run, download its artifact and verify the manifest before staging any module or building firmware. A missing manifest, stale run, changed source, changed translator submodule, mismatched hash or missing group is a hard failure. Never silently fall back to an older artifact.

## 3. Correctness and coverage rules

Source syntax is not opcode coverage. The audit is based on decoded ESPB Code sections and the actual raw opcode / extended subopcode values emitted. A source-level `__atomic_*` call does not count as raw atomic-opcode coverage when it becomes an imported native call.

Each group needs:
- a clear semantic purpose and deterministic input;
- explicit expected result or side-effect assertion;
- a stable machine-readable PASS/FAIL marker;
- Interpreter and JIT execution of the same logical test;
- test evidence identifying target, group, mode, source run and module hash.

Opcode coverage and semantic execution coverage are separate:
- *Static emitted coverage* says an opcode occurs in a module.
- *Dynamic execution coverage* says a path containing that opcode actually executed.
- *Semantic assertions* say the output/side effects are correct.

Do not infer dynamic execution from static presence. Add runtime instrumentation only in a separate coverage profile; keep it out of timed benchmark regions. Keep the denominator tied to the exact implementation set and target dispatch implementation under audit.

## 4. ABI and callbacks

Keep distinct tests for:
1. direct local calls and call chains;
2. recursion;
3. function pointers / indirect calls entirely inside bytecode;
4. native imports with ordinary scalar and pointer arguments;
5. native code calling an ESPB function pointer (native-to-ESPB callback);
6. callback user-data and nontrivial argument signatures;
7. callback metadata / automatic closure creation (CBMETA path);
8. pointer marshalling metadata, where used.

The QEMU harness already exposes native helper functions `host_invoke_cb` and `host_invoke_cb2`. The existing `07_calls_callbacks.c` checks local function pointers and recursion, but is not evidence of the native-to-ESPB callback path until it actually imports and invokes those helpers with valid callback metadata. Keep this as an explicit coverage gap until produced bytecode and both engine modes prove it.

## 5. RTOS isolation

FreeRTOS task/timer tests are integration tests, not timing workloads. Keep them deterministic and bounded. The current harness limit is exactly three executor calls per mode for each RTOS group. Each RTOS case must clean up its own task/timer resources and report an explicit assertion. Do not derive Interpreter/JIT speed ratios from RTOS cases.

## 6. Benchmark methodology

Report three different execution costs:
- Interpreter core: resolved function index and execution frame/context overhead; public name lookup excluded.
- Raw JIT entry: direct generated JIT function execution; compilation, precompile, warm-up, register-buffer initialization and argument copying excluded.
- Public API: full public `espb_call_function_sync` path, reported separately.

Use repeated samples and a median (and retain individual samples). Keep correctness checks and instrumentation outside timed regions where practical. Do not include translator/API/build time in runtime execution time. Compare paired workloads, same target, same run and same inputs. QEMU timing is for regression diagnostics and engine ratios; physical-board results are required for external performance claims.

## 7. Resource and lifecycle methodology

Record snapshots outside timers:
- free heap before load;
- after load;
- after JIT readiness / warm-up;
- after execution;
- after unload;
- minimum free heap;
- stack high-water mark before/after;
- JIT generated code bytes;
- all module starts, completions and cleanup results.

A successful test requires unload recovery within a defined tolerance and no persistent heap growth during a repeated lifecycle test. Keep one-time runtime initialization distinct from per-module leaks.

## 8. Target matrix and acceptance gates

Run target jobs independently with fail-fast disabled:
- ESP32 Xtensa;
- ESP32-C3 RISC-V.

Do not mask one target's failure with the other's success. Keep the existing target configuration and CPU frequencies unchanged. Critical results later require physical-board confirmation.

Acceptance order:
1. Host tools pass.
2. All ten LLVM groups compile and produce exactly ten non-empty bitcode files.
3. Translator creates ten JIT and ten Interpreter modules and a valid provenance manifest.
4. Manifest and pair audit pass against the exact selected run.
5. QEMU firmware builds on both targets.
6. All Interpreter and JIT modules load, are ready, execute semantic assertions and unload cleanly.
7. All required group assertions pass in both modes; RTOS groups remain bounded and outside benchmarks.
8. Required benchmark and resource records are complete and valid.
9. Performance claims are rechecked on physical boards.

A failure at an earlier gate blocks interpretation of later gates. In particular, a firmware build pass is not a runtime pass, and a QEMU panic before group assertions is not a semantic opcode failure classification.

## 9. Current engineering constraints

- Work on `ESPB_WORK`; preserve `main` and all other branches.
- Do not alter ESP-IDF/sdkconfig, CPU frequencies, compiler/linker flags, or IRAM/DRAM placement to influence results.
- Do not modify existing runtime modules to accommodate a test unless a reproducible root cause establishes a runtime defect.
- Add tests in small isolated groups and keep artifacts provenance-bound.
- Keep translator API use separate and manual/quota-aware; bitcode generation and host checks should not spend translator quota.
