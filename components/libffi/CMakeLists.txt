# Определяем исходные файлы и заголовочные директории для компонента libffi

# Общие исходники libffi
set(LIBFFI_COMMON_SRCS
    "src/common/types.c"
    "src/common/prep_cif.c"
    "src/common/closures.c"
    "src/common/raw_api.c"
    "src/common/tramp.c"
)

# Определение архитектуры по переменной IDF_TARGET
if(IDF_TARGET MATCHES "^(esp32|esp32s2|esp32s3)$")
    message(STATUS "Configuring libffi component for Xtensa target: ${IDF_TARGET}")
    set(LIBFFI_ARCH_SRCS
        "src/xtensa/ffi.c"
        "src/xtensa/sysv.S"
    )
    set(LIBFFI_ARCH_INCLUDE_DIR "src/xtensa")
    set(LIBFFI_ARCH_DEF "-DFFI_TARGET_XTENSA")

elseif(IDF_TARGET MATCHES "^(esp32c3|esp32c6|esp32h2)$")
    message(STATUS "Configuring libffi component for RISC-V target: ${IDF_TARGET}")
    set(LIBFFI_ARCH_SRCS
        "src/riscv/ffi_esp.c"
        "src/riscv/sysv_esp.S"
    )
    set(LIBFFI_ARCH_INCLUDE_DIR "src/riscv")
    set(LIBFFI_ARCH_DEF "-DFFI_TARGET_RISCV")
else()
    message(FATAL_ERROR "libffi component: Unsupported IDF_TARGET: ${IDF_TARGET}")
endif()

if(CONFIG_LIBFFI_USE_IRAM_POOL)
    list(APPEND LIBFFI_COMMON_SRCS "src/common/iram_pool.c")
endif()

# Регистрируем компонент
idf_component_register(SRCS ${LIBFFI_COMMON_SRCS} ${LIBFFI_ARCH_SRCS}
                    INCLUDE_DIRS "include" "src/common" "${LIBFFI_ARCH_INCLUDE_DIR}"
                    KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig
)

# Добавляем определения компилятора для выбранной архитектуры
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${LIBFFI_ARCH_DEF}) 

# Условно добавляем iram_pool и его настройки
if(CONFIG_LIBFFI_USE_IRAM_POOL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "-DIRAM_POOL_SIZE=${CONFIG_LIBFFI_IRAM_POOL_SIZE}")
endif() 