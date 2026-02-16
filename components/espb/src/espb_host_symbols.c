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
    uint8_t module_num;
    const EspbSymbol *symbols;  // NULL-terminated array (для именованных символов)
    // Fast (index-based) table support
    const EspbSymbolFast *fast_symbols;  // NULL if not used
    uint32_t fast_count;
    uint8_t fast_flags;  // IMPORT_FLAG_FAST_IDF or IMPORT_FLAG_FAST_CUSTOM
} ModuleSymbolTable;

// Максимальное количество зарегистрированных таблиц символов
#define MAX_SYMBOL_TABLES 10

// Массив зарегистрированных таблиц символов
static ModuleSymbolTable g_symbol_tables[MAX_SYMBOL_TABLES] = {0};
static int g_num_symbol_tables = 0;

// Fast (index-based) tables
static const EspbSymbolFast *g_idf_fast_symbols = NULL;
static uint32_t g_idf_fast_count = 0;
static const EspbSymbolFast *g_custom_fast_symbols = NULL;
static uint32_t g_custom_fast_count = 0;


const void* espb_lookup_symbol_in_table(const EspbSymbol *symbols, const char *name) {
    if (!symbols || !name) return NULL;
    
    for (const EspbSymbol *sym = symbols; sym->name != NULL; sym++) {
        if (strcmp(sym->name, name) == 0) {
            return sym->address;
        }
    }
    
    return NULL;
}

__attribute__((aligned(32)))
void espb_register_symbol_table(uint8_t module_num, const EspbSymbol *symbols) {
    if (!symbols || g_num_symbol_tables >= MAX_SYMBOL_TABLES) {
        ESP_LOGE(TAG, "Failed to register symbol table: %s",
                 !symbols ? "null symbols" : "too many tables");
        return;
    }

    // Replace if already registered
    for (int i = 0; i < g_num_symbol_tables; i++) {
        if (g_symbol_tables[i].module_num == module_num) {
            ESP_LOGW(TAG, "Symbol table for module_num %u already registered, replacing", (unsigned)module_num);
            g_symbol_tables[i].symbols = symbols;
            return;
        }
    }

    g_symbol_tables[g_num_symbol_tables].module_num = module_num;
    g_symbol_tables[g_num_symbol_tables].symbols = symbols;
    g_num_symbol_tables++;

    ESP_LOGI(TAG, "Registered symbol table for module_num %u", (unsigned)module_num);
}
__attribute__((aligned(32)))
void espb_register_idf_fast_table(const EspbSymbolFast *symbols, uint32_t count) {
    // Регистрируем как специальную запись в таблице (module_num = 0xFF для idf_fast)
    if (g_num_symbol_tables >= MAX_SYMBOL_TABLES) {
        ESP_LOGE(TAG, "Too many symbol tables registered");
        return;
    }
    g_symbol_tables[g_num_symbol_tables].module_num = 0xFF;  // Специальный маркер для idf_fast
    g_symbol_tables[g_num_symbol_tables].symbols = NULL;
    g_symbol_tables[g_num_symbol_tables].fast_symbols = symbols;
    g_symbol_tables[g_num_symbol_tables].fast_count = count;
    g_symbol_tables[g_num_symbol_tables].fast_flags = IMPORT_FLAG_FAST_IDF;
    g_num_symbol_tables++;

    ESP_LOGI(TAG, "Registered idf_fast table: count=%u", (unsigned)count);
}
__attribute__((aligned(32)))
void espb_register_custom_fast_table(const EspbSymbolFast *symbols, uint32_t count) {
    // Регистрируем как специальную запись в таблице (module_num = 0xFE для custom_fast)
    if (g_num_symbol_tables >= MAX_SYMBOL_TABLES) {
        ESP_LOGE(TAG, "Too many symbol tables registered");
        return;
    }
    g_symbol_tables[g_num_symbol_tables].module_num = 0xFE;  // Специальный маркер для custom_fast
    g_symbol_tables[g_num_symbol_tables].symbols = NULL;
    g_symbol_tables[g_num_symbol_tables].fast_symbols = symbols;
    g_symbol_tables[g_num_symbol_tables].fast_count = count;
    g_symbol_tables[g_num_symbol_tables].fast_flags = IMPORT_FLAG_FAST_CUSTOM;
    g_num_symbol_tables++;
    // ESP_LOGI(TAG, "Registered custom_fast table: count=%u", (unsigned)count);
}

const void* espb_lookup_host_symbol(uint8_t module_num, const char *entity_name) {
    if (!entity_name) return NULL;

    // 1) Search exact module_num table first
    for (int i = 0; i < g_num_symbol_tables; i++) {
        if (g_symbol_tables[i].module_num == module_num) {
            return espb_lookup_symbol_in_table(g_symbol_tables[i].symbols, entity_name);
        }
    }

    // 2) Fallback only for module_num == 0: search other tables in ascending module_num order
    if (module_num == 0) {
        for (uint16_t m = 1; m < 256; m++) {
            for (int i = 0; i < g_num_symbol_tables; i++) {
                if (g_symbol_tables[i].module_num != (uint8_t)m) continue;
                const void *addr = espb_lookup_symbol_in_table(g_symbol_tables[i].symbols, entity_name);
                if (addr) return addr;
                break;
            }
        }
    }

    return NULL;
}

// Fast lookup helper used by resolve_imports()
void espb_get_fast_tables(const EspbSymbolFast **idf_tbl, uint32_t *idf_cnt,
                          const EspbSymbolFast **custom_tbl, uint32_t *custom_cnt) {
    if (idf_tbl) *idf_tbl = NULL;
    if (idf_cnt) *idf_cnt = 0;
    if (custom_tbl) *custom_tbl = NULL;
    if (custom_cnt) *custom_cnt = 0;

    for (int i = 0; i < g_num_symbol_tables; i++) {
        const EspbSymbolFast *tbl = g_symbol_tables[i].fast_symbols;
        if (!tbl) continue;

        uint8_t ff = g_symbol_tables[i].fast_flags;
        if (ff == IMPORT_FLAG_FAST_IDF) {
            if (idf_tbl) *idf_tbl = tbl;
            if (idf_cnt) *idf_cnt = g_symbol_tables[i].fast_count;
        } else if (ff == IMPORT_FLAG_FAST_CUSTOM) {
            if (custom_tbl) *custom_tbl = tbl;
            if (custom_cnt) *custom_cnt = g_symbol_tables[i].fast_count;
        }
    }
}

const void* espb_lookup_fast_symbol(uint8_t import_flags, uint16_t symbol_index) {
    // Compatibility helper (not used in hot code): linear scan.
    for (int i = 0; i < g_num_symbol_tables; i++) {
        const EspbSymbolFast *tbl = g_symbol_tables[i].fast_symbols;
        if (!tbl) continue;
        if ((import_flags & g_symbol_tables[i].fast_flags) == 0) continue;
        if (symbol_index >= g_symbol_tables[i].fast_count) return NULL;
        return tbl[symbol_index].address;
    }
    return NULL;
}
