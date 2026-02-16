# Компонент ESPB: включает интерпретатор и libffi

# 1. Исходные файлы самого интерпретатора
set(ESPB_SRCS
    "src/espb_heap_manager.c"
    "src/espb_cpp_symbols.cpp"
    "src/espb_interpreter_reader.c"
    "src/espb_interpreter_parser.c"
    "src/espb_interpreter_runtime.c"
    "src/espb_interpreter_runtime_oc.c"
    "src/espb_host_symbols.c"
    "src/espb_callback_system.c"
    "src/ffi_freertos.c"
    "src/iram_pool_wrapper.c"
    "src/espb_api.c"
    "src/espb_jit_cache.c"
    "src/espb_jit_dispatcher.c"
    "src/espb_jit_import_call.c"
    "src/espb_jit_indirect_ptr.c"
    "src/espb_jit_globals.c"
    "src/espb_jit_helpers.c"
    "src/espb_exec_memory.c"
    "src/espb_runtime_alloca.c"
    "src/espb_runtime_call_import_basic.c"
    "src/espb_runtime_ffi_call.c"
    "src/espb_runtime_ffi_types.c"
    "src/espb_runtime_ffi_pack.c"
    "src/espb_jit_precompile.c"
)

# --- Architecture-specific sources ---
# JIT backend is currently implemented only for RISC-V targets.
#
# Notes:
# - Use CONFIG_IDF_TARGET_ARCH_* (from sdkconfig) instead of enumerating chips.
# - Also build only when CONFIG_ESPB_JIT_ENABLED is enabled, so JIT code doesn't get pulled
#   into builds where JIT is explicitly disabled.
if(CONFIG_ESPB_JIT_ENABLED AND CONFIG_IDF_TARGET_ARCH_RISCV)
    list(APPEND ESPB_SRCS "src/arch/jit_riscv.c")
endif()

if(CONFIG_ESPB_JIT_ENABLED AND CONFIG_IDF_TARGET_ARCH_XTENSA)
    list(APPEND ESPB_SRCS "src/arch/jit_xtensa.c")
    list(APPEND ESPB_SRCS "src/arch/jit_xtensa_inline.c")
endif()

# 2. Регистрируем основной компонент espb
idf_component_register(SRCS ${ESPB_SRCS}
                      # Публичные заголовочные файлы компонента
                      INCLUDE_DIRS "include" "src"
                      # Приватные зависимости - наш компонент зависит от libffi и esp_timer
                      REQUIRES "libffi" "esp_timer" "driver" "esp_mm"
)

# Добавляем директорию symbols как приватную (только для этого компонента)
target_include_directories(${COMPONENT_LIB} PRIVATE "symbols")


# 3. Добавляем флаги компилятора (если необходимо), как это было в main/CMakeLists.txt
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-return-type)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu99>)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>)

# JIT debug statistics (helper calls)
target_compile_definitions(${COMPONENT_LIB} PRIVATE JIT_STATS=1)

