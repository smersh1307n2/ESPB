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

#ifndef ESPB_INTERPRETER_READER_H
#define ESPB_INTERPRETER_READER_H

#include "espb_interpreter_common_types.h"
// <stdint.h>, <stddef.h>, <stdbool.h> уже включены через common_types

#ifdef __cplusplus
extern "C" {
#endif

// Чтение значений из буфера
bool read_u8(const uint8_t **ptr, const uint8_t *end, uint8_t *value);
bool read_u16(const uint8_t **ptr, const uint8_t *end, uint16_t *value);
bool read_u32(const uint8_t **ptr, const uint8_t *end, uint32_t *value);
bool read_i32(const uint8_t **ptr, const uint8_t *end, int32_t *value);
bool read_i8(const uint8_t **ptr, const uint8_t *end, int8_t *value);
bool read_i16(const uint8_t **ptr, const uint8_t *end, int16_t *value);
bool read_i64(const uint8_t **ptr, const uint8_t *end, int64_t *value);
bool read_u64(const uint8_t **ptr, const uint8_t *end, uint64_t *value);
bool read_f32(const uint8_t **ptr, const uint8_t *end, float *value);
bool read_f64(const uint8_t **ptr, const uint8_t *end, double *value);

// Печать дампа памяти в шестнадцатеричном виде
void print_memory_dump(const uint8_t *data, size_t size, const char *prefix);

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_READER_H
