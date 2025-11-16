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
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "espb_symbols";

// Структура для хранения пары "имя модуля - таблица символов"
typedef struct {
    const char *module_name;
    const EspbSymbol *symbols;
} ModuleSymbolTable;

// Максимальное количество зарегистрированных таблиц символов
#define MAX_SYMBOL_TABLES 10

// Массив зарегистрированных таблиц символов
static ModuleSymbolTable g_symbol_tables[MAX_SYMBOL_TABLES] = {0};
static int g_num_symbol_tables = 0;


/*
// Обертки для встроенных атомарных функций GCC
static const EspbSymbol env_symbols[] = {
    {"printf", (const void*)printf},
    {"vTaskDelay", (const void*)vTaskDelay},
    {"set_magic_number", (const void*)native_set_magic_number},
    {"host_invoke_cb", (const void*)host_invoke_cb},
    {"host_invoke_cb2", (const void*)host_invoke_cb2},
    {"xTimerCreate", (const void*)xTimerCreate},
    {"xTimerGenericCommand", (const void*)xTimerGenericCommand},
    {"xTaskGetTickCount", (const void*)xTaskGetTickCount},
    {"vTaskDelete", (const void*)vTaskDelete},
    {"pvTimerGetTimerID", (const void*)pvTimerGetTimerID},
    {"xTaskCreatePinnedToCore", (const void*)xTaskCreatePinnedToCore},
    {"puts", (const void*)puts},
    {"strcmp", (const void*)strcmp},
    {"memcmp", (const void*)memcmp},
    {NULL, NULL} // Завершающий маркер
};
*/
void init_c_symbols(void) {
   // espb_register_symbol_table("env", env_symbols);
}

const void* espb_lookup_symbol_in_table(const EspbSymbol *symbols, const char *name) {
    if (!symbols || !name) return NULL;
    
    for (const EspbSymbol *sym = symbols; sym->name != NULL; sym++) {
        if (strcmp(sym->name, name) == 0) {
            return sym->address;
        }
    }
    
    return NULL;
}

void espb_register_symbol_table(const char *module_name, const EspbSymbol *symbols) {
    if (!module_name || !symbols || g_num_symbol_tables >= MAX_SYMBOL_TABLES) {
        ESP_LOGE(TAG, "Failed to register symbol table: %s", 
                 !module_name ? "null module name" : 
                 !symbols ? "null symbols" : "too many tables");
        return;
    }
    
    // Проверяем, если таблица для этого модуля уже зарегистрирована
    for (int i = 0; i < g_num_symbol_tables; i++) {
        if (strcmp(g_symbol_tables[i].module_name, module_name) == 0) {
            ESP_LOGW(TAG, "Symbol table for module '%s' already registered, replacing", module_name);
            g_symbol_tables[i].symbols = symbols;
            return;
        }
    }
    
    // Добавляем новую таблицу
    g_symbol_tables[g_num_symbol_tables].module_name = module_name;
    g_symbol_tables[g_num_symbol_tables].symbols = symbols;
    g_num_symbol_tables++;
    
    ESP_LOGI(TAG, "Registered symbol table for module '%s'", module_name);
}

const void* espb_lookup_host_symbol(const char *module_name, const char *entity_name) {
    if (!module_name || !entity_name) return NULL;
    
    ESP_LOGD(TAG, "Looking up symbol '%s::%s'", module_name, entity_name);
    
    // Сначала ищем таблицу для указанного модуля
    for (int i = 0; i < g_num_symbol_tables; i++) {
        if (strcmp(g_symbol_tables[i].module_name, module_name) == 0) {
            const void *addr = espb_lookup_symbol_in_table(g_symbol_tables[i].symbols, entity_name);
            if (addr) {
                ESP_LOGD(TAG, "Found symbol '%s::%s' at %p", module_name, entity_name, addr);
                return addr;
            }
        }
    }
    
    // Если модуль не найден или символ не найден в его таблице, попробуем поискать 
    // среди всех таблиц, если именно этот символ не требует точного соответствия модуля
    for (int i = 0; i < g_num_symbol_tables; i++) {
        // Пропускаем модуль, который мы уже проверили выше
        if (strcmp(g_symbol_tables[i].module_name, module_name) == 0) {
            continue;
        }
        
        const void *addr = espb_lookup_symbol_in_table(g_symbol_tables[i].symbols, entity_name);
        if (addr) {
            ESP_LOGD(TAG, "Found symbol '%s' in alternative module '%s' at %p", 
                     entity_name, g_symbol_tables[i].module_name, addr);
            return addr;
        }
    }
    
    ESP_LOGW(TAG, "Symbol '%s::%s' not found", module_name, entity_name);
    return NULL;
} 