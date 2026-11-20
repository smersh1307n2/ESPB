# Embed every test*.espb placed in this component directory.
file(GLOB ESPB_TEST_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/test*.espb")
if(NOT ESPB_TEST_MODULES)
    message(FATAL_ERROR "No test*.espb modules found in main/")
endif()

set(ESPB_MODULE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/espb_embedded_modules.h")
set(ESPB_MODULE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/espb_embedded_modules.cpp")

file(WRITE "${ESPB_MODULE_HEADER}" [=[
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t size;
} EspbEmbeddedModule;
extern const EspbEmbeddedModule espb_embedded_modules[];
extern const size_t espb_embedded_module_count;
]=])

file(WRITE "${ESPB_MODULE_SOURCE}" "#include <stdint.h>\n#include \"espb_embedded_modules.h\"\n")
set(ESPB_MODULE_ENTRIES "")
set(ESPB_EMBED_FILES "")
foreach(ESPB_FILE IN LISTS ESPB_TEST_MODULES)
    get_filename_component(ESPB_NAME "${ESPB_FILE}" NAME)
    list(APPEND ESPB_EMBED_FILES "${ESPB_NAME}")
    string(REPLACE "." "_" ESPB_SYMBOL_SUFFIX "${ESPB_NAME}")
    set(ESPB_START "_binary_${ESPB_SYMBOL_SUFFIX}_start")
    set(ESPB_END "_binary_${ESPB_SYMBOL_SUFFIX}_end")
    file(APPEND "${ESPB_MODULE_SOURCE}"
        "extern const uint8_t ${ESPB_START}[] asm(\"${ESPB_START}\");\n"
        "extern const uint8_t ${ESPB_END}[] asm(\"${ESPB_END}\");\n")
    string(APPEND ESPB_MODULE_ENTRIES
        "{\"${ESPB_NAME}\", ${ESPB_START}, (size_t)(${ESPB_END} - ${ESPB_START})},\n")
endforeach()
file(APPEND "${ESPB_MODULE_SOURCE}"
    "const EspbEmbeddedModule espb_embedded_modules[] = {\n${ESPB_MODULE_ENTRIES}};\n"
    "const size_t espb_embedded_module_count = sizeof(espb_embedded_modules) / sizeof(espb_embedded_modules[0]);\n")

idf_component_register(
    SRCS "main.cpp" "${ESPB_MODULE_SOURCE}"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES espb esp_hw_support
    EMBED_FILES ${ESPB_EMBED_FILES}
)
