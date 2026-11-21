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
#ifndef ESPB_INTERPRETER_PARSER_H
#define ESPB_INTERPRETER_PARSER_H

#include "espb_interpreter_common_types.h"
// <stdint.h>, <stddef.h> уже включены через common_types

#ifdef __cplusplus
extern "C" {
#endif

// --- Основные функции парсинга модуля ---

EspbResult espb_parse_module(EspbModule **out_module, const uint8_t *buffer, size_t buffer_size);
void espb_free_module(EspbModule *module);

// --- Вспомогательные функции парсинга ---

// Поиск секции по ID
bool espb_find_section(const EspbModule *module, uint8_t section_id, const uint8_t **out_data, uint32_t *out_size);

// Парсинг заголовка и таблицы секций
EspbResult espb_parse_header_and_sections(EspbModule *module, const uint8_t *buffer, size_t buffer_size);

// --- Функции парсинга отдельных секций ---

EspbResult espb_parse_types_section(EspbModule *module);
EspbResult espb_parse_functions_section(EspbModule *module);
EspbResult espb_parse_code_section(EspbModule *module);
EspbResult espb_parse_memory_section(EspbModule *module);
EspbResult espb_parse_globals_section(EspbModule *module);
EspbResult espb_parse_data_section(EspbModule *module);
EspbResult espb_parse_imports_section(EspbModule *module);
EspbResult espb_parse_exports_section(EspbModule *module);
EspbResult espb_parse_relocations_section(EspbModule *module);
EspbResult espb_parse_tables_section(EspbModule *module);
EspbResult espb_parse_element_section(EspbModule *module);
EspbResult espb_parse_start_section(EspbModule *module);
EspbResult espb_parse_cbmeta_section(EspbModule *module);
EspbResult espb_parse_immeta_section(EspbModule *module);
EspbResult espb_parse_func_ptr_map_section(EspbModule *module);

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_PARSER_H
