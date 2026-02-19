# ESPB Interpreter Component

## Overview

**ESPB** is a lightweight, embeddable bytecode interpreter designed for the ESP-IDF framework. It enables the loading and execution of pre-compiled code modules in a sandboxed environment on ESP32 microcontrollers. The interpreter is written in C and integrates tightly with FreeRTOS and `libffi` to provide a powerful Foreign Function Interface (FFI) for calling native C/C++ functions from the interpreted code.

This component is ideal for applications that require dynamic code loading, safe execution of third-party logic, or a way to update parts of the application logic without a full firmware OTA update.

## Core Features

*   **Sandboxed Execution**: Code runs within a dedicated memory space, with controlled access to system resources.
*   **Custom Bytecode Format (`.espb`)**: A section-based binary format optimized for low-memory environments, inspired by formats like WebAssembly.
*   **Rich Type System**: Supports common value types, including integers (8 to 64-bit), floats, doubles, and pointers.
*   **Advanced FFI**: Seamlessly call native C functions from the bytecode using `libffi`.
*   **Host Bindings & Callbacks**:
    *   Import native functions into the sandbox.
    *   Pass interpreter functions as callbacks to native functions.
    *   Automatic marshalling for pointer arguments (e.g., handling IN/OUT buffers).
*   **Memory Management**: Includes a custom heap manager (`multi_heap`) to manage memory allocations within the sandbox.
*   **Thread-Safety**: Designed to be thread-safe using FreeRTOS mutexes for instance-level operations.

## JIT Compilation

ESPB now includes a Just-In-Time (JIT) compiler for improved execution performance. The JIT engine translates ESPB bytecode to native machine code at runtime, providing significant speedups compared to interpreted execution.

### Supported Architectures

* **Xtensa** (ESP32 series)
* **RISC-V** (ESP32-C series)

> **Note**: Currently, JIT compilation has been tested only on ESP32, ESP32-C3, ESP32-C6. Other targets are supported but may require additional validation.

### How It Works

The JIT compiler translates each ESPB bytecode instruction into corresponding native instructions for the target architecture. This allows the virtual machine to execute native code directly, eliminating the interpretation overhead.

### JIT_HOT Functions

Only functions marked with the `JIT_HOT` attribute are compiled to native code via JIT and executed as native machine code. All other functions continue to run in the standard interpreted mode. This selective compilation approach allows you to optimize performance-critical functions while maintaining the flexibility and safety of interpretation for the rest of your code.

