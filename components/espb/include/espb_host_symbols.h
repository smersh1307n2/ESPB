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

/**
 * @brief Структура для хранения информации о символе (функции или глобальной переменной)
 */
typedef struct {
    const char *name;    // Имя символа (без пространства имен)
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
const void* espb_lookup_host_symbol(const char *module_name, const char *entity_name);

/**
 * @brief Регистрирует таблицу символов для использования при поиске
 * 
 * @param module_name Имя модуля, с которым связана таблица (например, "env")
 * @param symbols Массив символов, заканчивающийся ESP_ELFSYM_END
 */
void espb_register_symbol_table(const char *module_name, const EspbSymbol *symbols);

/**
 * @brief Инициализирует таблицы символов C++ для использования в ESPb.
 * 
 * Должна быть вызвана перед началом работы с ESPb для регистрации
 * необходимых символов для импорта функций C++.
 */
void init_cpp_symbols(void);
void init_c_symbols(void);

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