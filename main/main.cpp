#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "espb_api.h"
#include "espb_host_symbols.h"
#include "espb_fast_symbols.h"
#include "espb_interpreter_runtime.h"
#include "espb_jit_precompile.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

/* Keep in sync with the private handle layout used by ESPB QEMU tests. */
struct espb_module_handle_t { EspbInstance *instance; EspbModule *module; uint32_t num_func_imports; };
typedef void (*host_cb1_t)(void *);
typedef void (*host_cb2_t)(int, void *);

__attribute__((noinline, optimize("O0"))) static void host_invoke_cb(host_cb1_t cb, void *data) { if (cb) cb(data); }
__attribute__((noinline, optimize("O0"))) static void host_invoke_cb2(host_cb2_t cb, int value, void *data) { if (cb) cb(value, data); }
static void native_set_magic_number(int *out) { if (out) *out = 42; }
static void my_custom_print(const char *message) { printf("ESPB module: %s\n", message ? message : "(null)"); }
static portMUX_TYPE demo_atomic_mux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t demo_atomic_exchange_8(volatile void *address, uint64_t value, int memory_order) {
    (void)memory_order; volatile uint64_t *target = (volatile uint64_t *)address;
    taskENTER_CRITICAL(&demo_atomic_mux); const uint64_t old = *target; *target = value;
    taskEXIT_CRITICAL(&demo_atomic_mux); return old;
}
static TimerHandle_t demo_xTimerCreate(const char *name, TickType_t period, UBaseType_t reload, void *id, TimerCallbackFunction_t cb) {
    if (period == 0) { ESP_LOGW("ESPB", "timer period zero; clamping to one tick"); period = 1; }
    return xTimerCreate(name, period, reload, id, cb);
}
static const EspbSymbolFast app_fast_symbols[] = {
#include "symbols/custom_fast.sym"
};
static const EspbSymbol app_named_symbols[] = {
    {"printf", (const void *)&printf}, {"host_invoke_cb", (const void *)&host_invoke_cb},
    {"host_invoke_cb2", (const void *)&host_invoke_cb2}, {"set_magic_number", (const void *)&native_set_magic_number},
    {"my_custom_print", (const void *)&my_custom_print}, {"__atomic_exchange_8", (const void *)&demo_atomic_exchange_8},
    {"vTaskDelay", (const void *)&vTaskDelay}, {"xTaskCreatePinnedToCore", (const void *)&xTaskCreatePinnedToCore},
    {"vTaskDelete", (const void *)&vTaskDelete}, {"xTaskGetTickCount", (const void *)&xTaskGetTickCount},
    {"xTimerCreate", (const void *)&demo_xTimerCreate}, {"xTimerGenericCommand", (const void *)&xTimerGenericCommand},
    {"pvTimerGetTimerID", (const void *)&pvTimerGetTimerID}, ESP_ELFSYM_END
};
struct EmbeddedModule { const char *name; const uint8_t *data; size_t size; bool jit; };

