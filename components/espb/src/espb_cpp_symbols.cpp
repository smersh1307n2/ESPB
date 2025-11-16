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
// Таблица символов C++ функций
// Имена точно такие, как они будут в импортах ESPb модуля
static const EspbSymbol cpp_symbols[] = {
    // Пример для printf (если модуль импортирует "env::printf")
    // { "printf", (const void*)&espb_simple_puts }, // Старая версия с оберткой
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
    { "gpio_config", (const void*)&gpio_config },
    { "esp_err_to_name", (const void*)&esp_err_to_name },
    { "gpio_set_level", (const void*)&gpio_set_level },
    
    { "esp_rom_get_cpu_ticks_per_us", (const void*)&esp_rom_get_cpu_ticks_per_us },
     { "esp_cpu_get_cycle_count", (const void*)&esp_cpu_get_cycle_count },



  //  { "xTimerDelete", (const void*)&xTimerDelete },



    // Host-like helpers to invoke callbacks (CB, user_data)
    { "host_invoke_cb", (const void*)&host_invoke_cb },
    { "host_invoke_cb2", (const void*)&host_invoke_cb2 },
    { "set_magic_number", (const void*)&native_set_magic_number },
    
    // Атомарные операции для 64-битных значений (обертки для встроенных функций GCC)
    // Пример для C++ I/O (если модуль это использует и оно реализовано)
// ... existing code ...

    ESP_ELFSYM_END
};

// Экспорт глобального объекта std::cout
extern "C" void* _ZSt4cout_ptr() {
    return &std::cout;
}

// Таблица глобальных переменных C++
static const EspbSymbol cpp_globals[] = {
    { "_ZSt4cout", (const void*)&_ZSt4cout_ptr },
    ESP_ELFSYM_END
};

// Функция инициализации символов, вызывать в начале main
extern "C" void init_cpp_symbols(void) {
    // Регистрируем таблицы символов
    espb_register_symbol_table("env", cpp_symbols);
    espb_register_symbol_table("cpp_globals", cpp_globals);
} 