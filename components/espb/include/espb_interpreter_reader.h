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
// === БЫСТРОЕ ЧТЕНИЕ ОПЕРАНДОВ (для горячего цикла интерпретатора) ===
// Эти макросы предполагают, что указатель `pc` находится в области видимости
// и что чтение не выйдет за границы. Они предназначены для горячих циклов,
// где производительность критична, а проверки границ выполняются заранее.

// Специальные типы для невыровненного доступа
typedef uint16_t __attribute__((aligned(1))) unaligned_u16_t;
typedef int16_t  __attribute__((aligned(1))) unaligned_i16_t;
typedef uint32_t __attribute__((aligned(1))) unaligned_u32_t;
typedef int32_t  __attribute__((aligned(1))) unaligned_i32_t;
typedef uint64_t __attribute__((aligned(1))) unaligned_u64_t;
typedef int64_t  __attribute__((aligned(1))) unaligned_i64_t;
typedef float    __attribute__((aligned(1))) unaligned_f32_t;
typedef double   __attribute__((aligned(1))) unaligned_f64_t;

// Чтение байта
#define READ_U8()  (*pc++)
#define READ_I8()  ((int8_t)(*pc++))

// Чтение 16 бит (1 инструкция CPU)
#define READ_I16() ({ int16_t v = *(const unaligned_i16_t*)pc; pc += 2; v; })
#define READ_U16() ({ uint16_t v = *(const unaligned_u16_t*)pc; pc += 2; v; })

// Чтение 32 бит (1 инструкция CPU)
#define READ_I32() ({ int32_t v = *(const unaligned_i32_t*)pc; pc += 4; v; })
#define READ_U32() ({ uint32_t v = *(const unaligned_u32_t*)pc; pc += 4; v; })
#define READ_F32() ({ float v = *(const unaligned_f32_t*)pc; pc += 4; v; })

// Чтение 64 бит (2 инструкции CPU на 32-бит arch)
#define READ_I64() ({ int64_t v = *(const unaligned_i64_t*)pc; pc += 8; v; })
#define READ_U64() ({ uint64_t v = *(const unaligned_u64_t*)pc; pc += 8; v; })
#define READ_F64() ({ double v = *(const unaligned_f64_t*)pc; pc += 8; v; })


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
