set(ESPB_MODULE_FILES
    "01_integer32.interpreter.espb"
    "01_integer32.jit.espb"
    "02_integer64.interpreter.espb"
    "02_integer64.jit.espb"
    "03_float.interpreter.espb"
    "03_float.jit.espb"
    "04_control_flow.interpreter.espb"
    "04_control_flow.jit.espb"
    "05_select_convert.interpreter.espb"
    "05_select_convert.jit.espb"
    "06_memory.interpreter.espb"
    "06_memory.jit.espb"
    "07_calls_callbacks.interpreter.espb"
    "07_calls_callbacks.jit.espb"
    "08_atomics.interpreter.espb"
    "08_atomics.jit.espb"
    "09_freertos_task.interpreter.espb"
    "09_freertos_task.jit.espb"
    "10_freertos_timer.interpreter.espb"
    "10_freertos_timer.jit.espb"
)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES espb esp_timer esp_hw_support esp_system freertos
    EMBED_FILES ${ESPB_MODULE_FILES}
)
