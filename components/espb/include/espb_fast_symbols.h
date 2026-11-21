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
#ifndef ESPB_FAST_SYMBOLS_H
#define ESPB_FAST_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Address-only symbol table entry (no names in firmware)
typedef struct {
    const void *address;
} EspbSymbolFast;

// Helper flags for ESPB_SYM_OPT that are NOT tied to Kconfig.
// They can be used inside *.sym files to quickly force-enable/disable a symbol entry
// while keeping the index slot stable.
//
// Example:
//   ESPB_SYM_OPT(CONFIG_ESPB_OFF, "some_symbol", (const void*)&some_symbol)
//
#ifndef CONFIG_ESPB_ON
#define CONFIG_ESPB_ON 1
#endif
#ifndef CONFIG_ESPB_OFF
#define CONFIG_ESPB_OFF 0
#endif

// Register fast symbol tables (called internally by init_cpp_symbols or by user)
void espb_register_idf_fast_table(const EspbSymbolFast *table, uint32_t count);
void espb_register_custom_fast_table(const EspbSymbolFast *table, uint32_t count);

#ifdef __cplusplus
}
#endif

// ============================================================================
// API для регистрации custom индексной таблицы символов
// ============================================================================
// Использование в app_main():
//
//    espb_register_custom_index_symbol_table(my_custom_table);
//
// Размер таблицы вычисляется автоматически.
// ВАЖНО: Порядок записей должен совпадать с порядком при трансляции!
//
// ПРИМЕЧАНИЕ: idf_fast таблица регистрируется автоматически в init_cpp_symbols()
// ============================================================================

// Вспомогательные макросы для .sym файлов
#define ESPB_SYM(_name, _expr)          { (const void*)(_expr) },
#define ESPB_SYM_OPT(_cfg, _name, _expr) { (_cfg) ? (const void*)(_expr) : (const void*)0 },

#ifdef __cplusplus
// C++ версия - template функция с автоматическим выводом размера
template<size_t N>
inline void espb_register_custom_index_symbol_table(const EspbSymbolFast (&table)[N]) {
    espb_register_custom_fast_table(table, static_cast<uint32_t>(N));
}
#else
// C версия - макрос с sizeof
#define espb_register_custom_index_symbol_table(table) \
    espb_register_custom_fast_table(table, \
        (uint32_t)(sizeof(table) / sizeof(table[0])))
#endif

// Пример использования в main.cpp:
//
//   // Определить custom индексную таблицу
//   const EspbSymbolFast my_custom_fast_table[] = {
//       #include "symbols/custom_fast.sym"
//   };
//   
//   // В app_main():
//   espb_register_custom_index_symbol_table(my_custom_fast_table);

#endif // ESPB_FAST_SYMBOLS_H