extern const uint8_t module_01_integer32_interpreter_espb_start[] asm("_binary_01_integer32_interpreter_espb_start");
extern const uint8_t module_01_integer32_interpreter_espb_end[] asm("_binary_01_integer32_interpreter_espb_end");
extern const uint8_t module_01_integer32_jit_espb_start[] asm("_binary_01_integer32_jit_espb_start");
extern const uint8_t module_01_integer32_jit_espb_end[] asm("_binary_01_integer32_jit_espb_end");
extern const uint8_t module_02_integer64_interpreter_espb_start[] asm("_binary_02_integer64_interpreter_espb_start");
extern const uint8_t module_02_integer64_interpreter_espb_end[] asm("_binary_02_integer64_interpreter_espb_end");
extern const uint8_t module_02_integer64_jit_espb_start[] asm("_binary_02_integer64_jit_espb_start");
extern const uint8_t module_02_integer64_jit_espb_end[] asm("_binary_02_integer64_jit_espb_end");
extern const uint8_t module_03_float_interpreter_espb_start[] asm("_binary_03_float_interpreter_espb_start");
extern const uint8_t module_03_float_interpreter_espb_end[] asm("_binary_03_float_interpreter_espb_end");
extern const uint8_t module_03_float_jit_espb_start[] asm("_binary_03_float_jit_espb_start");
extern const uint8_t module_03_float_jit_espb_end[] asm("_binary_03_float_jit_espb_end");
extern const uint8_t module_04_control_flow_interpreter_espb_start[] asm("_binary_04_control_flow_interpreter_espb_start");
extern const uint8_t module_04_control_flow_interpreter_espb_end[] asm("_binary_04_control_flow_interpreter_espb_end");
extern const uint8_t module_04_control_flow_jit_espb_start[] asm("_binary_04_control_flow_jit_espb_start");
extern const uint8_t module_04_control_flow_jit_espb_end[] asm("_binary_04_control_flow_jit_espb_end");
extern const uint8_t module_05_select_convert_interpreter_espb_start[] asm("_binary_05_select_convert_interpreter_espb_start");
extern const uint8_t module_05_select_convert_interpreter_espb_end[] asm("_binary_05_select_convert_interpreter_espb_end");
extern const uint8_t module_05_select_convert_jit_espb_start[] asm("_binary_05_select_convert_jit_espb_start");
extern const uint8_t module_05_select_convert_jit_espb_end[] asm("_binary_05_select_convert_jit_espb_end");
extern const uint8_t module_06_memory_interpreter_espb_start[] asm("_binary_06_memory_interpreter_espb_start");
extern const uint8_t module_06_memory_interpreter_espb_end[] asm("_binary_06_memory_interpreter_espb_end");
extern const uint8_t module_06_memory_jit_espb_start[] asm("_binary_06_memory_jit_espb_start");
extern const uint8_t module_06_memory_jit_espb_end[] asm("_binary_06_memory_jit_espb_end");
extern const uint8_t module_07_calls_callbacks_interpreter_espb_start[] asm("_binary_07_calls_callbacks_interpreter_espb_start");
extern const uint8_t module_07_calls_callbacks_interpreter_espb_end[] asm("_binary_07_calls_callbacks_interpreter_espb_end");
extern const uint8_t module_07_calls_callbacks_jit_espb_start[] asm("_binary_07_calls_callbacks_jit_espb_start");
extern const uint8_t module_07_calls_callbacks_jit_espb_end[] asm("_binary_07_calls_callbacks_jit_espb_end");
extern const uint8_t module_08_atomics_interpreter_espb_start[] asm("_binary_08_atomics_interpreter_espb_start");
extern const uint8_t module_08_atomics_interpreter_espb_end[] asm("_binary_08_atomics_interpreter_espb_end");
extern const uint8_t module_08_atomics_jit_espb_start[] asm("_binary_08_atomics_jit_espb_start");
extern const uint8_t module_08_atomics_jit_espb_end[] asm("_binary_08_atomics_jit_espb_end");
extern const uint8_t module_09_freertos_task_interpreter_espb_start[] asm("_binary_09_freertos_task_interpreter_espb_start");
extern const uint8_t module_09_freertos_task_interpreter_espb_end[] asm("_binary_09_freertos_task_interpreter_espb_end");
extern const uint8_t module_09_freertos_task_jit_espb_start[] asm("_binary_09_freertos_task_jit_espb_start");
extern const uint8_t module_09_freertos_task_jit_espb_end[] asm("_binary_09_freertos_task_jit_espb_end");
extern const uint8_t module_10_freertos_timer_interpreter_espb_start[] asm("_binary_10_freertos_timer_interpreter_espb_start");
extern const uint8_t module_10_freertos_timer_interpreter_espb_end[] asm("_binary_10_freertos_timer_interpreter_espb_end");
extern const uint8_t module_10_freertos_timer_jit_espb_start[] asm("_binary_10_freertos_timer_jit_espb_start");
extern const uint8_t module_10_freertos_timer_jit_espb_end[] asm("_binary_10_freertos_timer_jit_espb_end");

