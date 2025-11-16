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
)

# 2. Регистрируем основной компонент espb
idf_component_register(SRCS ${ESPB_SRCS}
                      # Публичные заголовочные файлы компонента
                      INCLUDE_DIRS "include"
                      # Приватные зависимости - наш компонент зависит от libffi и esp_timer
                      REQUIRES "libffi" "esp_timer" "driver"
)


# 3. Добавляем флаги компилятора (если необходимо), как это было в main/CMakeLists.txt
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-return-type)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu99>)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>)
