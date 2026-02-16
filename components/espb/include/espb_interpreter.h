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
#ifndef ESPB_INTERPRETER_H
#define ESPB_INTERPRETER_H

// Основные внешние зависимости, если нужны напрямую в API
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Общие типы данных, используемые во всем интерпретаторе
#include "espb_interpreter_common_types.h"

// Публичный API для парсинга модулей
#include "espb_interpreter_parser.h"

// Публичный API для выполнения модулей
#include "espb_interpreter_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Основные константы API ---
#define ESPB_MAGIC_NUMBER 0x42505345 // 'ESPB' в Little-Endian
#define ESPB_VERSION_1_6 0x00000106 // Пример старой версии (если нужно)
#define ESPB_VERSION_1_7 0x00000107 // Актуальная версия, с которой работает парсер

// Определения кодов ошибок (EspbResult) теперь находятся в espb_interpreter_common_types.h
// Макрос MIN также находится в espb_interpreter_common_types.h

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_H 