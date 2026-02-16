#include <stdio.h>
#include "espb_api.h"
#include <esp_log.h>
#include "espb_host_symbols.h"

#include "espb_fast_symbols.h"


// External symbols for accessing embedded test.espb file
extern const uint8_t test_espb_start[] asm("_binary_test_espb_start");
extern const uint8_t test_espb_end[]   asm("_binary_test_espb_end");

static const char *TAG = "main";
// --- Host-like callback helpers ---
typedef void (*cb1_t)(void*);
typedef void (*cb2_t)(int, void*);

__attribute__((noinline, optimize("O0")))
void host_invoke_cb(cb1_t cb, void *user_data) {
    if (cb) cb(user_data);
}

__attribute__((noinline, optimize("O0")))
void host_invoke_cb2(cb2_t cb, int x, void *user_data) {
    if (cb) cb(x, user_data);
}

__attribute__((noinline, optimize("O0")))
void native_set_magic_number(int* out_value) {
   ESP_LOGD(TAG, "native_set_magic_number called with out_value pointer = %p\n", (void*)out_value);
    if (out_value) {
        *out_value = 42; // Write magic number
       ESP_LOGD(TAG, "Wrote 42 to the pointer.\n");
    } else {
       ESP_LOGD(TAG, "ERROR: out_value pointer is NULL!\n");
    }
}

// --- User symbols demonstration ---

// 1. Create custom function to make available for ESPB
void my_custom_print(const char* str) {
    printf(">> Custom Print from main.cpp: %s\n", str);
}
/*
// 2. Create named symbol table for our custom functions
static const EspbSymbol custom_symbols[] = {
    {"my_custom_print", (const void*)&my_custom_print },
    {"set_magic_number", (const void*)native_set_magic_number},
    {"host_invoke_cb", (const void*)host_invoke_cb},
    {"host_invoke_cb2", (const void*)host_invoke_cb2},
    ESP_ELFSYM_END // Обязательный маркер конца таблицы
};

*/

// 3. (Optional) Define custom index table
// IMPORTANT: Order must match symbols/custom_fast.sym and translator!
///*
static const EspbSymbolFast my_custom_fast_table[] = {
#include "symbols/custom_fast.sym"
};
//*/


extern "C" void app_main(void)
{

    printf("--- ESPB API Demo ---\n");

    // 3. Register our custom symbol tables.
    // Register user named symbols under module_num=2 (will be searched after module_num=0/1)
    //espb_register_symbol_table(2, custom_symbols);
   
    // (Optional) Register custom fast table for O(1) index-based symbol resolution
    // Use only if you compile modules with --custom-fast flag
    // NOTE: idf_fast table is automatically registered in init_cpp_symbols()
    espb_register_custom_index_symbol_table(my_custom_fast_table);


    espb_handle_t espb_handle = NULL;
    EspbResult result;

    // 1. Load module
    const size_t espb_size = test_espb_end - test_espb_start;
    result = espb_load_module(test_espb_start, espb_size, &espb_handle);
    if (result != ESPB_OK) {
        printf("Failed to load ESPB module, error: %d\n", result);
        return;
    }

    printf("\nCalling 'app_main' with arguments...\n");
    const char* my_string = "Hello from main!";

    Value args[] = {
        ESPB_I32(1112),
        ESPB_PTR(my_string),
        ESPB_PTR(NULL)
    };

    result = espb_call_function_sync(espb_handle, "app_main", args, 3, NULL);
    if (result != ESPB_OK) {
        printf("Failed to call 'app_main', error: %d\n", result);
    }

    printf("\nCalling 'test' with (int, double, char*)...\n");

   //  double d=0;
  //  printf("hi_hi %f",d);
    ///*
    Value args1[] = {
        ESPB_I32(12345),           // Argument 1: int (32-bit)
        ESPB_F64(3.1415926535),    // Argument 2: double (64-bit float)
        ESPB_STRING("This is a test string!")  // Argument 3: char* (pointer)
    };
  
    // Synchronous call to function "test" with 3 arguments
    result = espb_call_function_sync(espb_handle, "test",(Value *) args1, 3, NULL);
    if (result != ESPB_OK) {
        printf("Failed to call 'test', error: %d\n", result);
    }

    espb_unload_module(espb_handle);

    
    printf("\n--- ESPB API Demo Finished ---\n");
}



