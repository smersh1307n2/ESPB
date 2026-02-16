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
#ifndef ESPB_HOST_SYMBOLS_H
#define ESPB_HOST_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

#include "espb_fast_symbols.h" // EspbSymbolFast + CONFIG_ESPB_ON/OFF

/**
 * @brief Структура для хранения информации о символе (для именованных таблиц)
 */
typedef struct {
    const char *name;    // Имя символа
    const void *address; // Адрес символа
} EspbSymbol;

/**
 * @brief Макросы для упрощения создания таблиц символов
 */
#define ESP_ELFSYM_EXPORT(_sym)     { #_sym, (const void*)&_sym }
#define ESP_ELFSYM_EXACT(_name, _sym) { _name, (const void*)&_sym }
#define ESP_ELFSYM_END              { NULL,  NULL }

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ищет символ в таблице по его имени
 * 
 * @param symbols Таблица символов (массив EspbSymbol, завершающийся ESP_ELFSYM_END)
 * @param name Имя символа для поиска
 * @return const void* Адрес символа или NULL, если символ не найден
 */
const void* espb_lookup_symbol_in_table(const EspbSymbol *symbols, const char *name);

/**
 * @brief Основная функция поиска символов для использования в resolve_imports
 * 
 * @param module_name Имя модуля символа (например, "env")
 * @param entity_name Имя символа (например, "malloc")
 * @return const void* Адрес символа или NULL, если символ не найден
 */
const void* espb_lookup_host_symbol(uint8_t module_num, const char *entity_name);

// Fast lookup helper used by the runtime to resolve index-based imports.
// Returns NULL if the table is not registered, index is out of range, or the slot is disabled (NULL).
const void* espb_lookup_fast_symbol(uint8_t import_flags, uint16_t symbol_index);

// Resolve-time helper: get pointers to currently registered fast tables.
// This is intended to be called once during module instantiation.
void espb_get_fast_tables(const EspbSymbolFast **idf_tbl, uint32_t *idf_cnt,
                          const EspbSymbolFast **custom_tbl, uint32_t *custom_cnt);

/**
 * @brief Регистрирует таблицу символов для использования при поиске
 * 
 * @param module_num номер модуля, с которым связана таблица
 * @param symbols Массив символов, заканчивающийся ESP_ELFSYM_END
 */
void espb_register_symbol_table(uint8_t module_num, const EspbSymbol *symbols);

// Register fast (index-based) tables. Only two are supported for now.
void espb_register_idf_fast_table(const EspbSymbolFast *symbols, uint32_t count);
void espb_register_custom_fast_table(const EspbSymbolFast *symbols, uint32_t count);

// Import flags for Func imports (must match the ESPB specification)
#define IMPORT_FLAG_INDEXED     0x10
#define IMPORT_FLAG_FAST_CUSTOM 0x20
#define IMPORT_FLAG_FAST_IDF    0x40


/**
 * @brief Инициализирует таблицы символов C++ для использования в ESPb.
 * 
 * Должна быть вызвана перед началом работы с ESPb для регистрации
 * необходимых символов для импорта функций C++.
 */
void init_cpp_symbols(void);


// Host-like callback helpers (available to ESPB imports)
typedef void (*espb_cb1_t)(void*);
typedef void (*espb_cb2_t)(int, void*);
void host_invoke_cb(espb_cb1_t cb, void *user_data);
void host_invoke_cb2(espb_cb2_t cb, int x, void *user_data);
void native_set_magic_number(int* out_value);

// Обертки для встроенных атомарных функций GCC
#ifdef __cplusplus
}
#endif

#endif // ESPB_HOST_SYMBOLS_H 