static const EmbeddedModule modules[] = {
    {"01_integer32.interpreter.espb", module_01_integer32_interpreter_espb_start, (size_t)(module_01_integer32_interpreter_espb_end - module_01_integer32_interpreter_espb_start), false},
    {"01_integer32.jit.espb", module_01_integer32_jit_espb_start, (size_t)(module_01_integer32_jit_espb_end - module_01_integer32_jit_espb_start), true},
    {"02_integer64.interpreter.espb", module_02_integer64_interpreter_espb_start, (size_t)(module_02_integer64_interpreter_espb_end - module_02_integer64_interpreter_espb_start), false},
    {"02_integer64.jit.espb", module_02_integer64_jit_espb_start, (size_t)(module_02_integer64_jit_espb_end - module_02_integer64_jit_espb_start), true},
    {"03_float.interpreter.espb", module_03_float_interpreter_espb_start, (size_t)(module_03_float_interpreter_espb_end - module_03_float_interpreter_espb_start), false},
    {"03_float.jit.espb", module_03_float_jit_espb_start, (size_t)(module_03_float_jit_espb_end - module_03_float_jit_espb_start), true},
    {"04_control_flow.interpreter.espb", module_04_control_flow_interpreter_espb_start, (size_t)(module_04_control_flow_interpreter_espb_end - module_04_control_flow_interpreter_espb_start), false},
    {"04_control_flow.jit.espb", module_04_control_flow_jit_espb_start, (size_t)(module_04_control_flow_jit_espb_end - module_04_control_flow_jit_espb_start), true},
    {"05_select_convert.interpreter.espb", module_05_select_convert_interpreter_espb_start, (size_t)(module_05_select_convert_interpreter_espb_end - module_05_select_convert_interpreter_espb_start), false},
    {"05_select_convert.jit.espb", module_05_select_convert_jit_espb_start, (size_t)(module_05_select_convert_jit_espb_end - module_05_select_convert_jit_espb_start), true},
    {"06_memory.interpreter.espb", module_06_memory_interpreter_espb_start, (size_t)(module_06_memory_interpreter_espb_end - module_06_memory_interpreter_espb_start), false},
    {"06_memory.jit.espb", module_06_memory_jit_espb_start, (size_t)(module_06_memory_jit_espb_end - module_06_memory_jit_espb_start), true},
    {"07_calls_callbacks.interpreter.espb", module_07_calls_callbacks_interpreter_espb_start, (size_t)(module_07_calls_callbacks_interpreter_espb_end - module_07_calls_callbacks_interpreter_espb_start), false},
    {"07_calls_callbacks.jit.espb", module_07_calls_callbacks_jit_espb_start, (size_t)(module_07_calls_callbacks_jit_espb_end - module_07_calls_callbacks_jit_espb_start), true},
    {"08_atomics.interpreter.espb", module_08_atomics_interpreter_espb_start, (size_t)(module_08_atomics_interpreter_espb_end - module_08_atomics_interpreter_espb_start), false},
    {"08_atomics.jit.espb", module_08_atomics_jit_espb_start, (size_t)(module_08_atomics_jit_espb_end - module_08_atomics_jit_espb_start), true},
    {"09_freertos_task.interpreter.espb", module_09_freertos_task_interpreter_espb_start, (size_t)(module_09_freertos_task_interpreter_espb_end - module_09_freertos_task_interpreter_espb_start), false},
    {"09_freertos_task.jit.espb", module_09_freertos_task_jit_espb_start, (size_t)(module_09_freertos_task_jit_espb_end - module_09_freertos_task_jit_espb_start), true},
    {"10_freertos_timer.interpreter.espb", module_10_freertos_timer_interpreter_espb_start, (size_t)(module_10_freertos_timer_interpreter_espb_end - module_10_freertos_timer_interpreter_espb_start), false},
    {"10_freertos_timer.jit.espb", module_10_freertos_timer_jit_espb_start, (size_t)(module_10_freertos_timer_jit_espb_end - module_10_freertos_timer_jit_espb_start), true},
};

extern "C" void app_main(void) {
    printf("\n--- ESPB: sequential embedded module run ---\n");
    espb_register_custom_index_symbol_table(app_fast_symbols); espb_register_symbol_table(0, app_named_symbols);
    const char *message = "Hello from ESPB main";
    Value args[] = { ESPB_I32(1112), ESPB_PTR(message), ESPB_PTR(NULL) };
    const size_t count = sizeof(modules) / sizeof(modules[0]); printf("[ESPB] module_count=%u\n", (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
        const EmbeddedModule &m = modules[i];
        printf("\n[ESPB] (%u/%u) loading %s (%u bytes, %s)\n", (unsigned)(i+1), (unsigned)count, m.name, (unsigned)m.size, m.jit ? "JIT" : "interpreter");
        espb_handle_t handle = NULL; const int64_t t0 = esp_timer_get_time();
        EspbResult result = espb_load_module(m.data, m.size, &handle); const int64_t load_us = esp_timer_get_time() - t0;
        if (result != ESPB_OK) { printf("[ESPB] load_failed module=%s error=%d\n", m.name, (int)result); return; }
        if (m.jit) { const int64_t c0 = esp_timer_get_time(); result = espb_jit_precompile_hot_functions(handle->instance, NULL);
            const int64_t compile_us = esp_timer_get_time() - c0;
            if (result != ESPB_OK) { printf("[ESPB] jit_precompile_failed module=%s error=%d\n", m.name, (int)result); espb_unload_module(handle); return; }
            printf("[ESPB] jit_precompiled module=%s elapsed_us=%lld\n", m.name, (long long)compile_us); }
        const int64_t c0 = esp_timer_get_time(); result = espb_call_function_sync(handle, "app_main", args, 3, NULL);
        const int64_t call_us = esp_timer_get_time() - c0;
        if (result != ESPB_OK) { printf("[ESPB] call_failed module=%s error=%d\n", m.name, (int)result); espb_unload_module(handle); return; }
        const int64_t u0 = esp_timer_get_time(); espb_unload_module(handle); const int64_t unload_us = esp_timer_get_time() - u0;
        printf("[ESPB] module_done=%s load_us=%lld call_us=%lld unload_us=%lld\n", m.name, (long long)load_us, (long long)call_us, (long long)unload_us);
        if (strstr(m.name, "09_freertos_task") || strstr(m.name, "10_freertos_timer")) vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("\n--- ESPB: all embedded modules finished ---\n");
}
