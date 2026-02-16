/*
 * espb component
 * Copyright (C) 2025 Smersh
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "espb_host_symbols.h"
#include "espb_fast_symbols.h"

#include <stdio.h>
#include <iostream>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <math.h>
#include <stdlib.h> // Для malloc и free
#include <string.h> // Для memcpy, memset, memcmp
// Определение хост-функций для интеграции C++ в ESPb
#include "esp_timer.h"
#include <driver/gpio.h>
#include "esp_log.h"

// Новая функция-обертка
extern "C" int espb_simple_puts(const char* str) {
    // Используем стандартный printf для отладочных сообщений из этой функции,
    // чтобы не смешивать с тем printf, который мы тестируем.
    printf("[espb_simple_puts DBG] Entered. str_ptr: %p\n", (void*)str);
    fflush(stdout);

    if (str == NULL) {
        printf("[espb_simple_puts DBG] str is NULL.\n");
        fflush(stdout);
        int result = printf("(null)"); 
        printf("[espb_simple_puts DBG] printf for NULL returned: %d\n", result);
        fflush(stdout);
        return result;
    }

    printf("[espb_simple_puts DBG] str is NOT NULL. Content (first 20): START>>%.20s<<END\n", str);
    fflush(stdout);
    
    printf("[espb_simple_puts DBG] Calling target printf(\"%%s\", str)...\n");
    fflush(stdout);
    int result = printf("%s", str);
    fflush(stdout); // Важно после printf, который мы тестируем
    printf("[espb_simple_puts DBG] Target printf returned: %d\n", result);
    fflush(stdout);

    return result;
}

// Функции для поддержки вывода C++ (std::cout и т.д.)
/*
// Оператор вывода C-строк в std::ostream
extern "C" std::ostream& _ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc(std::ostream& os, const char* str) {
    return os << str;
}

// Оператор вывода целых чисел в std::ostream
extern "C" std::ostream& _ZNSolsEi(std::ostream& os, int i) {
    return os << i;
}

// Оператор вывода манипуляторов потока
extern "C" std::ostream& _ZNSolsEPFRSoS_E(std::ostream& os, std::ostream& (*pf)(std::ostream&)) {
    return os << pf;
}

// Манипулятор endl
extern "C" std::ostream& _ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_(std::ostream& os) {
    return std::endl(os);
}
*/
// --- Fast table: ESP-IDF/libc/FreeRTOS symbols (no names) ---
// Included via macros from components/espb/symbols/idf_fast.sym
 //#define ESPB_SYM(_name, _expr)          { (const void*)(_expr) },
//#define ESPB_SYM_OPT(_cfg, _name, _expr) { (_cfg) ? (const void*)(_expr) : (const void*)0 },
// __attribute__((section(".espb_symtab_rodata"))) 
//__attribute__((aligned(64)))
static const EspbSymbolFast idf_fast_symbols[] = {
#include "idf_fast.sym"
};
//#undef ESPB_SYM
//#undef ESPB_SYM_OPT

static const uint32_t idf_fast_count = (uint32_t)(sizeof(idf_fast_symbols) / sizeof(idf_fast_symbols[0]));

// Экспорт глобального объекта std::cout
extern "C" void* _ZSt4cout_ptr() {
    return &std::cout;
}
// Named table (legacy) used by existing string-based resolver
// Имена точно такие, как они будут в импортах ESPb модуля
 //__attribute__((section(".espb_symtab_rodata")))
//__attribute__((aligned(64)))  
 static const EspbSymbol cpp_symbols[] = {
    // Пример для printf (если модуль импортирует "env::printf")
    // { "printf", (const void*)&espb_simple_puts }, // Старая версия с оберткой
    /*
    { "printf", (const void*)&printf },          // Новая версия, напрямую системный printf
    { "puts", (const void*)&puts },
    { "vTaskDelay", (const void*)&vTaskDelay },
    { "xTaskCreatePinnedToCore", (const void*)&xTaskCreatePinnedToCore },
    { "xTimerCreate", (const void*)&xTimerCreate },
    { "pvTimerGetTimerID", (const void*)&pvTimerGetTimerID },
    { "xTimerGenericCommand", (const void*)&xTimerGenericCommand },
    { "xTaskGetTickCount", (const void*)&xTaskGetTickCount },
    {"pvTimerGetTimerID", (const void*)pvTimerGetTimerID},
    { "vTaskDelete", (const void*)&vTaskDelete },
    { "strcmp", (const void*)&strcmp },
    { "memcmp", (const void*)&memcmp },
    { "memcpy", (const void*)&memcpy },
    { "memset", (const void*)&memset },
    { "sqrtf", (const void*)&sqrtf },
    { "sqrt", reinterpret_cast<const void*>(static_cast<double(*)(double)>(sqrt)) },
    { "fminf", (const void*)&fminf },
    { "fmaxf", (const void*)&fmaxf },
    { "fmin", reinterpret_cast<const void*>(static_cast<double(*)(double, double)>(fmin)) },
    { "fmax", reinterpret_cast<const void*>(static_cast<double(*)(double, double)>(fmax)) },
    { "fabsf", (const void*)&fabsf },
    { "fabs", reinterpret_cast<const void*>(static_cast<double(*)(double)>(fabs)) },
    { "putchar", (const void*)&putchar },
    
    { "esp_log_write", (const void*)&esp_log_write },
    { "esp_log_timestamp", (const void*)&esp_log_timestamp },
#if CONFIG_ESPB_IDF_GPIO
    { "gpio_config", (const void*)&gpio_config },
    { "gpio_set_level", (const void*)&gpio_set_level },
#endif
    { "esp_err_to_name", (const void*)&esp_err_to_name },
    
    { "esp_rom_get_cpu_ticks_per_us", (const void*)&esp_rom_get_cpu_ticks_per_us },
     { "esp_cpu_get_cycle_count", (const void*)&esp_cpu_get_cycle_count },

{ "_ZSt4cout", (const void*)&_ZSt4cout_ptr },

  //  { "xTimerDelete", (const void*)&xTimerDelete },

//*/

    
    // Атомарные операции для 64-битных значений (обертки для встроенных функций GCC)
    // Пример для C++ I/O (если модуль это использует и оно реализовано)
// ... existing code ...



    ESP_ELFSYM_END
};




// Функция инициализации символов, вызывать в начале main
extern "C" void init_cpp_symbols(void) {
  
    // Register fast table for ESP-IDF (index-based, no names) - автоматически
    espb_register_idf_fast_table(idf_fast_symbols, idf_fast_count);

        // Регистрируем таблицы символов
    // module_num=0 is the default/global named namespace
  //  espb_register_symbol_table(0, cpp_symbols);

    // NOTE: custom_fast таблица регистрируется пользователем в main.cpp
} 