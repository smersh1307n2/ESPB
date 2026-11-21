# Opcode coverage audit

The ten C groups are source-level candidates only. Coverage must be computed from the translated ESPB Code sections, not inferred from C syntax.

## Current artifact state

- Bitcode build run `44859958155` produced `espb-opcode-groups-bc` with ten non-empty `.bc` files.
- The paired translation workflow `build_opcode_groups_espb.yml` is configured to produce ten `.jit.espb` and ten `.interpreter.espb` files, but no paired artifact has yet been verified.
- The checked-in `.espb` seed files are legacy corpus inputs and are not substitutes for the paired ten-group output.

## Static coverage candidates to verify against the actual bytecode

- Atomics: current source exercises I32 fetch-add, compare-exchange and load; I64 exchange, store and load. Verify missing RMW variants (I32 sub/and/or/xor/exchange; I64 add/sub/and/or/xor/compare-exchange), plus fence.
- Floating point: current source exercises F32/F64 add/sub/mul/div and a limited conversion path. Verify min/max/abs/sqrt and the remaining signed/unsigned conversion opcodes.
- Select/conversion: current source exercises I32/I64/F64 selects, I64-to-I32 truncation, I8-to-I32 sign extension and pointer roundtrip. Verify F32 and pointer select and the other integer extension/truncation variants.
- Memory: distinguish scalar load/store opcodes from imported `memcpy`/`memset`; do not count those calls as bulk-memory opcode coverage.
- Control flow: verify whether `switch` actually emits `BR_TABLE`; C syntax alone is not evidence.

## Acceptance

For each paired module, decode the Code section and record the set and frequency of executed ESPB opcodes. Union all ten groups, compare with the v1.7 implemented opcode set and with both JIT backends. Any new source tests must remain compact and preserve the three-call execution limit for FreeRTOS groups. Do not change runtime or JIT implementation based on static speculation alone.
