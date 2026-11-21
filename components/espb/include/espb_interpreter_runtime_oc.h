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
#ifndef ESPB_INTERPRETER_RUNTIME_OC_H
#define ESPB_INTERPRETER_RUNTIME_OC_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

ExecutionContext* init_execution_context(void);
void free_execution_context(ExecutionContext *ctx);

/**
 * @brief Выполняет вызов функции ESPb по ее индексу.
 *
 * @param instance Указатель на инстанс модуля ESPb.
 * @param exec_ctx Указатель на контекст выполнения.
 * @param func_idx Индекс вызываемой функции (включая импорты).
 * @param args Указатель на массив аргументов типа Value. Количество должно соответствовать сигнатуре функции. Может быть NULL, если аргументов нет.
 * @param results Указатель на массив для возвращаемых значений типа Value. Количество должно соответствовать сигнатуре функции. Может быть NULL, если возвращаемых значений нет или они не нужны.
 * @return EspbResult Код результата выполнения. ESPB_OK в случае успеха.
 */
EspbResult espb_call_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_RUNTIME_OC_H
