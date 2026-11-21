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
#ifndef ESPB_JIT_DISPATCHER_H
#define ESPB_JIT_DISPATCHER_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Основная точка входа для выполнения функции ESPB с поддержкой JIT.
 * 
 * Эта функция-диспетчер проверяет, была ли функция скомпилирована JIT.
 * Если да, она выполняет нативный код. В противном случае, она вызывает
 * стандартный интерпретатор.
 *
 * @param instance Указатель на инстанс модуля ESPB.
 * @param exec_ctx Контекст выполнения для текущего потока.
 * @param func_idx Индекс функции для выполнения.
 * @param args Указатель на массив аргументов для функции.
 * @param results Указатель на Value для сохранения результата.
 * @return EspbResult Код результата выполнения.
 */
EspbResult espb_execute_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);

/**
 * @brief Выполнить функцию ТОЛЬКО через JIT (без fallback на интерпретатор).
 *
 * Используется, когда код уже исполняется в JIT и переход в интерпретатор недопустим/нежелателен.
 * Если JIT-компиляция невозможна (неподдерживаемые опкоды и т.п.), функция вернёт ошибку.
 */
EspbResult espb_execute_function_jit_only(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);


/**
 * @brief Заглушка для выполнения скомпилированного JIT-кода.
 * 
 * В будущем эта функция будет принимать указатель на скомпилированный код
 * и необходимые аргументы, и выполнять его.
 *
 * @param jit_code Указатель на буфер с нативным кодом.
 * @param args Указатель на массив аргументов.
 * @param results Указатель на Value для сохранения результата.
 * @param frame_size Размер стекового кадра, который нужно выделить.
 * @return EspbResult Код результата выполнения.
 */
EspbResult execute_jit_code(EspbInstance *instance, void *jit_code, const Value *args, uint8_t num_args, Value *results, uint16_t frame_size);


#ifdef __cplusplus
}
#endif

#endif // ESPB_JIT_DISPATCHER_H