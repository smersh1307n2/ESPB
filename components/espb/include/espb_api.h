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

// Макросы для создания Value-аргументов перенесены в espb_interpreter_common_types.h

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
