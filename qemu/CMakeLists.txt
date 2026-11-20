cmake_minimum_required(VERSION 3.16)

# Keep the same ESP-IDF format-warning policy as the primary ESPB project.
add_compile_options(-Wno-format -Wno-error=format)

# Reuse the exact ESPB component source tree without changing the root project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESPB_QEMU)
