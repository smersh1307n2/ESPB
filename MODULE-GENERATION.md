# ESPB_WORK module generation

This branch keeps the runtime in the repository and tracks the two companion projects as Git submodules:

- `projects/ESP32_PRJ_TO_LLVM` — source examples and ESP-IDF/Clang bitcode builder.
- `projects/ESPB_Desktop_Client` — reference implementation of the ESPB Translation API client.

The four manual workflows are intentionally independent. Each builds and translates exactly one module, then commits only that module under `modules/`:

| Workflow | Source example | Bitcode | Output |
|---|---|---|---|
| `Generate 01_integer_integer` | `examples/01_integer` | `01_integer_integer.bc` | `modules/01_integer/01_integer_integer.espb` |
| `Generate 02_control_flow_control_flow` | `examples/02_control_flow` | `02_control_flow_control_flow.bc` | `modules/02_control_flow/02_control_flow_control_flow.espb` |
| `Generate 03_memory_memory` | `examples/03_memory` | `03_memory_memory.bc` | `modules/03_memory/03_memory_memory.espb` |
| `Generate 04_calls_calls` | `examples/04_calls` | `04_calls_calls.bc` | `modules/04_calls/04_calls_calls.espb` |

## API keys

Create a repository Actions secret named `ESPB_TRANSLATOR_KEYS`. Its value is a newline-separated list of API keys, in preferred order. For example:

```text
key-one
key-two
key-three
```

Add further keys by adding further lines to this single secret; no workflow edit is required. The value is never printed. A key is skipped only when the API reports quota exhaustion / rate limiting; other failures stop the job so they are not hidden.

For compatibility, if `ESPB_TRANSLATOR_KEYS` is empty, the workflows also read the existing `ESPB_API_KEY`, `ESPB_API_KEY_1`, and `ESPB_API_KEY_2` secrets. GitHub Actions cannot enumerate arbitrary secret names, so future numbered secrets must be copied into the aggregate list (or explicitly wired into the workflows).

## Run

Open **Actions**, select the required `Generate ...` workflow, and choose **Run workflow**. No other module is built or translated, so the run spends quota on only the selected module.

The workflow pushes the generated file to `ESPB_WORK`. It does not write to `main` in any of the three repositories.
