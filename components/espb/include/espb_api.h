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

#ifndef ESPB_API_H
#define ESPB_API_H

#include <stdint.h>
#include <stddef.h>
#include "espb_interpreter_common_types.h"

// Определяем непрозрачный указатель для дескриптора модуля
typedef struct espb_module_handle_t* espb_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

// Макросы для упрощенного создания Value аргументов
#define ESPB_I8(v)     ((Value){.type = ESPB_TYPE_I8, .value = {.i32 = (int8_t)(v)}})
#define ESPB_U8(v)     ((Value){.type = ESPB_TYPE_U8, .value = {.i32 = (uint8_t)(v)}})
#define ESPB_I16(v)    ((Value){.type = ESPB_TYPE_I16, .value = {.i32 = (int16_t)(v)}})
#define ESPB_U16(v)    ((Value){.type = ESPB_TYPE_U16, .value = {.i32 = (uint16_t)(v)}})
#define ESPB_I32(v)    ((Value){.type = ESPB_TYPE_I32, .value = {.i32 = (int32_t)(v)}})
#define ESPB_U32(v)    ((Value){.type = ESPB_TYPE_U32, .value = {.u32 = (uint32_t)(v)}})
#define ESPB_I64(v)    ((Value){.type = ESPB_TYPE_I64, .value = {.i64 = (int64_t)(v)}})
#define ESPB_U64(v)    ((Value){.type = ESPB_TYPE_U64, .value = {.u64 = (uint64_t)(v)}})
#define ESPB_F32(v)    ((Value){.type = ESPB_TYPE_F32, .value = {.f32 = (float)(v)}})
#define ESPB_F64(v)    ((Value){.type = ESPB_TYPE_F64, .value = {.f64 = (double)(v)}})
#define ESPB_PTR(v)    ((Value){.type = ESPB_TYPE_PTR, .value = {.ptr = (void*)(v)}})
#define ESPB_BOOL(v)   ((Value){.type = ESPB_TYPE_BOOL, .value = {.i32 = (v) ? 1 : 0}})
#define ESPB_V128(v)   ((Value){.type = ESPB_TYPE_V128, .value = {.v128 = (v)}})
#define ESPB_FUNC(idx) ((Value){.type = ESPB_TYPE_INTERNAL_FUNC_IDX, .value = {.func_idx = (idx)}})
#define ESPB_VOID()    ((Value){.type = ESPB_TYPE_VOID, .value = {.i32 = 0}})

// Макросы для передачи массивов (массивы передаются как указатели)
// Эти макросы просто для наглядности - они эквивалентны ESPB_PTR
#define ESPB_ARRAY(arr)     ESPB_PTR(arr)
#define ESPB_STRING(str)    ESPB_PTR(str)

/**
 * @brief Загружает и инстанцирует модуль ESPB.
 *
 * @param espb_data Указатель на данные модуля ESPB.
 * @param espb_size Размер данных модуля в байтах.
 * @param out_handle Указатель на переменную для сохранения дескриптора модуля.
 * @return ESPB_OK в случае успеха, или код ошибки.
 */
EspbResult espb_load_module(const uint8_t *espb_data, size_t espb_size, espb_handle_t *out_handle);

/**
 * @brief Выгружает модуль ESPB и освобождает все ресурсы.
 *
 * @param handle Дескриптор модуля, полученный от espb_load_module.
 */
void espb_unload_module(espb_handle_t handle);


/**
 * @brief Синхронно вызывает функцию в загруженном модуле ESPB.
 *
 * @param handle Дескриптор модуля.
 * @param function_name Имя экспортируемой функции для вызова.
 * @param args Массив аргументов для функции.
 * @param num_args Количество аргументов в массиве.
 * @param results Указатель на массив для сохранения результатов (может быть NULL).
 * @return ESPB_OK в случае успешного выполнения, или код ошибки.
 */
EspbResult espb_call_function_sync(espb_handle_t handle, const char* function_name, const Value *args, uint32_t num_args, Value *results);


#ifdef __cplusplus
}
#endif

#endif // ESPB_API_H