To mark a function for JIT compilation, add the `JIT_HOT` attribute to the function in your LLVM IR source before compiling to ESPB bytecode. For detailed instructions and examples, please refer to the companion project: [ESP32_PRJ_TO_LLVM](https://github.com/smersh1307n2/ESP32_PRJ_TO_LLVM).

## Architecture


The ESPB component consists of several key modules:

*   **Parser** ([`espb_interpreter_parser.h`](include/espb_interpreter_parser.h)): Responsible for reading and validating the `.espb` binary file. It parses the header, section table, and all other sections (Types, Functions, Code, Imports, Exports, Data, etc.).
*   **Runtime** ([`espb_interpreter_runtime.h`](include/espb_interpreter_runtime.h)): The execution engine. It manages the virtual machine state, including the operand stack, call stack, and virtual registers. It executes the bytecode instructions.
*   **Public API** ([`espb_api.h`](include/espb_api.h)): Provides the main entry points for host applications to interact with the interpreter, such as loading modules and calling functions.
*   **FFI & Callback System** ([`ffi_freertos.h`](include/ffi_freertos.h), [`espb_callback_system.h`](include/espb_callback_system.h)): Manages the interaction between the interpreter and native code. It resolves imported host symbols and uses `libffi` to construct and invoke function calls. It also handles creating native closures for ESPB functions passed as callbacks.
*   **Heap Manager** ([`espb_heap_manager.h`](include/espb_heap_manager.h)): A wrapper around `multi_heap` that provides a dedicated heap for each module instance.

## The `.espb` Binary Format

The `.espb` format is defined by a header and a series of sections.

*   **Header** (`EspbHeader`): Contains a magic number (`'ESPB'`), version, flags, and the number of sections.
*   **Section Table**: An array of `SectionHeaderEntry` structs, each describing a section's ID, offset, and size.

Key sections include:
*   `Types`: Defines all function signatures used in the module.
*   `Imports`: Lists all host functions, globals, or memories that the module requires.
*   `Functions`: Associates each internal function with a type signature.
*   `Code`: Contains the actual bytecode for each function.
*   `Exports`: Lists the functions that are exposed to the host application.
*   `Data`: Defines data segments to be loaded into the module's linear memory.
*   `Relocations`: Contains information needed to link the module at runtime (e.g., patching addresses of functions and data).
*   `cbmeta` / `immeta`: Custom metadata sections for advanced FFI features like automatic callback creation and pointer marshalling.

## Public API Usage

The main API functions are declared in [`espb_api.h`](include/espb_api.h).

### Data Types

*   `espb_handle_t`: An opaque handle representing a loaded module instance.
*   `Value`: A union representing a value of any supported type on the operand stack.
*   `EspbResult`: An enum for status and error codes.

### Core Functions

*   `espb_load_module(const uint8_t *data, size_t size, espb_handle_t *handle)`:
    Parses, validates, and instantiates an ESPB module from a byte buffer. On success, it returns a handle to the new instance.

*   `espb_unload_module(espb_handle_t handle)`:
    Frees all resources associated with a module instance, including its memory, globals, and any created FFI closures.

*   `espb_call_function_sync(espb_handle_t handle, const char* name, const Value *args, uint32_t num_args, Value *results)`:
    Synchronously calls an exported function by name. It takes an array of `Value` arguments and can return a result.

### Example

```c
#include "espb_api.h"
#include "esp_log.h"

// Assume 'my_module_espb_data' is a const uint8_t[] array containing the bytecode
extern const uint8_t my_module_espb_data[];
extern const size_t my_module_espb_size;

void run_espb_code() {
    espb_handle_t module_handle = NULL;
    EspbResult res;

    // 1. Load the module
    res = espb_load_module(my_module_espb_data, my_module_espb_size, &module_handle);
    if (res != ESPB_OK) {
        ESP_LOGE("ESPB", "Failed to load module, error: %d", res);
        return;
    }

    // 2. Prepare arguments for a function call
    // Let's call a function: `int add(int a, int b)`
    Value args[2];
    args[0] = ESPB_I32(10);
    args[1] = ESPB_I32(32);

    // 3. Prepare a variable for the result
    Value result;

    // 4. Call the exported function
    res = espb_call_function_sync(module_handle, "add", args, 2, &result);
    if (res != ESPB_OK) {
        ESP_LOGE("ESPB", "Failed to call function 'add', error: %d", res);
    } else {
        ESP_LOGI("ESPB", "Result of add(10, 32) is: %d", result.value.i32);
    }

    // 5. Unload the module and free resources
    espb_unload_module(module_handle);
}
```

## Building

The component is configured via `CMakeLists.txt` and is registered as a standard `idf_component`.

## License

Components espb and libffi is licensed under the **GNU Affero General Public License v3.0**. See the [`LICENSE`](LICENSE) file for details.

## Configuration for RISC-V Targets

When using this component on ESP32 RISC-V targets, special configuration is required for closures (callbacks) to work correctly.
You must **disable** memory protection in the SDK Configuration (`menuconfig`). This is required because `libffi` needs to write executable trampoline code to RAM, which is prevented by memory protection.

*   Go to `Component config` -> `ESP System Settings` -> `Memory protection`
*   Uncheck `Enable memory protection`.

## Stack and IRAM Settings for Examples

The following settings have been verified for the provided examples. You may need to adjust them based on your application's needs.

*   **Main Task Stack Size**: `8192`
    *   `Component config` -> `ESP System Settings` -> `Main task stack size`
*   **Timer Task Stack Size**: `8192`
    *   `Component config` -> `ESP System Settings` -> `Timer task stack size`
*   **Initial Shadow Stack Size**: `4096`
    *   `Component config` -> `ESP32C3-specific` -> `Initial shadow stack size (bytes)`
*   **Shadow Stack Increment Size**: `4096`
    *   `Component config` -> `ESP32C3-specific` -> `Shadow stack increment size (bytes)`
*   **Enable IRAM Pool for Closures**:
    *   Go to `Component config` -> `libffi`
    *   Check `Use IRAM memory pool for closures`
*   **IRAM Pool Size**: `1024`
    *   Set `IRAM memory pool size (bytes)` to `1024`.

   ### Translation

The generated LLVM IR can be translated using the web-based translator:

**[http://espb.runasp.net/](http://espb.runasp.net/)**

For translation, you must use the binary file `linked_module.bc`.
