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
#ifndef ESPB_INTERPRETER_RUNTIME_H
#define ESPB_INTERPRETER_RUNTIME_H

#include "espb_interpreter_common_types.h"
// <stdint.h>, <stddef.h> уже включены через common_types

#ifdef __cplusplus
extern "C" {
#endif

// --- Функции инстанцирования и исполнения ---

EspbResult espb_instantiate(EspbInstance **out_instance, const EspbModule *module /*, HostResolver *host_resolver */);
void espb_free_instance(EspbInstance *instance);

EspbResult espb_apply_relocations(EspbInstance *instance);
EspbResult espb_initialize_data_segments(EspbInstance *instance);
EspbResult espb_initialize_element_segments(EspbInstance *instance);

EspbResult espb_run_start_function(EspbInstance *instance, ExecutionContext *exec_ctx);
EspbResult espb_call_function(EspbInstance *instance, ExecutionContext *exec_ctx, uint32_t func_idx, const Value *args, Value *results);

ExecutionContext* init_execution_context(void);
void free_execution_context(ExecutionContext *ctx);

// Вспомогательные функции времени выполнения (обычно static и не объявляются здесь, но если что-то нужно извне...)
// static EspbResult allocate_linear_memory(EspbInstance *instance); // Пример
// static EspbResult allocate_globals(EspbInstance *instance);
// static EspbResult allocate_tables(EspbInstance *instance);
// static EspbResult resolve_imports(EspbInstance *instance);
// static EspbResult espb_evaluate_init_expr(const EspbInstance *instance, const uint8_t *expr, size_t expr_len, uint32_t *out_value);

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_RUNTIME_H
